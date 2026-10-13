/*
 * Host tests of the sound effect file parsers and sample conversion
 * (src/n64/n64_fx.c): VOC and WAV files in memory and streamed from a
 * simulated ROM.
 *
 *   make -C tests/host
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

// pitch.h pulls in the whole game (duke3d.h): only PITCH_GetScale is used.
#define __PITCH_H
#include <stdint.h>
uint32_t PITCH_GetScale(int pitchoffset) { return 1 << 16; }

#include "../../src/n64/n64_fx.c"

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

/*
 * ------------------------------------------------------------------------
 *  Simulated hardware
 * ------------------------------------------------------------------------
 */

#define ROM_BASE 0x10000000u
static uint8_t rom[65536];

void dma_read(void *ram, pi_addr_t pi, unsigned long len)
{
    // PI DMA: the RAM and ROM addresses must have the same parity, and
    // n64_fx.c reads into a buffer at an even address.
    if (pi & 1)
        fail("dma_read from an odd ROM address 0x%X", (unsigned)pi);
    if (pi < ROM_BASE || pi - ROM_BASE + len > sizeof(rom))
        fail("dma_read outside the ROM: 0x%X + %lu", (unsigned)pi, len);
    memcpy(ram, rom + (pi - ROM_BASE), len);
}

void *malloc_uncached(size_t size) { return calloc(1, size); }
void data_cache_hit_writeback(const void *addr, unsigned long length) {}
void audio_init(const int frequency, int numbuffers) {}
void audio_close(void) {}
void mixer_init(int num_channels) {}
void mixer_close(void) {}
void mixer_set_vol(float vol) {}
void mixer_try_play(void) {}
void mixer_ch_set_limits(int ch, int max_bits, float max_frequency, int max_buf_sz) {}
void mixer_ch_play(int ch, waveform_t *wave) {}
void mixer_ch_stop(int ch) {}
bool mixer_ch_playing(int ch) { return true; }
void mixer_ch_set_vol(int ch, float lvol, float rvol) {}
void mixer_ch_set_freq(int ch, float frequency) {}
void mixer_ch_set_loop(int ch, bool loop) {}

// The channel's sample buffer: fx_read appends into it.
struct samplebuffer_s { uint8_t data[1 << 16]; int bytes; int bps; };
static struct samplebuffer_s sbuf;

void *samplebuffer_append(samplebuffer_t *buf, int wlen)
{
    void *p = buf->data + buf->bytes;
    buf->bytes += wlen * buf->bps;
    if (buf->bytes > (int)sizeof(buf->data))
        fail("sample buffer overflow");
    return p;
}

/*
 * ------------------------------------------------------------------------
 *  Test files
 * ------------------------------------------------------------------------
 */

