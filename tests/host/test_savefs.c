/*
 * Host tests of the "save:/" filesystem (src/n64/n64_savefs.c), on a
 * simulated FlashRAM with the libdragon driver's rules (tests/host/stubs).
 *
 *   make -C tests/host
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>

#include "../../src/n64/n64_savefs.c"

/*
 * ------------------------------------------------------------------------
 *  Simulated FlashRAM
 * ------------------------------------------------------------------------
 */

#define FLASH_SIZE (128 * 1024)
#define SECTOR_SIZE (16 * 1024)

static uint8_t flash[FLASH_SIZE];
// Writes left before one is cut short, as by a power cut (-1: never): its
// sectors end up erased, then half programmed.
static int flash_fail_after = -1;
static int flash_sectors_written = 0;
// Like many flashcarts and emulators: writes work, but the status bits that
// say so stay unset, and flashram_write() reports a failure.
static int flash_no_status = 0;
// A broken chip: sectors are erased, never programmed.
static int flash_dead = 0;
static filesystem_t *fs;

static jmp_buf test_abort;
static const char *test_name;
static int failures = 0;

static void fail(const char *fmt, ...)
{
    va_list args;

    printf("FAIL %s: ", test_name);
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    failures++;
    longjmp(test_abort, 1);
}

bool flashram_init(const void *timings, flashram_info_t *info)
{
    info->name = "simulated";
    info->total_size = FLASH_SIZE;
    info->sector_size = SECTOR_SIZE;
    return true;
}

int flashram_read(void *dst, size_t offset, size_t len)
{
    if (offset + len > FLASH_SIZE)
        fail("flashram_read out of range: offset=0x%X len=0x%X", (unsigned)offset, (unsigned)len);
    if (len && (offset & 1))
        fail("flashram_read requires an even offset, got 0x%X", (unsigned)offset);
    memcpy(dst, flash + offset, len);
    return len;
}

int flashram_write(const void *src, size_t offset, size_t len)
{
    if (offset + len > FLASH_SIZE)
        fail("flashram_write out of range: offset=0x%X len=0x%X", (unsigned)offset, (unsigned)len);
    if (flash_fail_after == 0)
    {
        flash_fail_after = -1;
        memset(flash + offset, 0xFF, len);
        memcpy(flash + offset, src, len / 2);
        return -1;
    }
    if (flash_dead)
    {
        memset(flash + offset, 0xFF, len);
        return -1;
    }
    if (flash_fail_after > 0)
        flash_fail_after--;
    memcpy(flash + offset, src, len);
    flash_sectors_written += (len + SECTOR_SIZE - 1) / SECTOR_SIZE;
    return flash_no_status ? -1 : (int)len;
}

int attach_filesystem(const char *prefix, filesystem_t *filesystem)
{
    fs = filesystem;
    return 0;
}

/*
 * ------------------------------------------------------------------------
 *  Helpers
 * ------------------------------------------------------------------------
 */

// Boots again: forgets everything but the FlashRAM.
static void remount(void)
{
    mounted = 0;
    last_write_error = 0;
    memset(files, 0xAA, sizeof(files));
    nfiles = 12345;
    fs = NULL;
    n64_savefs_init();
    if (!fs)
        fail("save:/ not attached");
}

static void format_flash(void)
{
    memset(flash, 0xFF, sizeof(flash));
    flash_fail_after = -1;
    flash_no_status = 0;
    flash_dead = 0;
    remount();
}

static uint32_t rng_state;

static uint32_t rng(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return rng_state >> 8;
}

// Something like a save: zero runs, repeated records with small changes,
// text and noise. noise_pct of the bytes are random (100: incompressible).
static void make_data(uint8_t *buf, int len, uint32_t seed, int noise_pct)
{
    int i = 0;

    rng_state = seed;
    while (i < len)
    {
        int kind = rng() % 4, n = 1 + rng() % 600, k;
        if (i + n > len)
            n = len - i;
        for (k = 0; k < n; k++, i++)
        {
            if ((int)(rng() % 100) < noise_pct)
                buf[i] = rng();
            else if (kind == 0)
                buf[i] = 0;
            else if (kind == 1)
                buf[i] = (i >= 44) ? buf[i - 44] + (k % 7 == 0) : k;
            else if (kind == 2)
                buf[i] = "DUKE NUKEM FOREVER "[k % 19];
            else
                buf[i] = rng() & 0x0F;
        }
    }
}

