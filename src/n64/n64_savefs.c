/*
 * "save:/" filesystem: the game's save files, compressed, in the cartridge
 * FlashRAM (128 KB, erased and written in sectors of 16 KB).
 *
 * The game keeps writing and reading its save files with the C library
 * (fopen/open, fwrite/read); this layer stores them:
 *
 *   FlashRAM image: records, each at a multiple of 16 bytes
 *     header  magic, file name, sequence number, sizes, CRC-32 of the data
 *             and of the header itself (savefs_record_t)
 *     data    the file, compressed
 *
 *   There is no directory: at boot the image is scanned, and a file is the
 *   record with its name and the highest sequence number whose CRCs check.
 *   A damaged record is ignored (an older version of the file, if one is
 *   still in the FlashRAM, takes its place).
 *
 *   Compressed data: a sequence of chunks, each holding up to CHUNK_SIZE
 *   bytes of the file: [uint16 stored size][uint16 original size][data].
 *   When stored size == original size the chunk is uncompressed, otherwise
 *   it is LZ compressed (see lz_compress). Chunks keep the memory needed
 *   small: a save is ~850 KB uncompressed, more than the free RAM.
 *
 * Writing a file (fclose) puts the new version in free space and leaves the
 * previous one where it was: if the power goes off half way, the new record
 * fails its CRC and the previous version is still there. Only when no free
 * space is big enough are the files packed together first (a rewrite of the
 * image a power cut can damage). Only the sectors that change are written.
 * If the file does not fit at all, the write fails (close returns -1 and
 * n64_savefs_error() says why) and the previous contents are kept.
 *
 * Images of version 2 (a directory at 0x000, the files after it from 0x200,
 * see read_v2) are read as they are and turned into records by the first
 * write.
 */
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <libdragon.h>
#include <system.h>

#include "n64_platform.h"

#define SAVEFS_MAX_FILES    16      // 10 saved games, the settings and room to spare
#define RECORD_MAGIC        0x444B5356  // "DKSV"
#define CHUNK_SIZE          16384
#define NO_SPACE            0xFFFFFFFFu

// Records start at multiples of 16 bytes: flashram_read() needs an even
// offset, and with 16 it can DMA straight into the (16 byte aligned) buffers.
#define SAVEFS_ALIGN(x)     (((x) + 15) & ~(uint32_t)15)

typedef struct {
    uint32_t magic;         // RECORD_MAGIC
    uint32_t seq;           // the highest of a name is its current version
    uint32_t csize;         // compressed data after the header
    uint32_t usize;         // original file size
    uint32_t data_crc;      // CRC-32 of the compressed data
    char name[16];
    uint32_t reserved[2];
    uint32_t header_crc;    // CRC-32 of the header up to here
} savefs_record_t;

_Static_assert(sizeof(savefs_record_t) == 48, "records must keep their data 16 byte aligned");

#define RECORD_SIZE(csize)  (sizeof(savefs_record_t) + (csize))

// A file of the image.
typedef struct {
    char name[16];
    uint32_t offset;        // of its record (version 2 images: of its data)
    uint32_t csize;         // compressed size (chunks included)
    uint32_t usize;         // original file size
    uint32_t seq;
    int v2;                 // stored without a record header (version 2 image)
} savefs_entry_t;

typedef struct {
    int writing;
    char name[16];          // writing: file name
    // reading
    uint32_t csize, usize;  // of the file
    uint8_t *cdata;         // compressed data of the whole file
    uint32_t cpos;          // next chunk in cdata
    uint32_t pos;           // file position of chunk[0]
    uint32_t chunk_start;   // file position of the decoded chunk
    uint32_t chunk_len;
    // writing
    uint8_t *out;           // compressed output
    uint32_t out_len, out_cap;
    uint32_t in_len;        // bytes in chunk[]
    uint32_t total;
    int overflow;           // more than the FlashRAM can hold
    int nomem;              // out of memory for out[]
    uint8_t chunk[CHUNK_SIZE];
} savefs_file_t;

