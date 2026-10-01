/*
 * "save:/" filesystem: the game's save files, compressed, in the cartridge
 * FlashRAM (128 KB).
 *
 * The game keeps writing and reading its save files with the C library
 * (fopen/open, fwrite/read); this layer stores them:
 *
 *   FlashRAM image
 *     0x000  directory: magic + up to SAVEFS_MAX_FILES entries
 *              (name, offset, compressed size, uncompressed size)
 *     0x200  compressed data of every file, one after another
 *
 *   Compressed data: a sequence of chunks, each holding up to CHUNK_SIZE
 *   bytes of the file: [uint16 stored size][uint16 original size][data].
 *   When stored size == original size the chunk is uncompressed, otherwise
 *   it is LZ compressed (see lz_compress). Chunks keep the memory needed
 *   small: a save is ~850 KB uncompressed, more than the free RAM.
 *
 * Writing a file (fclose) rebuilds the image in RAM with the new version of
 * the file and programs the FlashRAM. If everything does not fit, the write
 * fails (close returns -1 and n64_savefs_full() reports it) and the previous
 * contents are kept.
 */
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <libdragon.h>
#include <system.h>

#include "n64_platform.h"

#define SAVEFS_MAGIC        "DK3DSAV2"   // (2: the compact saves; older images start empty)
#define SAVEFS_MAX_FILES    10
#define SAVEFS_DATA_START   0x200
#define CHUNK_SIZE          16384

typedef struct {
    char name[16];
    uint32_t offset;        // in the image, 0 if unused
    uint32_t csize;         // compressed size (chunks included)
    uint32_t usize;         // original file size
    uint32_t reserved;
} savefs_entry_t;

typedef struct {
    char magic[8];
    uint32_t count;
    uint32_t reserved;
    savefs_entry_t entry[SAVEFS_MAX_FILES];
} savefs_dir_t;

typedef struct {
    int writing;
    int entry;              // reading: directory entry
    char name[16];          // writing: file name
    // reading
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
    int overflow;
    uint8_t chunk[CHUNK_SIZE];
} savefs_file_t;

static savefs_dir_t dir;
static uint32_t flash_size = 0;
static int mounted = 0;
static int last_write_full = 0;


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

static int find_entry(const char *name)
{
    int i;
    for (i = 0; i < SAVEFS_MAX_FILES; i++)
        if (dir.entry[i].offset && strcmp(dir.entry[i].name, name) == 0)
            return i;
    return -1;
}