// Writes a file in pieces of varying size. Returns what close returned.
static int write_file(const char *name, const uint8_t *data, int len)
{
    char path[32];
    void *f;
    int done = 0;

    snprintf(path, sizeof(path), "/%s", name);
    f = fs->open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (!f)
        fail("open %s for writing", name);
    rng_state = len;
    while (done < len)
    {
        int n = 1 + rng() % 5000;
        if (n > len - done)
            n = len - done;
        if (fs->write(f, (uint8_t *)data + done, n) != n)
            fail("write %s", name);
        done += n;
    }
    return fs->close(f);
}

// Checks that name holds exactly data[0..len).
static void check_file(const char *name, const uint8_t *data, int len)
{
    static uint8_t buf[FLASH_SIZE * 8];
    struct stat st;
    void *f;
    int done = 0, n;

    if (fs->stat((char *)name, &st) != 0)
        fail("%s: stat failed", name);
    if (st.st_size != len)
        fail("%s: size %ld, expected %d", name, (long)st.st_size, len);
    f = fs->open((char *)name, O_RDONLY);
    if (!f)
        fail("%s: open failed", name);
    while ((n = fs->read(f, buf + done, 3001)) > 0)
        done += n;
    fs->close(f);
    if (n < 0)
        fail("%s: read error after %d bytes (corrupt data)", name, done);
    if (done != len)
        fail("%s: read %d bytes, expected %d", name, done, len);
    if (memcmp(buf, data, len) != 0)
        fail("%s: contents differ", name);
}

static void check_missing(const char *name)
{
    struct stat st;
    if (fs->stat((char *)name, &st) == 0)
        fail("%s: should not exist", name);
}

/*
 * ------------------------------------------------------------------------
 *  Tests
 * ------------------------------------------------------------------------
 */

static void test_lz_roundtrip(void)
{
    static uint8_t in[CHUNK_SIZE], packed[CHUNK_SIZE + CHUNK_SIZE / 64 + 16], out[CHUNK_SIZE];
    static const int sizes[] = { 1, 2, 3, 4, 5, 127, 128, 129, 130, 1000, 4097, CHUNK_SIZE - 1, CHUNK_SIZE };
    static const int noise[] = { 0, 5, 30, 100 };
    unsigned s, p;

    for (p = 0; p < sizeof(noise) / sizeof(noise[0]); p++)
        for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++)
        {
            int len = sizes[s];
            uint32_t c;

            make_data(in, len, 1000 + s * 31 + p, noise[p]);
            c = lz_compress(in, len, packed);
            if (c > sizeof(packed))
                fail("lz_compress overflowed: %u bytes for %d", (unsigned)c, len);
            if (lz_decompress(packed, c, out, len) != 0 || memcmp(in, out, len) != 0)
                fail("lz roundtrip, %d bytes, %d%% noise", len, noise[p]);
        }
}

static void test_roundtrip_and_seek(void)
{
    static uint8_t data[300000];
    static uint8_t buf[64];
    void *f;
    int len = 120000;

    format_flash();
    make_data(data, len, 7, 1);
    if (write_file("game0.sav", data, len) != 0)
        fail("write failed");
    remount();
    check_file("game0.sav", data, len);

    // Forwards, backwards and past the end, like the game's loader.
    f = fs->open("game0.sav", O_RDONLY);
    if (fs->lseek(f, 100000, SEEK_SET) != 100000 || fs->read(f, buf, 64) != 64 ||
        memcmp(buf, data + 100000, 64) != 0)
        fail("seek forwards");
    if (fs->lseek(f, 10, SEEK_SET) != 10 || fs->read(f, buf, 64) != 64 || memcmp(buf, data + 10, 64) != 0)
        fail("seek backwards");
    if (fs->lseek(f, -5, SEEK_END) != len - 5 || fs->read(f, buf, 64) != 5)
        fail("seek from the end");
    fs->close(f);
}