// out[] grows in steps as the file is written: a save takes a fraction of
// the FlashRAM, and the heap may not have room for all of it.
#define OUT_STEP            (16 * 1024)

static savefs_entry_t files[SAVEFS_MAX_FILES];
static int nfiles = 0;
static int v2_image = 0;            // the image is still in the version 2 layout
static uint32_t next_seq = 1;
static uint32_t flash_size = 0, sector_size = 0;
static int mounted = 0;
static int last_write_error = 0;    // errno of the last write, 0 if it was saved


/*
 * ------------------------------------------------------------------------
 *  LZ compression
 *  Tokens: 0x00-0x7F literal run of (t+1) bytes that follow;
 *          0x80-0xFF match of ((t&0x7F)+3) bytes at distance d (next 2 bytes, LE).
 * ------------------------------------------------------------------------
 */

#define LZ_MIN_MATCH 3
#define LZ_MAX_MATCH (0x7F + LZ_MIN_MATCH)
#define LZ_HASH_BITS 12

#define LZ_CHAIN     32      // earlier positions with the same hash tried

static uint32_t lz_compress(const uint8_t *in, uint32_t len, uint8_t *out)
{
    static int32_t last[1 << LZ_HASH_BITS];
    static int16_t prev[CHUNK_SIZE];    // previous position with the same hash
    uint32_t i = 0, o = 0, lit_start = 0;

    memset(last, 0xff, sizeof(last));

    #define FLUSH_LITERALS(end) do {                        \
        uint32_t s = lit_start;                             \
        while (s < (end)) {                                 \
            uint32_t n = (end) - s; if (n > 128) n = 128;   \
            out[o++] = n - 1;                               \
            memcpy(out + o, in + s, n); o += n; s += n;     \
        }                                                   \
    } while (0)

    while (i + LZ_MIN_MATCH <= len)
    {
        uint32_t h = ((in[i] << 8) ^ (in[i+1] << 4) ^ in[i+2]) & ((1 << LZ_HASH_BITS) - 1);
        int32_t cand = last[h];
        uint32_t best = 0, dist = 0;
        int tries = LZ_CHAIN;

        prev[i] = cand;
        last[h] = i;

        // Run of a repeated byte: distance 1 finds long zero runs.
        if (i > 0 && in[i] == in[i-1] && in[i+1] == in[i-1] && in[i+2] == in[i-1])
        {
            uint32_t n = 0;
            while (i + n < len && n < LZ_MAX_MATCH && in[i+n] == in[i-1]) n++;
            best = n; dist = 1;
        }
        while (cand >= 0 && i - cand <= 0xFFFF && tries-- > 0 && best < LZ_MAX_MATCH)
        {
            uint32_t n = 0;
            while (i + n < len && n < LZ_MAX_MATCH && in[cand+n] == in[i+n]) n++;
            if (n > best) { best = n; dist = i - cand; }
            cand = prev[cand];
        }

        if (best >= LZ_MIN_MATCH)
        {
            FLUSH_LITERALS(i);
            out[o++] = 0x80 | (best - LZ_MIN_MATCH);
            out[o++] = dist & 0xFF;
            out[o++] = dist >> 8;
            i += best;
            lit_start = i;
        }
        else
            i++;
    }
    FLUSH_LITERALS(len);
    #undef FLUSH_LITERALS
    return o;
}

static int lz_decompress(const uint8_t *in, uint32_t len, uint8_t *out, uint32_t out_len)
{
    uint32_t i = 0, o = 0;

    while (i < len)
    {
        uint8_t t = in[i++];
        if (t < 0x80)
        {
            uint32_t n = t + 1;
            if (i + n > len || o + n > out_len) return -1;
            memcpy(out + o, in + i, n); i += n; o += n;
        }
        else
        {
            uint32_t n = (t & 0x7F) + LZ_MIN_MATCH, d;
            if (i + 2 > len) return -1;
            d = in[i] | (in[i+1] << 8); i += 2;
            if (d == 0 || d > o || o + n > out_len) return -1;
            while (n--) { out[o] = out[o - d]; o++; }   // may overlap
        }
    }
    return o == out_len ? 0 : -1;
}