// Replaces (or adds) name with the given compressed data and writes the
// whole image. Returns 0 or -1 when it does not fit.
static int store_file(const char *name, const uint8_t *cdata, uint32_t csize, uint32_t usize)
{
    savefs_dir_t newdir;
    uint8_t *image;
    uint32_t end = SAVEFS_DATA_START;
    int i, slot = -1;

    image = memalign(16, flash_size);
    if (!image)
        return -1;
    flashram_read(image, 0, flash_size);

    memset(&newdir, 0, sizeof(newdir));
    memcpy(newdir.magic, SAVEFS_MAGIC, 8);

    // Pack the files we keep at the start of the data area, in order.
    for (i = 0; i < SAVEFS_MAX_FILES; i++)
    {
        savefs_entry_t e = dir.entry[i];
        if (!e.offset)
            continue;
        if (strcmp(e.name, name) == 0) { slot = i; continue; }
        memmove(image + end, image + e.offset, e.csize);
        e.offset = end;
        end += e.csize;
        newdir.entry[i] = e;
    }
    if (slot < 0)
        for (i = 0; i < SAVEFS_MAX_FILES; i++)
            if (!newdir.entry[i].offset && !dir.entry[i].offset) { slot = i; break; }

    if (slot < 0 || end + csize > flash_size)
    {
        free(image);
        return -1;
    }

    memcpy(image + end, cdata, csize);
    strncpy(newdir.entry[slot].name, name, sizeof(newdir.entry[slot].name) - 1);
    newdir.entry[slot].offset = end;
    newdir.entry[slot].csize = csize;
    newdir.entry[slot].usize = usize;
    end += csize;
    for (i = 0; i < SAVEFS_MAX_FILES; i++)
        if (newdir.entry[i].offset)
            newdir.count++;

    memcpy(image, &newdir, sizeof(newdir));
    flashram_write(image, 0, end);
    free(image);

    dir = newdir;
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

    if (strlen(n) >= sizeof(dir.entry[0].name)) { errno = ENAMETOOLONG; return NULL; }

    if ((flags & O_ACCMODE) != O_RDONLY)
    {
        f = calloc(1, sizeof(*f));
        if (!f) { errno = ENOMEM; return NULL; }
        f->writing = 1;
        strcpy(f->name, n);
        f->out_cap = flash_size - SAVEFS_DATA_START;
        f->out = malloc(f->out_cap);
        if (!f->out) { free(f); errno = ENOMEM; return NULL; }
        return f;
    }

    {
        int e = find_entry(n);
        if (e < 0) { errno = ENOENT; return NULL; }
        f = calloc(1, sizeof(*f));
        if (!f) { errno = ENOMEM; return NULL; }
        f->entry = e;
        f->cdata = memalign(16, dir.entry[e].csize + 16);
        if (!f->cdata) { free(f); errno = ENOMEM; return NULL; }
        flashram_read(f->cdata, dir.entry[e].offset, dir.entry[e].csize);
        return f;
    }
}

// Decodes the next chunk of a file open for reading.
static int next_chunk(savefs_file_t *f)
{
    uint32_t csize = f->cdata[f->cpos] | (f->cdata[f->cpos+1] << 8);
    uint32_t usize = f->cdata[f->cpos+2] | (f->cdata[f->cpos+3] << 8);
    const uint8_t *data = f->cdata + f->cpos + 4;

    if (usize == 0 || usize > CHUNK_SIZE || f->cpos + 4 + csize > dir.entry[f->entry].csize)
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
    uint32_t size = dir.entry[f->entry].usize;
    int done = 0;

    while (len > 0 && f->pos < size)
    {
        uint32_t n;
        if (f->pos >= f->chunk_start + f->chunk_len)
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

    if (f->out_len + 4 + csize > f->out_cap)
        f->overflow = 1;
    else
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
        case SEEK_END: target = dir.entry[f->entry].usize + offset; break;
        default: errno = EINVAL; return -1;
    }
    if (target > dir.entry[f->entry].usize) target = dir.entry[f->entry].usize;

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
    st->st_size = f->writing ? f->total : dir.entry[f->entry].usize;
    return 0;
}

static int savefs_stat(char *name, struct stat *st)
{
    int e = find_entry(base_name(name));

    if (e < 0) { errno = ENOENT; return -1; }
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG;
    st->st_size = dir.entry[e].usize;
    return 0;
}

static int savefs_close(void *file)
{
    savefs_file_t *f = file;
    int result = 0;

    if (f->writing)
    {
        flush_chunk(f);
        last_write_full = f->overflow || store_file(f->name, f->out, f->out_len, f->total) < 0;
        if (last_write_full)
        {
            debugf("savefs: %s does not fit (%lu bytes compressed)\n", f->name, (unsigned long)f->out_len);
            errno = ENOSPC;
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

    flashram_read(&dir, 0, sizeof(dir));
    if (memcmp(dir.magic, SAVEFS_MAGIC, 8) != 0)
        memset(&dir, 0, sizeof(dir));   // blank or foreign data: start empty

    attach_filesystem("save:/", &savefs);
    debugf("savefs: %s, %lu KB, %lu files\n", info.name,
           (unsigned long)(flash_size / 1024), (unsigned long)dir.count);
}

int n64_savefs_full(void)
{
    return last_write_full;
}