// The order the slots end up in the image is not the directory's: a file
// rewritten goes to the end of the data, after files of later entries.
static void test_rewrite_then_new_file(void)
{
    static uint8_t a[20000], a2[20000], b[15000], c[9000];

    format_flash();
    make_data(a, sizeof(a), 1, 10);
    make_data(a2, sizeof(a2), 2, 10);
    make_data(b, sizeof(b), 3, 10);
    make_data(c, sizeof(c), 4, 10);

    if (write_file("game0.sav", a, sizeof(a)) || write_file("game1.sav", b, sizeof(b)))
        fail("write failed");
    if (write_file("game0.sav", a2, sizeof(a2)))
        fail("rewrite failed");
    if (write_file("game2.sav", c, sizeof(c)))
        fail("write failed");

    check_file("game0.sav", a2, sizeof(a2));
    check_file("game1.sav", b, sizeof(b));
    check_file("game2.sav", c, sizeof(c));
    remount();
    check_file("game0.sav", a2, sizeof(a2));
    check_file("game1.sav", b, sizeof(b));
    check_file("game2.sav", c, sizeof(c));
}

// Incompressible files of odd sizes: compressed sizes are odd as well.
static void test_odd_sizes(void)
{
    static uint8_t a[1001], b[2003], c[517];

    format_flash();
    make_data(a, sizeof(a), 11, 100);
    make_data(b, sizeof(b), 12, 100);
    make_data(c, sizeof(c), 13, 100);
    if (write_file("game0.sav", a, sizeof(a)) || write_file("game1.sav", b, sizeof(b)) ||
        write_file("game2.sav", c, sizeof(c)))
        fail("write failed");
    remount();
    check_file("game0.sav", a, sizeof(a));
    check_file("game1.sav", b, sizeof(b));
    check_file("game2.sav", c, sizeof(c));
}

// The chunks a file is stored as (the data of a record).
static uint32_t encode(const uint8_t *data, int len, uint8_t *out)
{
    savefs_file_t *f = fs->open("/encode", O_WRONLY);
    uint32_t n;

    if (!f || fs->write(f, (uint8_t *)data, len) != len)
        fail("encode");
    flush_chunk(f);
    n = f->out_len;
    memcpy(out, f->out, n);
    free(f->out);
    free(f);
    return n;
}

// A version 2 image (directory at 0, files from 0x200 one right after the
// other, so at odd offsets too) is turned into records when it is mounted.
static void test_v2_image(void)
{
    static uint8_t a[1001], b[3000], c[4000], d[2500];
    static uint8_t enc[3][8192];
    const uint8_t *src[3] = { a, b, c };
    const int len[3] = { sizeof(a), sizeof(b), sizeof(c) };
    savefs_v2_dir_t v2;
    uint32_t off = 0x200;
    int i;

    format_flash();
    make_data(a, sizeof(a), 21, 100);
    make_data(b, sizeof(b), 22, 20);
    make_data(c, sizeof(c), 23, 20);
    make_data(d, sizeof(d), 24, 20);

    memset(&v2, 0, sizeof(v2));
    memcpy(v2.magic, SAVEFS_V2_MAGIC, 8);
    for (i = 0; i < 3; i++)
    {
        uint32_t n = encode(src[i], len[i], enc[i]);
        snprintf(v2.entry[i].name, sizeof(v2.entry[i].name), "game%d.sav", i);
        v2.entry[i].offset = off;
        v2.entry[i].csize = n;
        v2.entry[i].usize = len[i];
        memcpy(flash + off, enc[i], n);
        off += n;
    }
    v2.count = 3;
    memcpy(flash, &v2, sizeof(v2));
    if (!(v2.entry[1].offset & 1))
        fail("test setup: game1.sav should be at an odd offset");

    remount();
    if (v2_image || nfiles != 3)
        fail("version 2 image not converted (%d files)", nfiles);
    if (memcmp(flash, SAVEFS_V2_MAGIC, 8) == 0)
        fail("the version 2 directory is still there");
    check_file("game0.sav", a, sizeof(a));
    check_file("game1.sav", b, sizeof(b));
    check_file("game2.sav", c, sizeof(c));

    if (write_file("game1.sav", d, sizeof(d)))
        fail("write failed");
    remount();
    check_file("game0.sav", a, sizeof(a));
    check_file("game1.sav", d, sizeof(d));
    check_file("game2.sav", c, sizeof(c));
}