/*
 * ------------------------------------------------------------------------
 *  Image
 * ------------------------------------------------------------------------
 */

static uint32_t crc32_update(uint32_t c, const uint8_t *p, uint32_t len)
{
    static uint32_t table[256];

    if (!table[1])
    {
        uint32_t i, k, t;
        for (i = 0; i < 256; i++)
        {
            for (t = i, k = 0; k < 8; k++)
                t = (t & 1) ? 0xEDB88320u ^ (t >> 1) : t >> 1;
            table[i] = t;
        }
    }
    for (; len--; p++)
        c = table[(c ^ *p) & 0xFF] ^ (c >> 8);
    return c;
}

static uint32_t crc32(const uint8_t *p, uint32_t len)
{
    return ~crc32_update(0xFFFFFFFFu, p, len);
}

static uint32_t header_crc(const savefs_record_t *r)
{
    return crc32((const uint8_t *)r, offsetof(savefs_record_t, header_crc));
}

static uint32_t entry_start(const savefs_entry_t *e) { return e->offset; }
static uint32_t entry_end(const savefs_entry_t *e) { return e->offset + (e->v2 ? 0 : sizeof(savefs_record_t)) + e->csize; }
static uint32_t entry_data(const savefs_entry_t *e) { return e->offset + (e->v2 ? 0 : sizeof(savefs_record_t)); }

static int find_entry(const char *name)
{
    int i;
    for (i = 0; i < nfiles; i++)
        if (strcmp(files[i].name, name) == 0)
            return i;
    return -1;
}

/*
 * The FlashRAM is read and written in small pieces and one sector at a
 * time: when the game saves, the heap may not have a FlashRAM-sized buffer
 * free (only the conversion of a version 2 image, done at boot, needs one).
 */
#define PIECE 512

// Copies len bytes at any offset (flashram_read() needs an even one).
static void flash_copy(uint8_t *dst, uint32_t offset, uint32_t len)
{
    static uint8_t piece[PIECE + 16] __attribute__((aligned(16)));

    while (len > 0)
    {
        uint32_t odd = offset & 1;
        uint32_t n = len < PIECE ? len : PIECE;
        flashram_read(piece, offset - odd, n + odd);
        memcpy(dst, piece + odd, n);
        dst += n; offset += n; len -= n;
    }
}

static uint32_t flash_crc32(uint32_t offset, uint32_t len)
{
    uint8_t buf[PIECE];
    uint32_t c = 0xFFFFFFFFu;

    while (len > 0)
    {
        uint32_t n = len < PIECE ? len : PIECE;
        flash_copy(buf, offset, n);
        c = crc32_update(c, buf, n);
        offset += n; len -= n;
    }
    return ~c;
}

// A version of a file found in the image: kept if it is the newest one.
static void add_version(const char *name, uint32_t offset, uint32_t csize, uint32_t usize,
                        uint32_t seq, int v2)
{
    int e = find_entry(name);

    if (e < 0)
    {
        if (nfiles == SAVEFS_MAX_FILES)
            return;
        e = nfiles++;
    }
    else if (files[e].seq >= seq)
        return;

    memset(&files[e], 0, sizeof(files[e]));
    strcpy(files[e].name, name);
    files[e].offset = offset;
    files[e].csize = csize;
    files[e].usize = usize;
    files[e].seq = seq;
    files[e].v2 = v2;
    if (seq >= next_seq)
        next_seq = seq + 1;
}

/*
 * Version 2 images: a directory at 0x000 and the files after it from 0x200,
 * one right after the other (so possibly at odd offsets), with no CRCs.
 */
#define SAVEFS_V2_MAGIC     "DK3DSAV2"
#define SAVEFS_V2_FILES     10

typedef struct {
    char name[16];
    uint32_t offset;        // in the image, 0 if unused
    uint32_t csize;
    uint32_t usize;
    uint32_t reserved;
} savefs_v2_entry_t;

typedef struct {
    char magic[8];
    uint32_t count;
    uint32_t reserved;
    savefs_v2_entry_t entry[SAVEFS_V2_FILES];
} savefs_v2_dir_t;

