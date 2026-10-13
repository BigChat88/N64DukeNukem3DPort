/*
 * The few libdragon pieces src/n64/n64_savefs.c uses, for building it on the
 * host (tests/host). The FlashRAM is an array with the real driver's rules:
 * reads need an even offset, ranges are checked, writes can be made to fail.
 */
#ifndef TEST_LIBDRAGON_H
#define TEST_LIBDRAGON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MIXER_LOOP_OVERREAD 64
#include <stdlib.h>
#include <fcntl.h>

#ifndef O_ACCMODE
#define O_ACCMODE (O_RDONLY | O_WRONLY | O_RDWR)
#endif
#ifndef ENAMETOOLONG
#define ENAMETOOLONG 91
#endif

#define debugf(...) ((void)0)

#include <string.h>

/*
 * The heap, counted: test_heap_used / test_heap_peak are the bytes the code
 * under test has allocated now / at most (memalign is newlib's, not
 * available everywhere on the host). A realloc counts the old and the new
 * block, as newlib may need both at once.
 */
static size_t test_heap_used = 0, test_heap_peak = 0;

static inline void *test_malloc(size_t n)
{
    size_t *p = (malloc)(n + 16);
    if (!p)
        return NULL;
    p[0] = n;
    test_heap_used += n;
    if (test_heap_used > test_heap_peak)
        test_heap_peak = test_heap_used;
    return (uint8_t *)p + 16;
}

static inline void test_free(void *ptr)
{
    if (ptr)
    {
        size_t *p = (size_t *)((uint8_t *)ptr - 16);
        test_heap_used -= p[0];
        (free)(p);
    }
}

static inline void *test_calloc(size_t n, size_t size)
{
    void *p = test_malloc(n * size);
    if (p)
        memset(p, 0, n * size);
    return p;
}

static inline void *test_realloc(void *ptr, size_t n)
{
    void *p = test_malloc(n);
    if (p && ptr)
    {
        size_t old = ((size_t *)((uint8_t *)ptr - 16))[0];
        memcpy(p, ptr, old < n ? old : n);
        test_free(ptr);
    }
    return p;
}

#define malloc(n)               test_malloc(n)
#define calloc(n, size)         test_calloc(n, size)
#define realloc(ptr, n)         test_realloc(ptr, n)
#define free(ptr)               test_free(ptr)
#define memalign(align, size)   test_malloc(size)

typedef struct {
    const char *name;
    size_t total_size;
    size_t sector_size;
} flashram_info_t;

bool flashram_init(const void *timings, flashram_info_t *info);
int flashram_read(void *dst, size_t offset, size_t len);
int flashram_write(const void *src, size_t offset, size_t len);

struct stat;
typedef struct {
    void *(*open)(char *name, int flags);
    int (*fstat)(void *file, struct stat *st);
    int (*stat)(char *name, struct stat *st);
    int (*lseek)(void *file, int offset, int whence);
    int (*read)(void *file, uint8_t *ptr, int len);
    int (*write)(void *file, uint8_t *ptr, int len);
    int (*close)(void *file);
} filesystem_t;

int attach_filesystem(const char *prefix, filesystem_t *fs);

/*
 * Audio (src/n64/n64_fx.c): only the fields and calls it uses.
 */
#define MIXER_MAX_CHANNELS          32
#define SAMPLEBUFFER_MARGIN_UNITS   128

typedef struct samplebuffer_s samplebuffer_t;
typedef uint32_t pi_addr_t;
typedef void (*WaveformRead)(void *ctx, samplebuffer_t *sbuf, int wpos, int wlen, bool seeking);

typedef struct {
    const char *name;
    uint8_t bits;
    uint8_t channels;
    float frequency;
    int len;
    int loop_len;
    WaveformRead read;
    void *ctx;
    const void *mem;
} waveform_t;

void *samplebuffer_append(samplebuffer_t *buf, int wlen);
void dma_read(void *ram_address, pi_addr_t pi_address, unsigned long len);
void *malloc_uncached(size_t size);
void data_cache_hit_writeback(const void *addr, unsigned long length);
void audio_init(const int frequency, int numbuffers);
void audio_close(void);
#define AUDIO_DEFAULT_LATENCY 0
void mixer_init(int num_channels);
void mixer_close(void);
void mixer_set_vol(float vol);
void mixer_try_play(void);
void mixer_ch_set_limits(int ch, int max_bits, float max_frequency, int max_buf_sz);
void mixer_ch_play(int ch, waveform_t *wave);
void mixer_ch_stop(int ch);
bool mixer_ch_playing(int ch);
void mixer_ch_set_vol(int ch, float lvol, float rvol);
void mixer_ch_set_freq(int ch, float frequency);
void mixer_ch_set_loop(int ch, bool loop);

#define TICKS_READ()                0u
#define TICKS_DISTANCE(from, to)    ((int32_t)((to) - (from)))
#define TICKS_FROM_MS(ms)           ((ms) * 1000u)

#endif