// A power cut while a file is written leaves its previous version.
static void test_power_cut(void)
{
    static uint8_t a1[30000], a2[30000], b[20000];
    int cut;

    for (cut = 0; cut < 3; cut++)
    {
        format_flash();
        make_data(a1, sizeof(a1), 51, 30);
        make_data(a2, sizeof(a2), 52, 30);
        make_data(b, sizeof(b), 53, 30);
        if (write_file("game0.sav", a1, sizeof(a1)) || write_file("game1.sav", b, sizeof(b)))
            fail("write failed");

        flash_fail_after = cut;     // the 1st, 2nd or 3rd sector written
        write_file("game0.sav", a2, sizeof(a2));
        flash_fail_after = -1;

        remount();
        // Either version is fine if the cut came after the record was
        // complete; never a damaged file, nor another file lost.
        {
            struct stat st;
            void *f = fs->open("game0.sav", O_RDONLY);
            static uint8_t buf[30000];
            if (!f || fs->stat("game0.sav", &st) != 0 || fs->read(f, buf, sizeof(buf)) != sizeof(buf))
                fail("cut at sector write %d: game0.sav lost", cut);
            fs->close(f);
            if (memcmp(buf, a1, sizeof(a1)) != 0 && memcmp(buf, a2, sizeof(a2)) != 0)
                fail("cut at sector write %d: game0.sav damaged", cut);
        }
        check_file("game1.sav", b, sizeof(b));
    }
}

// Damaged data is never loaded: the previous version takes its place, or
// the file is missing.
static void test_crc(void)
{
    static uint8_t v1[8000], v2[8000], c[5000];
    uint32_t off;

    format_flash();
    make_data(v1, sizeof(v1), 61, 20);
    make_data(v2, sizeof(v2), 62, 20);
    make_data(c, sizeof(c), 63, 20);
    // (The space of an older version is free: only until the next write
    // uses it is it there to fall back on.)
    if (write_file("game0.sav", v1, sizeof(v1)) || write_file("game0.sav", v2, sizeof(v2)))
        fail("write failed");
    off = files[find_entry("game0.sav")].offset;
    flash[off + sizeof(savefs_record_t) + 100] ^= 0x10;
    remount();
    check_file("game0.sav", v1, sizeof(v1));

    if (write_file("game1.sav", c, sizeof(c)))
        fail("write failed");
    off = files[find_entry("game1.sav")].offset;
    flash[off + 20] ^= 0x01;        // (in the header)
    remount();
    check_file("game0.sav", v1, sizeof(v1));
    check_missing("game1.sav");
}

// A save only programs the sectors it changes.
static void test_sectors_written(void)
{
    static uint8_t a[6000], b[3000];
    char name[16];
    int i;

    format_flash();
    for (i = 0; i < 6; i++)
    {
        make_data(a, sizeof(a), 70 + i, 30);
        snprintf(name, sizeof(name), "game%d.sav", i);
        if (write_file(name, a, sizeof(a)))
            fail("write failed");
    }
    make_data(b, sizeof(b), 80, 30);
    flash_sectors_written = 0;
    if (write_file("settings.dat", b, sizeof(b)))
        fail("write failed");
    if (flash_sectors_written > 2)
        fail("%d sectors written for a small file", flash_sectors_written);
}

static void test_max_files(void)
{
    static uint8_t a[500];
    char name[16];
    int i;

    format_flash();
    for (i = 0; i <= SAVEFS_MAX_FILES; i++)
    {
        make_data(a, sizeof(a), 90 + i, 10);
        snprintf(name, sizeof(name), "file%02d.dat", i);
        if ((write_file(name, a, sizeof(a)) == 0) != (i < SAVEFS_MAX_FILES))
            fail("file %d: %s", i, i < SAVEFS_MAX_FILES ? "not saved" : "saved past the limit");
    }
    remount();
    for (i = 0; i < SAVEFS_MAX_FILES; i++)
    {
        make_data(a, sizeof(a), 90 + i, 10);
        snprintf(name, sizeof(name), "file%02d.dat", i);
        check_file(name, a, sizeof(a));
    }
}