static void read_v2(void)
{
    savefs_v2_dir_t d;
    int i;

    flash_copy((uint8_t *)&d, 0, sizeof(d));
    for (i = 0; i < SAVEFS_V2_FILES; i++)
    {
        const savefs_v2_entry_t *e = &d.entry[i];
        if (!e->offset || !e->name[0] || memchr(e->name, 0, sizeof(e->name)) == NULL ||
            e->offset < sizeof(d) || e->offset >= flash_size || e->csize > flash_size - e->offset)
            continue;
        add_version(e->name, e->offset, e->csize, e->usize, 0, 1);
    }
    v2_image = 1;
}

// Is there a sound record at offset?
static int record_ok(uint32_t offset, savefs_record_t *r)
{
    if (offset + sizeof(*r) > flash_size)
        return 0;
    flash_copy((uint8_t *)r, offset, sizeof(*r));
    return r->magic == RECORD_MAGIC && r->header_crc == header_crc(r) &&
           r->name[0] && memchr(r->name, 0, sizeof(r->name)) != NULL &&
           r->csize <= flash_size - offset - sizeof(*r) &&
           r->data_crc == flash_crc32(offset + sizeof(*r), r->csize);
}

// Finds the files of the FlashRAM.
static void scan(void)
{
    uint8_t window[PIECE];          // to look for the magic numbers quickly
    uint32_t window_start = NO_SPACE, offset;
    char magic[8];

    nfiles = 0;
    v2_image = 0;
    next_seq = 1;

    flash_copy((uint8_t *)magic, 0, sizeof(magic));
    if (memcmp(magic, SAVEFS_V2_MAGIC, 8) == 0)
    {
        read_v2();
        return;
    }

    for (offset = 0; offset + sizeof(savefs_record_t) <= flash_size; )
    {
        savefs_record_t r;
        uint32_t m;

        if (window_start == NO_SPACE || offset < window_start || offset + 4 > window_start + PIECE)
        {
            window_start = offset;
            flash_copy(window, offset, flash_size - offset < PIECE ? flash_size - offset : PIECE);
        }
        memcpy(&m, window + (offset - window_start), 4);
        if (m == RECORD_MAGIC && record_ok(offset, &r))
        {
            add_version(r.name, offset, r.csize, r.usize, r.seq, 0);
            offset = SAVEFS_ALIGN(offset + RECORD_SIZE(r.csize));
        }
        else
            offset += 16;
    }
}

// The files but skip, in the order they are in the image. Returns how many.
static int image_order(int skip, int *order)
{
    int i, j, n = 0;

    for (i = 0; i < nfiles; i++)
    {
        if (i == skip)
            continue;
        for (j = n; j > 0 && files[order[j-1]].offset > files[i].offset; j--)
            order[j] = order[j-1];
        order[j] = i;
        n++;
    }
    return n;
}

/*
 * Free space for size bytes that leaves every file where it is, or NO_SPACE.
 * Preferably whole sectors no file uses: a power cut while a sector is
 * written loses all of it, and so the files sharing it with the new one.
 */
static uint32_t find_space(uint32_t size)
{
    int order[SAVEFS_MAX_FILES];
    int i, n = image_order(-1, order);
    uint32_t start = 0, s, run = 0;
    uint8_t busy[64] = { 0 };       // sectors holding part of a file
    uint32_t nsectors = flash_size / sector_size;

    if (nsectors <= sizeof(busy))
    {
        for (i = 0; i < n; i++)
            for (s = entry_start(&files[order[i]]) / sector_size;
                 s <= (entry_end(&files[order[i]]) - 1) / sector_size; s++)
                busy[s] = 1;
        for (s = 0; s < nsectors; s++)
        {
            run = busy[s] ? 0 : run + 1;
            if (run * sector_size >= size)
                return (s + 1 - run) * sector_size;
        }
    }

    for (i = 0; i < n; i++)
    {
        const savefs_entry_t *e = &files[order[i]];
        if (start + size <= entry_start(e))
            return start;
        if (SAVEFS_ALIGN(entry_end(e)) > start)
            start = SAVEFS_ALIGN(entry_end(e));
    }
    return start + size <= flash_size ? start : NO_SPACE;
}