static void le16(uint8_t *p, unsigned v) { p[0] = v; p[1] = v >> 8; }
static void le24(uint8_t *p, unsigned v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; }
static void le32(uint8_t *p, unsigned v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

// RIFF WAVE, mono PCM. riff_size / data_size: 0 to write the right ones.
static int make_wav(uint8_t *f, int bits, int rate, const uint8_t *pcm, int bytes,
                    uint32_t riff_size, uint32_t data_size)
{
    memcpy(f, "RIFF", 4);
    le32(f + 4, riff_size ? riff_size : 36 + bytes);
    memcpy(f + 8, "WAVEfmt ", 8);
    le32(f + 16, 16);
    le16(f + 20, 1);                // PCM
    le16(f + 22, 1);                // mono
    le32(f + 24, rate);
    le32(f + 28, rate * bits / 8);
    le16(f + 32, bits / 8);
    le16(f + 34, bits);
    memcpy(f + 36, "data", 4);
    le32(f + 40, data_size ? data_size : (uint32_t)bytes);
    memcpy(f + 44, pcm, bytes);
    return 44 + bytes;
}

// Creative Voice File: an 8-bit sound data block split in two (type 1 then
// type 2), then the terminator.
static int make_voc(uint8_t *f, const uint8_t *pcm, int bytes, int split)
{
    int o = 26;

    memset(f, 0, 26);
    memcpy(f, "Creative Voice File\x1A", 20);
    le16(f + 0x14, 26);
    f[o] = 1; le24(f + o + 1, split + 2); f[o + 4] = 256 - 1000000 / 11025; f[o + 5] = 0;
    memcpy(f + o + 6, pcm, split);
    o += 6 + split;
    f[o] = 2; le24(f + o + 1, bytes - split);
    memcpy(f + o + 4, pcm + split, bytes - split);
    o += 4 + bytes - split;
    f[o++] = 0;
    return o;
}

static void make_pcm(uint8_t *pcm, int bytes, int seed)
{
    int i;
    for (i = 0; i < bytes; i++)
        pcm[i] = (uint8_t)(i * 7 + seed * 13 + (i >> 3));
}

// The samples fx_read hands the mixer for the whole sound.
static void decode(fx_voice_t *v, int bps)
{
    int len = total_length(v), pos = 0;

    memset(&sbuf, 0, sizeof(sbuf));
    sbuf.bps = bps;
    while (pos < len)       // in pieces, as the mixer asks for them
    {
        int n = len - pos < 300 ? len - pos : 300;
        fx_read(v, &sbuf, pos, n, false);
        pos += n;
    }
}

static void check_8bit(fx_voice_t *v, const uint8_t *pcm, int bytes)
{
    int i;

    if (v->bits != 8 || total_length(v) != bytes)
        fail("8-bit sound of %d samples parsed as %d bits, %d samples", bytes, v->bits, total_length(v));
    decode(v, 1);
    for (i = 0; i < bytes; i++)
    {
        uint8_t want = (uint8_t)((int8_t)(pcm[i] ^ 0x80) * FX_PEAK >> 7);    // (scaled, see FX_PEAK)
        if (sbuf.data[i] != want)
            fail("sample %d: %02X, expected %02X", i, sbuf.data[i], want);
    }
}

static void check_16bit(fx_voice_t *v, const uint8_t *pcm, int bytes)
{
    int i;

    if (v->bits != 16 || total_length(v) != bytes / 2)
        fail("16-bit sound of %d samples parsed as %d bits, %d samples", bytes / 2, v->bits, total_length(v));
    decode(v, 2);
    for (i = 0; i < bytes / 2; i++)
    {
        int16_t got, want = (int16_t)((int16_t)(pcm[i*2] | (pcm[i*2+1] << 8)) * FX_PEAK >> 7);
        memcpy(&got, sbuf.data + i * 2, 2);
        if (got != want)
            fail("sample %d: %d, expected %d", i, got, want);
    }
}

// The file copied to an odd ROM address, as files inside a GRP can be. (A
// new address for each: the game streams one file per address.)
static const uint8_t *on_rom(const uint8_t *file, int size)
{
    static uint32_t next = 1;
    uint32_t at = next;

    next = (next + size + 64) | 1;
    if (at + size > sizeof(rom))
        fail("test ROM full");
    memcpy(rom + at, file, size);
    return n64_fx_stream_sound(ROM_BASE + at, size);
}

/*
 * ------------------------------------------------------------------------
 *  Tests
 * ------------------------------------------------------------------------
 */

static void test_wav(void)
{
    static uint8_t pcm[3001], file[4096];
    fx_voice_t v;
    int size;

    make_pcm(pcm, sizeof(pcm), 1);
    size = make_wav(file, 8, 11025, pcm, 3001, 0, 0);
    if (!parse_wav(&v, file) || v.rate != 11025)
        fail("8-bit WAV not parsed");
    check_8bit(&v, pcm, 3001);

    size = make_wav(file, 16, 22050, pcm, 3000, 0, 0);
    if (!parse_wav(&v, file) || v.rate != 22050)
        fail("16-bit WAV not parsed");
    check_16bit(&v, pcm, 3000);

    if (!parse_wav(&v, on_rom(file, size)))
        fail("16-bit WAV from ROM not parsed");
    check_16bit(&v, pcm, 3000);
}

// A data chunk bigger than the file (left unset by the writer) stops at
// the end of the RIFF, or of the file for a sound streamed from ROM.
static void test_wav_bad_data_size(void)
{
    static uint8_t pcm[2000], file[4096];
    fx_voice_t v;
    int size;

    make_pcm(pcm, sizeof(pcm), 2);
    make_wav(file, 8, 11025, pcm, 2000, 0, 0xFFFFFFFFu);
    if (!parse_wav(&v, file))
        fail("not parsed");
    check_8bit(&v, pcm, 2000);

    // From ROM the RIFF size may lie as well: the file's size is known.
    size = make_wav(file, 8, 11025, pcm, 2000, 0x7FFFFFFF, 0xFFFFFFFFu);
    if (!parse_wav(&v, on_rom(file, size)))
        fail("not parsed from ROM");
    check_8bit(&v, pcm, 2000);
}

static void test_voc(void)
{
    static uint8_t pcm[5000], file[8192];
    fx_voice_t v;
    int size;

    make_pcm(pcm, sizeof(pcm), 3);
    size = make_voc(file, pcm, 5000, 1777);
    if (!parse_voc(&v, file) || v.numseg != 2 || v.rate < 10900 || v.rate > 11200)
        fail("VOC not parsed (segments %d, rate %d)", v.numseg, v.rate);
    check_8bit(&v, pcm, 5000);

    if (!parse_voc(&v, on_rom(file, size)))
        fail("VOC from ROM not parsed");
    check_8bit(&v, pcm, 5000);
}

// A VOC file cut short (its last block runs past the end): from ROM, only
// what there is of it plays.
static void test_voc_truncated(void)
{
    static uint8_t pcm[5000], file[8192];
    fx_voice_t v;
    int size;

    make_pcm(pcm, sizeof(pcm), 4);
    size = make_voc(file, pcm, 5000, 1000);
    size -= 1 + 500;                // no terminator, and 500 samples missing
    if (!parse_voc(&v, on_rom(file, size)))
        fail("truncated VOC from ROM not parsed");
    check_8bit(&v, pcm, 4500);
}

/*
 * Sounds in memory are converted once, in place, to the mixer's format
 * (signed 8-bit, big endian 16-bit) at a 16 byte aligned offset, scaled
 * under the resampler's limit with the channel volume making up for it, with
 * what the RSP reads past the end filled in, the end of one that does not
 * loop faded:
 * played again, they are found converted.
 */
static void test_resident(void)
{
    // The file 8 bytes past a 16 byte boundary, as the game's cache leaves it:
    // the samples must be aligned in memory, not just within the file.
    static uint8_t pcm[3001], store[8192 + 16] __attribute__((aligned(16))), copy[8192];
    uint8_t *file = store + 8;
    fx_voice_t v;
    int i, k;

    // 8-bit VOC (samples at offset 32), 8-bit WAV (44), 16-bit WAV (44); the
    // last case plays looped.
    for (k = 0; k < 4; k++)
    {
        int bits = k == 2 ? 16 : 8, bytes = k == 2 ? 3000 : 3001, looped = k == 3;
        int len = bytes / (bits / 8), peak = 0, limit = bits == 8 ? FX_PEAK : FX_PEAK << 8;
        int32_t from;

        make_pcm(pcm, sizeof(pcm), 10 + k);
        if (k == 0)
        {
            // A VOC with one sound data block (make_voc splits it in two).
            memset(file, 0, 8192);
            memcpy(file, "Creative Voice File", 20);
            le16(file + 0x14, 26);
            file[26] = 1; le24(file + 27, bytes + 2);
            file[30] = 256 - 1000000 / 11025; file[31] = 0;
            memcpy(file + 32, pcm, bytes);
            from = 32;
        }
        else
        {
            make_wav(file, bits, 11025, pcm, bytes, 0, 0);
            from = 44;
        }
        for (i = 0; i < len; i++)
        {
            int s = bits == 8 ? (int8_t)(pcm[i] ^ 0x80) : (int16_t)(pcm[i*2] | (pcm[i*2+1] << 8));
            if (abs(s) > peak) peak = abs(s);
        }

        if (!parse_sound(&v, file, k == 0, looped) || !v.resident)
            fail("case %d: not made resident", k);
        if ((uintptr_t)v.seg[0].data & 15)
            fail("case %d: samples not 16 byte aligned in memory", k);
        if (v.bits != bits)
            fail("case %d: %d bits", k, v.bits);
        {
            // It ends MIXER_LOOP_OVERREAD bytes before the last cache line of
            // its own, and those bytes hold what the RSP may play past the
            // end: silence, or the start again for a loop.
            uintptr_t own = ((uintptr_t)file + from + len * (bits / 8)) & ~(uintptr_t)15;
            const uint8_t *end = v.seg[0].data + total_length(&v) * (bits / 8);
            if ((uintptr_t)end + MIXER_LOOP_OVERREAD != own)
                fail("case %d: %d samples of %d kept (loops: %d)", k, total_length(&v), len, looped);
            for (i = 0; i < MIXER_LOOP_OVERREAD; i++)
                if (end[i] != (looped ? v.seg[0].data[i] : 0))
                    fail("case %d: byte %d past the end is %d", k, i, end[i]);
        }
        if (peak > limit ? fabsf(v.gain - (float)peak / limit) > 0.01f : v.gain != 1.0f)
            fail("case %d: gain %f for a peak of %d", k, v.gain, peak);
        for (i = 0; i < total_length(&v) - (looped ? 0 : FX_TAIL_FADE); i++)
        {
            int s = bits == 8 ? (int8_t)(pcm[i] ^ 0x80) : (int16_t)(pcm[i*2] | (pcm[i*2+1] << 8));
            int want = peak > limit ? s * limit / peak : s;
            int got = bits == 8 ? (int8_t)v.seg[0].data[i] : (int16_t)((v.seg[0].data[i*2] << 8) | v.seg[0].data[i*2+1]);
            if (got != want)
                fail("case %d: sample %d is %d, expected %d", k, i, got, want);
        }
        if (!looped && v.seg[0].data[(total_length(&v) - 1) * (bits / 8)] != 0)
            fail("case %d: the end is not faded out", k);
        // Played again: the descriptor is found, nothing converted twice.
        memcpy(copy, file, sizeof(copy));
        if (!parse_sound(&v, file, k != 0, looped) || !v.resident || memcmp(copy, file, sizeof(copy)) != 0)
            fail("case %d: second play changed the file", k);
    }
}

// A VOC whose samples are in several blocks stays on the fx_read path.
static void test_not_resident(void)
{
    static uint8_t pcm[5000], file[8192];
    fx_voice_t v;

    make_pcm(pcm, sizeof(pcm), 20);
    make_voc(file, pcm, 5000, 1777);
    if (!parse_sound(&v, file, 1, 0) || v.resident)
        fail("a two-block VOC was made resident");
    check_8bit(&v, pcm, 5000);
    if (!parse_sound(&v, (uint8_t *)on_rom(file, 6000), 1, 0) || v.resident)
        fail("a sound streamed from ROM was made resident");
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
    fx_bounce = malloc_uncached(FX_BOUNCE_SIZE);

    run("wav", test_wav);
    run("wav_bad_data_size", test_wav_bad_data_size);
    run("voc", test_voc);
    run("voc_truncated", test_voc_truncated);
    run("resident", test_resident);
    run("not_resident", test_not_resident);

    printf("%s: %d failed\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