static void test_full(void)
{
    static uint8_t keep[30000], big[200000];
    char name[16];
    int i, stored = 0;

    format_flash();
    make_data(keep, sizeof(keep), 31, 10);
    if (write_file("game0.sav", keep, sizeof(keep)))
        fail("write failed");

    // Incompressible files until one does not fit.
    for (i = 1; i < 10; i++)
    {
        make_data(big, 40000, 100 + i, 100);
        snprintf(name, sizeof(name), "game%d.sav", i);
        if (write_file(name, big, 40000) != 0)
            break;
        stored++;
    }
    if (i == 10)
        fail("128 KB took %d incompressible files of 40 KB", stored);
    if (n64_savefs_error() != ENOSPC)
        fail("n64_savefs_error() should report ENOSPC, not %d", n64_savefs_error());
    check_missing(name);

    // A rewrite too big keeps the previous version.
    make_data(big, sizeof(big), 99, 100);
    if (write_file("game0.sav", big, sizeof(big)) == 0)
        fail("a 200 KB incompressible file fit");
    remount();
    check_file("game0.sav", keep, sizeof(keep));
    for (i = 1; i <= stored; i++)
    {
        make_data(big, 40000, 100 + i, 100);
        snprintf(name, sizeof(name), "game%d.sav", i);
        check_file(name, big, 40000);
    }
}

// A deleted file gives its space to the others (the game deletes the save
// slots it no longer offers, see readsavenames).
static void test_unlink(void)
{
    static uint8_t big[40000];
    char name[16];
    int i;

    format_flash();
    for (i = 0; i < 3; i++)
    {
        make_data(big, sizeof(big), 200 + i, 100);
        snprintf(name, sizeof(name), "game%d.sav", i);
        if (write_file(name, big, sizeof(big)) != 0)
            fail("%s: not saved", name);
    }
    make_data(big, sizeof(big), 203, 100);
    if (write_file("game3.sav", big, sizeof(big)) == 0)
        fail("a fourth incompressible file of 40 KB fit");

    if (fs->unlink("/game2.sav") != 0)
        fail("unlink failed");
    if (fs->unlink("/game2.sav") == 0 || errno != ENOENT)
        fail("unlink of a missing file should fail with ENOENT");
    check_missing("game2.sav");
    if (write_file("game3.sav", big, sizeof(big)) != 0)
        fail("no room after the unlink");

    remount();
    check_missing("game2.sav");     // its record was overwritten
    for (i = 0; i < 4; i++)
    {
        if (i == 2)
            continue;
        make_data(big, sizeof(big), 200 + i, 100);
        snprintf(name, sizeof(name), "game%d.sav", i);
        check_file(name, big, sizeof(big));
    }
}

static void test_write_error(void)
{
    static uint8_t a[5000];

    format_flash();
    make_data(a, sizeof(a), 41, 10);
    flash_dead = 1;
    if (write_file("game0.sav", a, sizeof(a)) == 0)
        fail("a failed FlashRAM write was reported as saved");
    if (n64_savefs_error() != EIO)
        fail("n64_savefs_error() should report EIO, not %d", n64_savefs_error());
    flash_dead = 0;
}

// A FlashRAM whose status bits never say "OK" (flashcarts, emulators):
// what is written reads back, so it is saved.
static void test_no_status_bits(void)
{
    static uint8_t a[20000], b[9000];

    format_flash();
    flash_no_status = 1;
    make_data(a, sizeof(a), 45, 20);
    make_data(b, sizeof(b), 46, 20);
    if (write_file("game0.sav", a, sizeof(a)) || write_file("game1.sav", b, sizeof(b)) ||
        write_file("game0.sav", b, sizeof(b)))
        fail("not saved (error %d)", n64_savefs_error());
    remount();
    check_file("game0.sav", b, sizeof(b));
    check_file("game1.sav", b, sizeof(b));
}