static void make_record(savefs_record_t *r, const char *name, const uint8_t *data,
                        uint32_t csize, uint32_t usize, uint32_t seq)
{
    memset(r, 0, sizeof(*r));
    r->magic = RECORD_MAGIC;
    r->seq = seq;
    r->csize = csize;
    r->usize = usize;
    r->data_crc = crc32(data, csize);
    strncpy(r->name, name, sizeof(r->name) - 1);
    r->header_crc = header_crc(r);
}

// Is the sector at s the same as buf? (Read in pieces: no second sector buffer.)
static int sector_equals(const uint8_t *buf, uint32_t s)
{
    uint8_t piece[PIECE] __attribute__((aligned(16)));
    uint32_t o;

    for (o = 0; o < sector_size; o += PIECE)
    {
        flashram_read(piece, s + o, PIECE);
        if (memcmp(piece, buf + o, PIECE) != 0)
            return 0;
    }
    return 1;
}

/*
 * Programs the sector at s with buf, if it differs, and reads it back.
 * flashram_write()'s result is not trusted: it is the chip's "erase OK" /
 * "program OK" status bits, which flashcarts and emulators that emulate the
 * FlashRAM often leave unset after a write that went well.
 */
static int write_sector(const uint8_t *buf, uint32_t s)
{
    if (sector_equals(buf, s))
        return 0;
    if (flashram_write(buf, s, sector_size) < 0)
        debugf("savefs: sector at 0x%05lx: the FlashRAM reports a failed write\n", (unsigned long)s);
    if (!sector_equals(buf, s))
    {
        debugf("savefs: sector at 0x%05lx: does not read back as written\n", (unsigned long)s);
        return EIO;
    }
    return 0;
}

// Bytes to put at dst: from the FlashRAM at src, or from mem.
typedef struct {
    uint32_t dst, len, src;
    const uint8_t *mem;
} piece_t;

/*
 * Rewrites the sectors holding [from, to), first to last, each with the
 * pieces put over what it holds. A piece read from the FlashRAM must come
 * from its destination or above (packing only moves files down): the
 * sectors below have been rewritten already, the ones above not yet.
 */
static int write_pieces(const piece_t *p, int n, uint32_t from, uint32_t to)
{
    uint8_t *buf = memalign(16, sector_size);
    uint32_t s;
    int i, result = 0;

    if (!buf)
        return ENOMEM;
    for (s = from - from % sector_size; s < to && result == 0; s += sector_size)
    {
        flashram_read(buf, s, sector_size);
        for (i = 0; i < n; i++)
        {
            uint32_t a = p[i].dst > s ? p[i].dst : s;
            uint32_t b = p[i].dst + p[i].len < s + sector_size ? p[i].dst + p[i].len : s + sector_size;
            if (a >= b)
                continue;
            if (p[i].mem)
                memcpy(buf + (a - s), p[i].mem + (a - p[i].dst), b - a);
            else if (p[i].src != p[i].dst)
                flash_copy(buf + (a - s), p[i].src + (a - p[i].dst), b - a);
        }
        result = write_sector(buf, s);
    }
    free(buf);
    return result;
}

/*
 * Turns a version 2 image into records. Its files move up as well as down
 * (each gains a header): this is done in a FlashRAM-sized buffer, at boot
 * while the heap has room.
 */
static int migrate_v2(void)
{
    int order[SAVEFS_MAX_FILES];
    uint32_t newoff[SAVEFS_MAX_FILES];
    int n = image_order(-1, order);
    uint32_t end = 0, s;
    uint8_t *image;
    int i, j, pass, result = 0;

    for (j = 0; j < n; j++)
    {
        newoff[order[j]] = end;
        end = SAVEFS_ALIGN(end + RECORD_SIZE(files[order[j]].csize));
    }
    if (end > flash_size)
        return ENOSPC;
    image = memalign(16, flash_size);
    if (!image)
        return ENOMEM;
    flashram_read(image, 0, flash_size);

    // Each file's data goes to its new place: those going down first to
    // last, those going up last to first, so that none is written over one
    // that has not moved yet; then its header goes before it.
    for (pass = 0; pass < 2; pass++)
        for (j = pass ? n - 1 : 0; pass ? j >= 0 : j < n; j += pass ? -1 : 1)
        {
            const savefs_entry_t *e = &files[order[j]];
            uint32_t to = newoff[order[j]] + sizeof(savefs_record_t);
            savefs_record_t r;

            if ((to > e->offset) != (pass == 1))
                continue;
            memmove(image + to, image + e->offset, e->csize);
            make_record(&r, e->name, image + to, e->csize, e->usize, 1);
            memcpy(image + newoff[order[j]], &r, sizeof(r));
        }
    if (n == 0)
        memset(image, 0xFF, sizeof(savefs_v2_dir_t));   // no files: just the directory to go

    for (s = 0; s < flash_size && result == 0; s += sector_size)
        result = write_sector(image + s, s);
    free(image);
    if (result)
    {
        scan();
        return result;
    }
    for (i = 0; i < nfiles; i++)
    {
        files[i].offset = newoff[i];
        files[i].seq = 1;
        files[i].v2 = 0;
    }
    next_seq = 2;
    v2_image = 0;
    return 0;
}

// Replaces (or adds) name with the given compressed data. Returns 0, or
// ENOSPC (it does not fit), ENOMEM or EIO (the FlashRAM failed).
static int store_file(const char *name, const uint8_t *cdata, uint32_t csize, uint32_t usize)
{
    piece_t p[SAVEFS_MAX_FILES + 2];
    uint32_t newoff[SAVEFS_MAX_FILES];
    int order[SAVEFS_MAX_FILES];
    uint32_t need = RECORD_SIZE(csize), offset, from;
    savefs_record_t r;
    int e = find_entry(name), n = 0, packed = 0, i, j, result;

    if (e < 0 && nfiles == SAVEFS_MAX_FILES)
        return ENOSPC;
    if (v2_image && (result = migrate_v2()) != 0)
        return result;

    // Free space that leaves every file where it is, the previous version of
    // this one included; else the others packed at the start, in the same
    // order (each moves down, or stays), and the file after them.
    offset = find_space(need);
    from = offset;
    if (offset == NO_SPACE)
    {
        int m = image_order(e, order);
        uint32_t end = 0;

        for (j = 0; j < m; j++)
        {
            i = order[j];
            newoff[i] = end;
            p[n++] = (piece_t){ end, RECORD_SIZE(files[i].csize), files[i].offset, NULL };
            end = SAVEFS_ALIGN(end + RECORD_SIZE(files[i].csize));
        }
        if (end + need > flash_size)
            return ENOSPC;
        offset = end;
        from = 0;
        packed = 1;
    }

    make_record(&r, name, cdata, csize, usize, next_seq);
    p[n++] = (piece_t){ offset, sizeof(r), 0, (const uint8_t *)&r };
    p[n++] = (piece_t){ offset + sizeof(r), csize, 0, cdata };

    result = write_pieces(p, n, from, offset + need);
    if (result)
    {
        debugf("savefs: FlashRAM write failed\n");
        scan();         // whatever the chip holds now
        return result;
    }

    if (packed)
        for (i = 0; i < nfiles; i++)
            if (i != e)
                files[i].offset = newoff[i];
    if (e < 0)
        e = nfiles++;
    memset(&files[e], 0, sizeof(files[e]));
    strcpy(files[e].name, name);
    files[e].offset = offset;
    files[e].csize = csize;
    files[e].usize = usize;
    files[e].seq = next_seq++;
    return 0;
}

/*
 * ------------------------------------------------------------------------
 *  Filesystem callbacks
 * ------------------------------------------------------------------------
 */

static const char *base_name(const char *name)
{
    while (*name == '/') name++;
    return name;
}