// Random saves over the 10 slots, checked against a model of what they hold.
static void test_random(void)
{
    enum { NAMES = 10, MAXLEN = 60000, STEPS = 400 };
    static uint8_t model[NAMES][MAXLEN], data[MAXLEN];
    static int model_len[NAMES];
    char name[16];
    int step, i;

    format_flash();
    for (i = 0; i < NAMES; i++)
        model_len[i] = -1;

    for (step = 0; step < STEPS; step++)
    {
        uint32_t seed = 5000 + step;
        int slot, len, noise;

        rng_state = seed * 7919u;
        slot = rng() % NAMES;
        len = (rng() % 8 == 0) ? 0 : rng() % MAXLEN;
        noise = (rng() % 4 == 0) ? 100 : rng() % 30;
        make_data(data, len, seed, noise);
        snprintf(name, sizeof(name), "game%d.sav", slot);

        if (write_file(name, data, len) == 0)
        {
            memcpy(model[slot], data, len);
            model_len[slot] = len;
        }

        if (step % 25 == 24)
            remount();
        for (i = 0; i < NAMES; i++)
        {
            snprintf(name, sizeof(name), "game%d.sav", i);
            if (model_len[i] < 0)
                check_missing(name);
            else
                check_file(name, model[i], model_len[i]);
        }
    }
}

/*
 * A save needs little memory: the file being written (its 16 KB chunk), its
 * compressed data as it grows, and one sector, also when the files have to
 * be packed together. Never a FlashRAM-sized buffer (the console's heap
 * does not have one free in the middle of a game).
 */
static void test_memory(void)
{
    static uint8_t save[200000], filler[25000];
    size_t base, used;
    uint32_t csize;
    char name[16];
    int i, packs = 0;

    format_flash();
    // Like a save: arrays mostly zero, records repeating with small changes.
    rng_state = 101;
    for (i = 0; i < (int)sizeof(save); i++)
        save[i] = (i % 509 == 0) ? (uint8_t)rng() : (i % 1000 < 300) ? (uint8_t)(i / 7) : 0;
    for (i = 0; i < 10; i++)
    {
        int e, k, moved = 0;
        uint32_t before[SAVEFS_MAX_FILES];

        base = test_heap_used;
        test_heap_peak = base;
        snprintf(name, sizeof(name), "game%d.sav", i % 3);
        if (i == 5)
        {
            // Fill the FlashRAM so that the next saves must pack the files.
            make_data(filler, sizeof(filler), 102, 100);
            write_file("filler1.dat", filler, sizeof(filler));
            write_file("filler2.dat", filler, sizeof(filler));
            base = test_heap_used;
            test_heap_peak = base;
        }
        e = find_entry(name);
        for (k = 0; k < nfiles; k++)
            before[k] = files[k].offset;
        save[i] ^= 0x55;                        // a different save each time
        if (write_file(name, save, sizeof(save)))
            fail("save %d not saved (error %d)", i, n64_savefs_error());
        e = find_entry(name);
        csize = files[e].csize;
        used = test_heap_peak - base;
        if (i == 0)
            printf("     (a save: %u bytes compressed, %u bytes of heap)\n", (unsigned)csize, (unsigned)used);
        if (used > 2 * csize + sizeof(savefs_file_t) + 2 * SECTOR_SIZE)
            fail("save %d (%u bytes compressed) took %u bytes of heap", i, (unsigned)csize, (unsigned)used);
        if (used >= FLASH_SIZE)
            fail("save %d (%u compressed) took a FlashRAM-sized buffer (%u bytes)", i, (unsigned)csize, (unsigned)used);
        for (k = 0; k < nfiles; k++)
            if (k != e && files[k].offset != before[k])
                moved = 1;
        packs += moved;
    }
    if (packs == 0)
        fail("the test never made the files pack together");
    if (test_heap_used != base)
        fail("%d bytes leaked", (int)(test_heap_used - base));
    remount();
    check_file(name, save, sizeof(save));
}

static void run(const char *name, void (*test)(void))
{
    int before = failures;

    test_name = name;
    if (setjmp(test_abort) == 0)
        test();
    if (failures == before)
        printf("ok   %s\n", name);
}

int main(void)
{
    run("lz_roundtrip", test_lz_roundtrip);
    run("roundtrip_and_seek", test_roundtrip_and_seek);
    run("rewrite_then_new_file", test_rewrite_then_new_file);
    run("odd_sizes", test_odd_sizes);
    run("v2_image", test_v2_image);
    run("power_cut", test_power_cut);
    run("crc", test_crc);
    run("sectors_written", test_sectors_written);
    run("max_files", test_max_files);
    run("full", test_full);
    run("unlink", test_unlink);
    run("write_error", test_write_error);
    run("no_status_bits", test_no_status_bits);
    run("memory", test_memory);
    run("random", test_random);

    printf("%s: %d failed\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