static void *savefs_open(char *name, int flags)
{
    savefs_file_t *f;
    const char *n = base_name(name);

    if (strlen(n) >= sizeof(files[0].name)) { errno = ENAMETOOLONG; return NULL; }

    if ((flags & O_ACCMODE) != O_RDONLY)
    {
        f = calloc(1, sizeof(*f));
        if (!f) { errno = ENOMEM; return NULL; }
        f->writing = 1;
        strcpy(f->name, n);
        f->out_cap = OUT_STEP;
        f->out = malloc(f->out_cap);
        if (!f->out) { free(f); errno = ENOMEM; return NULL; }
        return f;
    }

    {
        int e = find_entry(n);
        if (e < 0) { errno = ENOENT; return NULL; }
        f = calloc(1, sizeof(*f));
        if (!f) { errno = ENOMEM; return NULL; }
        f->csize = files[e].csize;
        f->usize = files[e].usize;
        f->cdata = memalign(16, f->csize + 16);
        if (!f->cdata) { free(f); errno = ENOMEM; return NULL; }
        flash_copy(f->cdata, entry_data(&files[e]), f->csize);
        return f;
    }
}

// Decodes the next chunk of a file open for reading.
static int next_chunk(savefs_file_t *f)
{
    uint32_t csize, usize;
    const uint8_t *data = f->cdata + f->cpos + 4;

    if (f->cpos + 4 > f->csize)
        return -1;
    csize = f->cdata[f->cpos] | (f->cdata[f->cpos+1] << 8);
    usize = f->cdata[f->cpos+2] | (f->cdata[f->cpos+3] << 8);
    if (usize == 0 || usize > CHUNK_SIZE || f->cpos + 4 + csize > f->csize)
        return -1;
    if (csize == usize)
        memcpy(f->chunk, data, usize);
    else if (lz_decompress(data, csize, f->chunk, usize) < 0)
        return -1;

    f->chunk_start += f->chunk_len;
    f->chunk_len = usize;
    f->cpos += 4 + csize;
    return 0;
}

static int savefs_read(void *file, uint8_t *ptr, int len)
{
    savefs_file_t *f = file;
    int done = 0;

    if (f->writing) { errno = EBADF; return -1; }
    while (len > 0 && f->pos < f->usize)
    {
        uint32_t n;
        // (After a seek forwards, the chunks before pos are skipped.)
        while (f->pos >= f->chunk_start + f->chunk_len)
            if (next_chunk(f) < 0) { errno = EIO; return -1; }
        n = f->chunk_start + f->chunk_len - f->pos;
        if (n > (uint32_t)len) n = len;
        memcpy(ptr, f->chunk + (f->pos - f->chunk_start), n);
        ptr += n; len -= n; done += n; f->pos += n;
    }
    return done;
}

static void flush_chunk(savefs_file_t *f)
{
    static uint8_t packed[CHUNK_SIZE + CHUNK_SIZE / 64 + 16];
    uint32_t csize;
    const uint8_t *data;

    if (f->in_len == 0)
        return;
    csize = lz_compress(f->chunk, f->in_len, packed);
    data = packed;
    if (csize >= f->in_len) { csize = f->in_len; data = f->chunk; }

    if (f->out_len + 4 + csize > f->out_cap && !f->overflow && !f->nomem)
    {
        uint32_t max = flash_size - sizeof(savefs_record_t);
        uint32_t cap = f->out_cap;
        uint8_t *grown;

        while (cap < f->out_len + 4 + csize)
            cap += OUT_STEP;
        if (cap > max)
            cap = max;
        if (f->out_len + 4 + csize > cap)
            f->overflow = 1;
        else if (!(grown = realloc(f->out, cap)))
            f->nomem = 1;
        else
        {
            f->out = grown;
            f->out_cap = cap;
        }
    }
    if (!f->overflow && !f->nomem)
    {
        uint8_t *o = f->out + f->out_len;
        o[0] = csize & 0xFF; o[1] = csize >> 8;
        o[2] = f->in_len & 0xFF; o[3] = f->in_len >> 8;
        memcpy(o + 4, data, csize);
        f->out_len += 4 + csize;
    }
    f->in_len = 0;
}

static int savefs_write(void *file, uint8_t *ptr, int len)
{
    savefs_file_t *f = file;
    int done = len;

    if (!f->writing) { errno = EBADF; return -1; }
    while (len > 0)
    {
        uint32_t n = CHUNK_SIZE - f->in_len;
        if (n > (uint32_t)len) n = len;
        memcpy(f->chunk + f->in_len, ptr, n);
        f->in_len += n; f->total += n; ptr += n; len -= n;
        if (f->in_len == CHUNK_SIZE)
            flush_chunk(f);
    }
    return done;
}

static int savefs_lseek(void *file, int offset, int whence)
{
    savefs_file_t *f = file;
    uint32_t target;

    if (f->writing)
    {
        if (offset == 0 && whence != SEEK_SET) return f->total;
        errno = EINVAL; return -1;
    }

    switch (whence)
    {
        case SEEK_SET: target = offset; break;
        case SEEK_CUR: target = f->pos + offset; break;
        case SEEK_END: target = f->usize + offset; break;
        default: errno = EINVAL; return -1;
    }
    if (target > f->usize) target = f->usize;

    if (target < f->chunk_start)
    {
        // Backwards: decode again from the start.
        f->cpos = 0; f->chunk_start = 0; f->chunk_len = 0;
    }
    f->pos = target;
    return target;
}

static int savefs_fstat(void *file, struct stat *st)
{
    savefs_file_t *f = file;

    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG;
    st->st_size = f->writing ? f->total : f->usize;
    return 0;
}

static int savefs_stat(char *name, struct stat *st)
{
    int e = find_entry(base_name(name));

    if (e < 0) { errno = ENOENT; return -1; }
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG;
    st->st_size = files[e].usize;
    return 0;
}

static int savefs_close(void *file)
{
    savefs_file_t *f = file;
    int result = 0;

    if (f->writing)
    {
        flush_chunk(f);
        last_write_error = f->nomem ? ENOMEM : f->overflow ? ENOSPC :
                           store_file(f->name, f->out, f->out_len, f->total);
#ifdef N64
        if (last_write_error == ENOMEM)
        {
            heap_stats_t st;
            sys_get_heap_stats(&st);
            debugf("savefs: heap: %d KB used, %d KB free\n", st.used / 1024, st.free / 1024);
        }
#endif
        if (last_write_error)
        {
            debugf("savefs: %s could not be saved (%lu bytes compressed): %s\n", f->name,
                   (unsigned long)f->out_len, last_write_error == ENOSPC ? "no space" :
                   last_write_error == ENOMEM ? "out of memory" : "FlashRAM error");
            errno = last_write_error;
            result = -1;
        }
        else
            debugf("savefs: %s saved, %lu -> %lu bytes\n", f->name,
                   (unsigned long)f->total, (unsigned long)f->out_len);
        free(f->out);
    }
    else
        free(f->cdata);

    free(f);
    return result;
}

static filesystem_t savefs = {
    .open = savefs_open,
    .fstat = savefs_fstat,
    .stat = savefs_stat,
    .lseek = savefs_lseek,
    .read = savefs_read,
    .write = savefs_write,
    .close = savefs_close,
};


/*
 * ------------------------------------------------------------------------
 *  Public
 * ------------------------------------------------------------------------
 */

void n64_savefs_init(void)
{
    flashram_info_t info;

    if (mounted)
        return;
    mounted = 1;

    if (!flashram_init(NULL, &info))
    {
        debugf("savefs: no FlashRAM, saving disabled\n");
        return;
    }
    flash_size = info.total_size;
    sector_size = info.sector_size;

    scan();
    // (Now, while the heap has room for the FlashRAM-sized buffer it needs.)
    if (v2_image)
    {
        int result = migrate_v2();
        debugf("savefs: version 2 image converted: %s\n", result == 0 ? "ok" : "failed");
        (void)result;
    }

    attach_filesystem("save:/", &savefs);
    debugf("savefs: %s, %lu KB, %d files%s\n", info.name,
           (unsigned long)(flash_size / 1024), nfiles, v2_image ? " (version 2 image)" : "");
}

int n64_savefs_error(void)
{
    return last_write_error;
}
