/*
 * FX_* sound effects API (game/audiolib/fx_man.h) on top of libdragon's mixer.
 *
 * The DOS/SDL port mixes every voice on the CPU (MULTIVOC). On the N64 each
 * voice is a libdragon mixer channel and the mixing runs on the RSP: the CPU
 * only converts the samples it hands over (unsigned 8-bit / little endian
 * 16-bit to signed native samples) and keeps the bookkeeping.
 *
 * Behavior kept from MULTIVOC (multivoc.c):
 *  - volumes: MIX_VOLUME() levels (0-63) scaled by the global FX volume;
 *  - 3D sounds: the same angle/distance pan table (MV_CalcPanTable);
 *  - pitch: PITCH_GetScale() applied to the sample rate;
 *  - when all voices are busy, the lowest priority one is stolen if the new
 *    sound has an equal or higher priority (MV_AllocVoice);
 *  - the callback runs with the sound's callbackval whenever a voice ends,
 *    is stopped or is stolen (MV_Kill).
 * Looped sounds repeat the whole sample. Reverb is not implemented.
 */
#include <string.h>
#include <libdragon.h>

#include "audiolib/fx_man.h"
#include "audiolib/pitch.h"
#include "n64_platform.h"
#include "n64_prof.h"

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

#define FX_MAX_VOICES      32
#define FX_MAX_SEGMENTS    8     // sound data blocks of one VOC file
#define FX_MAX_LEVEL       63    // MV_MaxVolume
#define FX_NUM_PAN         32    // MV_NumPanPositions
#define FX_MAX_PAN         (FX_NUM_PAN - 1)
#define FX_MIX_VOLUME(v)   ((max(0, min((v), 255)) * (FX_MAX_LEVEL + 1)) >> 8)

typedef struct {
    const uint8_t *data;        // in memory, or
    uint32_t rom;               // PI address of a sound streamed from ROM (data NULL)
    int len;                    // in samples
} fx_segment_t;

/*
 * Sounds too big for BUILD's cache (Caribbean has ambient music loops of
 * 600 KB to 3 MB, the cache is 2 MB) are played straight from ROM: the game
 * gets a small descriptor instead of the loaded file (n64_fx_stream_sound)
 * and the samples are read by PI DMA as they play.
 */
#define FX_STREAM_MAGIC    "N64 FX STREAM"
#define FX_MAX_STREAMS     128
typedef struct {
    char magic[16];
    uint32_t rom;               // PI address of the file
    int32_t size;
} fx_stream_t;

static fx_stream_t fx_streams[FX_MAX_STREAMS];
static int fx_nstreams = 0;
static uint8_t *fx_bounce = NULL;   // uncached: PI DMA target, see rom_read
#define FX_BOUNCE_SIZE     512

// Where the parsers read a sound file from.
typedef struct {
    const uint8_t *mem;         // the file in memory, or
    uint32_t rom;               // its PI address
    int32_t size;               // (0: unknown, for files in memory)
} fx_src_t;

typedef struct {
    waveform_t wave;            // must stay first: the read callback gets it back
    fx_segment_t seg[FX_MAX_SEGMENTS];
    int numseg;
    int bits;                   // 8 (unsigned) or 16 (little endian signed)
    int rate;                   // sample rate of the data, in Hz
    int handle;                 // 0 when the voice is free
    int priority;
    uint32_t callbackval;
} fx_voice_t;

static fx_voice_t voices[FX_MAX_VOICES];
static int num_voices;
static int next_handle = 0;
static int fx_installed = 0;
static int total_volume = 255;  // FX_SetVolume, 0-255
static int reverse_stereo = 0;
static void (*fx_callback)(int32_t) = NULL;
static uint8_t pan_table[FX_NUM_PAN][FX_MAX_LEVEL + 1][2];  // [angle][distance][left,right]

#ifdef N64_PROFILE
static int sounds_started = 0;
#endif

static inline unsigned read_le16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static inline unsigned read_le24(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16); }
static inline unsigned read_le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }


/*
 * ------------------------------------------------------------------------
 *  Sample decoding
 * ------------------------------------------------------------------------
 */

// Copies n bytes from ROM. PI DMA needs the RAM and ROM addresses to have the
// same parity, and files in a GRP start anywhere: it goes through a bounce
// buffer.
static void rom_read(void *dst, uint32_t pi, int n)
{
    uint8_t *out = dst;

    while (n > 0)
    {
        int odd = pi & 1;
        int chunk = min(n, FX_BOUNCE_SIZE - 2);
        dma_read(fx_bounce, pi - odd, (chunk + odd + 1) & ~1);
        memcpy(out, fx_bounce + odd, chunk);
        out += chunk;
        pi += chunk;
        n -= chunk;
    }
}

static void src_read(const fx_src_t *src, uint32_t off, void *buf, int n)
{
    if (src->mem)
        memcpy(buf, src->mem + off, n);
    else
        rom_read(buf, src->rom + off, n);
}

static fx_segment_t src_segment(const fx_src_t *src, uint32_t off, int len)
{
    if (src->mem)
        return (fx_segment_t){ src->mem + off, 0, len };
    return (fx_segment_t){ NULL, src->rom + off, len };
}

// A pointer given by the game: a loaded sound file or a streamed one.
static fx_src_t sound_src(const uint8_t *ptr)
{
    const fx_stream_t *st = (const fx_stream_t *)ptr;

    if (memcmp(st->magic, FX_STREAM_MAGIC, sizeof(FX_STREAM_MAGIC)) == 0)
        return (fx_src_t){ NULL, st->rom, st->size };
    return (fx_src_t){ ptr, 0, 0 };
}

const uint8_t *n64_fx_stream_sound(uint32_t rom, int32_t size)
{
    int i;

    for (i = 0; i < fx_nstreams; i++)
        if (fx_streams[i].rom == rom)
            return (const uint8_t *)&fx_streams[i];
    if (fx_nstreams == FX_MAX_STREAMS)
        return NULL;
    memcpy(fx_streams[i].magic, FX_STREAM_MAGIC, sizeof(FX_STREAM_MAGIC));
    fx_streams[i].rom = rom;
    fx_streams[i].size = size;
    fx_nstreams++;
    return (const uint8_t *)&fx_streams[i];
}

// Mixer callback: copies wlen samples starting at wpos into the channel's
// sample buffer, converted to signed native samples.
static void fx_read(void *ctx, samplebuffer_t *sbuf, int wpos, int wlen, bool seeking)
{
    fx_voice_t *v = (fx_voice_t *)ctx;
    int s = 0, base = 0;

    (void)seeking;

    while (wlen > 0)
    {
        int n = min(wlen, SAMPLEBUFFER_MARGIN_UNITS);
        uint8_t *out8 = samplebuffer_append(sbuf, n);
        int16_t *out16 = (int16_t *)out8;
        int i = 0;

        while (i < n)
        {
            const fx_segment_t *seg;
            int pos, run, k;

            // The data block that holds sample wpos (wpos only grows within
            // one call; loops come as a new call from position 0).
            while (s < v->numseg && wpos >= base + v->seg[s].len)
                base += v->seg[s++].len;
            if (s >= v->numseg)
            {
                for (; i < n; i++)
                    if (v->bits == 8) out8[i] = 0; else out16[i] = 0;
                break;
            }
            seg = &v->seg[s];
            pos = wpos - base;
            run = min(n - i, seg->len - pos);

            if (v->bits == 8)
            {
                if (seg->data)
                    for (k = 0; k < run; k++)
                        out8[i + k] = seg->data[pos + k] ^ 0x80;
                else
                {
                    rom_read(out8 + i, seg->rom + pos, run);
                    for (k = 0; k < run; k++)
                        out8[i + k] ^= 0x80;
                }
            }
            else
            {
                if (seg->data)
                    for (k = 0; k < run; k++)
                        out16[i + k] = (int16_t)read_le16(seg->data + (pos + k) * 2);
                else
                {
                    rom_read(out16 + i, seg->rom + pos * 2, run * 2);
                    for (k = 0; k < run; k++)
                        out16[i + k] = (int16_t)read_le16((const uint8_t *)&out16[i + k]);
                }
            }
            i += run;
            wpos += run;
        }
        wlen -= n;
    }
}

// Splits a Creative Voice File into its sound data blocks.
static int parse_voc(fx_voice_t *v, const uint8_t *ptr)
{
    fx_src_t src = sound_src(ptr);
    uint8_t h[32];
    uint32_t off;
    int extended_rate = 0;

    src_read(&src, 0, h, 26);
    if (strncmp((const char *)h, "Creative Voice File", 19) != 0)
        return 0;

    v->numseg = 0;
    v->bits = 8;
    v->rate = 0;

    for (off = read_le16(h + 0x14); ; )
    {
        int type, len;
        uint32_t data;

        if (src.size && off + 4 > (uint32_t)src.size)
            break;
        src_read(&src, off, h, 4);
        type = h[0];
        if (type == 0)
            break;
        len = read_le24(h + 1);
        data = off + 4;

        switch (type)
        {
            case 1: // sound data: time constant, pack type, samples
                src_read(&src, data, h, 2);
                if (!v->rate)
                    v->rate = extended_rate ? extended_rate : 1000000 / (256 - h[0]);
                if (h[1] == 0 && v->numseg < FX_MAX_SEGMENTS && len > 2)
                    v->seg[v->numseg++] = src_segment(&src, data + 2, len - 2);
                break;

            case 2: // continuation of the previous sound data
                if (v->numseg > 0 && v->numseg < FX_MAX_SEGMENTS && len > 0)
                    v->seg[v->numseg++] = src_segment(&src, data, len);
                break;

            case 8: // extended info for the next type 1 block
            {
                unsigned tc;
                int stereo;
                src_read(&src, data, h, 4);
                tc = read_le16(h);
                stereo = h[3];
                extended_rate = (256000000 / (65536 - tc)) >> (stereo ? 1 : 0);
                break;
            }

            case 9: // new format sound data
            {
                int bits, channels;
                src_read(&src, data, h, 12);
                bits = h[4];
                channels = h[5];
                if (channels == 1 && (bits == 8 || bits == 16) && v->numseg < FX_MAX_SEGMENTS && len > 12)
                {
                    v->rate = read_le32(h);
                    v->bits = bits;
                    v->seg[v->numseg++] = src_segment(&src, data + 12, (len - 12) / (bits / 8));
                }
                break;
            }

            default: // silence, markers, text, repeat blocks: skipped
                break;
        }
        off = data + len;
    }

    return v->numseg > 0 && v->rate > 0;
}

// RIFF WAVE, mono PCM 8 or 16 bits.
static int parse_wav(fx_voice_t *v, const uint8_t *ptr)
{
    fx_src_t src = sound_src(ptr);
    uint8_t h[24];
    uint32_t off, end;
    int have_fmt = 0;

    src_read(&src, 0, h, 12);
    if (strncmp((const char *)h, "RIFF", 4) != 0 || strncmp((const char *)h + 8, "WAVE", 4) != 0)
        return 0;

    end = 8 + read_le32(h + 4);
    if (src.size && end > (uint32_t)src.size)
        end = src.size;
    v->numseg = 0;

    for (off = 12; off + 8 <= end; )
    {
        unsigned size;

        src_read(&src, off, h, 8);
        size = read_le32(h + 4);
        if (strncmp((const char *)h, "fmt ", 4) == 0)
        {
            src_read(&src, off + 8, h, 16);
            if (read_le16(h) != 1 || read_le16(h + 2) != 1)
                return 0;               // not mono PCM
            v->rate = read_le32(h + 4);
            v->bits = read_le16(h + 14);
            if (v->bits != 8 && v->bits != 16)
                return 0;
            have_fmt = 1;
        }
        else if (strncmp((const char *)h, "data", 4) == 0 && have_fmt)
        {
            v->seg[0] = src_segment(&src, off + 8, size / (v->bits / 8));
            v->numseg = 1;
            break;
        }
        off += 8 + ((size + 1) & ~1);
    }

    return v->numseg > 0 && v->rate > 0;
}

static int total_length(const fx_voice_t *v)
{
    int i, len = 0;

    for (i = 0; i < v->numseg; i++)
        len += v->seg[i].len;
    return len;
}


/*
 * ------------------------------------------------------------------------
 *  Voices
 * ------------------------------------------------------------------------
 */

static fx_voice_t *get_voice(int handle)
{
    int i;

    if (handle <= 0)
        return NULL;
    for (i = 0; i < num_voices; i++)
        if (voices[i].handle == handle)
            return &voices[i];
    return NULL;
}

static int voice_channel(const fx_voice_t *v)
{
    return v - voices;
}

// Stops a voice and reports it to the game (MV_Kill).
static void kill_voice(fx_voice_t *v)
{
    uint32_t callbackval = v->callbackval;

    mixer_ch_stop(voice_channel(v));
    v->handle = 0;
    if (fx_callback)
        fx_callback(callbackval);
}

static fx_voice_t *alloc_voice(int priority)
{
    fx_voice_t *lowest = NULL;
    int i;

    for (i = 0; i < num_voices; i++)
    {
        if (voices[i].handle == 0)
            return &voices[i];
        if (!lowest || voices[i].priority < lowest->priority)
            lowest = &voices[i];
    }

    if (lowest && priority >= lowest->priority)
    {
        kill_voice(lowest);
        return lowest;
    }
    return NULL;
}

// The sound effects at full volume drowned the music: the General MIDI
// soundfont plays quieter than DOS' OPL/wavetable music, and a synth voice
// can not go past its CC7 maximum (nor a mixer channel past 1.0). The master
// volume boosts everything by MIX_MASTER and the effects are scaled back
// down by FX_GAIN: effectively the music gains MIX_MASTER over them.
#define MIX_MASTER 2.5f
#define FX_GAIN (0.6f / MIX_MASTER)

static void set_volume(fx_voice_t *v, int left, int right)
{
    float scale = FX_GAIN * (float)total_volume / (255.0f * FX_MAX_LEVEL);
    float l = FX_MIX_VOLUME(left) * scale;
    float r = FX_MIX_VOLUME(right) * scale;

    if (reverse_stereo)
        mixer_ch_set_vol(voice_channel(v), r, l);
    else
        mixer_ch_set_vol(voice_channel(v), l, r);
}

static void set_pitch(fx_voice_t *v, int pitchoffset)
{
    uint32_t scale = PITCH_GetScale(pitchoffset);   // 16.16 fixed point
    mixer_ch_set_freq(voice_channel(v), (float)v->rate * scale / 65536.0f);
}

// Common start of every Play function. The sound must already be parsed into
// the 'parsed' scratch voice; it is copied into a real voice here.
static int start_voice(const fx_voice_t *parsed, int looped, int pitchoffset,
                       int left, int right, int priority, uint32_t callbackval)
{
    fx_voice_t *v;
    int len;

    if (!fx_installed)
        return FX_Warning;

    len = total_length(parsed);
    if (len <= 0)
        return FX_Warning;

    v = alloc_voice(priority);
    if (!v)
        return FX_Warning;

    memcpy(v->seg, parsed->seg, sizeof(v->seg));
    v->numseg = parsed->numseg;
    v->bits = parsed->bits;
    v->rate = parsed->rate;
    v->priority = priority;
    v->callbackval = callbackval;

    do {
        if (++next_handle < 1)
            next_handle = 1;
    } while (get_voice(next_handle));
    v->handle = next_handle;

    // A fresh waveform (zeroed, so a new mixer uuid) for each sound: reusing
    // the old one would make the mixer replay the samples it has buffered.
    memset(&v->wave, 0, sizeof(v->wave));
    v->wave.name = "fx";
    v->wave.bits = v->bits;
    v->wave.channels = 1;
    v->wave.frequency = v->rate;
    v->wave.len = len;
    v->wave.loop_len = looped ? len : 0;
    v->wave.read = fx_read;
    v->wave.ctx = v;

    set_volume(v, left, right);
    mixer_ch_play(voice_channel(v), &v->wave);
    set_pitch(v, pitchoffset);
#ifdef N64_PROFILE
    sounds_started++;
#endif

    return v->handle;
}

static void calc_pan_table(void)
{
    int level, angle, distance, ramp;
    const int half = FX_NUM_PAN / 2;

    for (distance = 0; distance <= FX_MAX_LEVEL; distance++)
    {
        level = (255 * (FX_MAX_LEVEL - distance)) / FX_MAX_LEVEL;
        for (angle = 0; angle <= half / 2; angle++)
        {
            ramp = level - ((level * angle) / (FX_NUM_PAN / 4));

            pan_table[angle][distance][0] = ramp;
            pan_table[half - angle][distance][0] = ramp;
            pan_table[half + angle][distance][0] = level;
            pan_table[FX_MAX_PAN - angle][distance][0] = level;

            pan_table[angle][distance][1] = level;
            pan_table[half - angle][distance][1] = level;
            pan_table[half + angle][distance][1] = ramp;
            pan_table[FX_MAX_PAN - angle][distance][1] = ramp;
        }
    }
}

static void pan_3d(int angle, int distance, int *left, int *right)
{
    int level;

    if (distance < 0)
    {
        distance = -distance;
        angle += FX_NUM_PAN / 2;
    }
    level = FX_MIX_VOLUME(distance);
    angle &= FX_MAX_PAN;
    *left = pan_table[angle][level][0];
    *right = pan_table[angle][level][1];
}

#ifdef N64_PROFILE
void n64_audio_stats(void)
{
    debugf("  audio: voices=%d started=%d\n", (int)FX_SoundsPlaying(), sounds_started);
    sounds_started = 0;
}
#endif

/*
 * For long loops of the renderer (drawrooms, drawmasks): polls the mixer at
 * most every ~4 ms. The audio is only mixed when polled, and a frame can take
 * 50 ms or more: polling only between frames lets the queue run dry when the
 * RSP happens to be busy at that moment (the music cuts out).
 */
void n64_audio_poll_soon(void)
{
    static uint32_t last = 0;
    uint32_t now = TICKS_READ();

    if (TICKS_DISTANCE(last, now) < (int32_t)TICKS_FROM_MS(4))
        return;
    last = now;
    n64_audio_poll();
}

void n64_audio_poll(void)
{
    int i;

    if (!fx_installed)
        return;

    PROF_BEGIN(PROF_AUDIO);
    // Queue the mixing of the free audio buffers on the RSP without waiting
    // for it (mixer_poll() waits, which cost ~10 ms per frame with the music).
    mixer_try_play();

    // Report the voices that reached their end.
    for (i = 0; i < num_voices; i++)
    {
        if (voices[i].handle && !mixer_ch_playing(i))
        {
            uint32_t callbackval = voices[i].callbackval;
            voices[i].handle = 0;
            if (fx_callback)
                fx_callback(callbackval);
        }
    }
    PROF_END(PROF_AUDIO);
}


/*
 * ------------------------------------------------------------------------
 *  FX API
 * ------------------------------------------------------------------------
 */

char *FX_ErrorString(int ErrorNumber)
{
    return ErrorNumber == FX_Ok ? "Fx ok." : "N64 FX error.";
}

int FX_SetupCard(int SoundCard, fx_device *device)
{
    device->MaxVoices = FX_MAX_VOICES;
    device->MaxSampleBits = 16;
    device->MaxChannels = 2;
    return FX_Ok;
}

int FX_GetBlasterSettings(fx_blaster_config *blaster)
{
    return FX_Error;
}

int FX_SetupSoundBlaster(fx_blaster_config blaster, int *MaxVoices, int *MaxSampleBits, int *MaxChannels)
{
    *MaxVoices = FX_MAX_VOICES;
    *MaxSampleBits = 16;
    *MaxChannels = 2;
    return FX_Ok;
}

int FX_Init(int SoundCard, int numvoices, int numchannels, int samplebits, unsigned mixrate)
{
    int i;

    if (fx_installed)
        FX_Shutdown();

    num_voices = max(1, min(numvoices, MIXER_MAX_CHANNELS - N64_MUSIC_CHANNELS));
    memset(voices, 0, sizeof(voices));

    calc_pan_table();   // (pitch.c's table is precomputed: no PITCH_Init)

    audio_init(mixrate, AUDIO_DEFAULT_LATENCY);
    if (!fx_bounce)
        fx_bounce = malloc_uncached(FX_BOUNCE_SIZE);
    // Sound effects use channels [0, num_voices), the music synthesizer the
    // N64_MUSIC_CHANNELS after them.
    mixer_init(num_voices + N64_MUSIC_CHANNELS);
    mixer_set_vol(MIX_MASTER);
    for (i = 0; i < num_voices; i++)
    {
        // Duke's samples are at most 22 kHz, and pitch shifts can double them.
        mixer_ch_set_limits(i, 16, 48000, 0);
    }
    for (; i < num_voices + N64_MUSIC_CHANNELS; i++)
    {
        // Soundfont samples are recorded at up to 44.1 kHz and notes above
        // their root key play them faster. The channel's sample buffer grows
        // with this limit (~13 KB each at 3x): the rare note above it is
        // clamped (played lower).
        mixer_ch_set_limits(i, 16, 3 * 44100, 0);
    }

    fx_installed = 1;
    return FX_Ok;
}

int n64_music_first_channel(void)
{
    return fx_installed ? num_voices : -1;
}

int FX_Shutdown(void)
{
    if (!fx_installed)
        return FX_Ok;

    FX_StopAllSounds();
    mixer_close();
    audio_close();
    fx_installed = 0;
    return FX_Ok;
}

int FX_SetCallBack(void (*function)(int32_t))
{
    fx_callback = function;
    return FX_Ok;
}

void FX_SetVolume(int volume)
{
    total_volume = max(0, min(volume, 255));
}

int FX_GetVolume(void)
{
    return total_volume;
}

void FX_SetReverseStereo(int setting)
{
    reverse_stereo = setting;
}

int FX_GetReverseStereo(void)
{
    return reverse_stereo;
}

// No reverb on the N64 (MULTIVOC's was a CPU effect on the whole mix).
void FX_SetReverb(int reverb) {}
void FX_SetFastReverb(int reverb) {}
int FX_GetMaxReverbDelay(void) { return 0; }
int FX_GetReverbDelay(void) { return 0; }
void FX_SetReverbDelay(int delay) {}

int FX_VoiceAvailable(int priority)
{
    int i;
    int lowest = -1;

    for (i = 0; i < num_voices; i++)
    {
        if (voices[i].handle == 0)
            return 1;
        if (lowest < 0 || voices[i].priority < lowest)
            lowest = voices[i].priority;
    }
    return lowest >= 0 && priority >= lowest;
}

int FX_EndLooping(int handle)
{
    fx_voice_t *v = get_voice(handle);

    if (!v)
        return FX_Warning;
    mixer_ch_set_loop(voice_channel(v), false);
    return FX_Ok;
}

int FX_SetPan(int handle, int vol, int left, int right)
{
    fx_voice_t *v = get_voice(handle);

    if (!v)
        return FX_Warning;
    set_volume(v, left, right);
    return FX_Ok;
}

int FX_SetPitch(int handle, int pitchoffset)
{
    fx_voice_t *v = get_voice(handle);

    if (!v)
        return FX_Warning;
    set_pitch(v, pitchoffset);
    return FX_Ok;
}

int FX_SetFrequency(int handle, int frequency)
{
    fx_voice_t *v = get_voice(handle);

    if (!v)
        return FX_Warning;
    v->rate = frequency;
    mixer_ch_set_freq(voice_channel(v), frequency);
    return FX_Ok;
}

int FX_PlayLoopedVOC(uint8_t *ptr, int32_t loopstart, int32_t loopend,
       int32_t pitchoffset, int32_t vol, int32_t left, int32_t right, int32_t priority,
       uint32_t callbackval)
{
    fx_voice_t parsed;

    // (The game picks VOC or WAV from the first byte, which for a sound
    // streamed from ROM is our stream header: either may be either.)
    if (!parse_voc(&parsed, ptr) && !parse_wav(&parsed, ptr))
        return FX_Warning;
    return start_voice(&parsed, loopstart >= 0, pitchoffset, left, right, priority, callbackval);
}

int FX_PlayVOC(uint8_t *ptr, int pitchoffset, int vol, int left, int right,
       int priority, uint32_t callbackval)
{
    return FX_PlayLoopedVOC(ptr, -1, -1, pitchoffset, vol, left, right, priority, callbackval);
}

int FX_PlayLoopedWAV(uint8_t *ptr, int32_t loopstart, int32_t loopend,
       int32_t pitchoffset, int32_t vol, int32_t left, int32_t right, int32_t priority,
       uint32_t callbackval)
{
    fx_voice_t parsed;

    if (!parse_wav(&parsed, ptr) && !parse_voc(&parsed, ptr))
        return FX_Warning;
    return start_voice(&parsed, loopstart >= 0, pitchoffset, left, right, priority, callbackval);
}

int FX_PlayWAV(uint8_t *ptr, int pitchoffset, int vol, int left, int right,
       int priority, uint32_t callbackval)
{
    return FX_PlayLoopedWAV(ptr, -1, -1, pitchoffset, vol, left, right, priority, callbackval);
}

int FX_PlayVOC3D(uint8_t *ptr, int32_t pitchoffset, int32_t angle, int32_t distance,
       int32_t priority, uint32_t callbackval)
{
    int left, right;

    pan_3d(angle, distance, &left, &right);
    return FX_PlayVOC(ptr, pitchoffset, max(0, 255 - distance), left, right, priority, callbackval);
}

int FX_PlayWAV3D(uint8_t *ptr, int pitchoffset, int angle, int distance,
       int priority, uint32_t callbackval)
{
    int left, right;

    pan_3d(angle, distance, &left, &right);
    return FX_PlayWAV(ptr, pitchoffset, max(0, 255 - distance), left, right, priority, callbackval);
}

int FX_PlayLoopedRaw(uint8_t *ptr, uint32_t length, char *loopstart,
       char *loopend, uint32_t rate, int32_t pitchoffset, int32_t vol, int32_t left,
       int32_t right, int32_t priority, uint32_t callbackval)
{
    fx_voice_t parsed;

    parsed.seg[0] = (fx_segment_t){ ptr, 0, length };
    parsed.numseg = 1;
    parsed.bits = 8;
    parsed.rate = rate;
    return start_voice(&parsed, loopstart != NULL, pitchoffset, left, right, priority, callbackval);
}

int FX_PlayRaw(uint8_t *ptr, uint32_t length, uint32_t rate,
       int32_t pitchoffset, int32_t vol, int32_t left, int32_t right, int32_t priority,
       uint32_t callbackval)
{
    return FX_PlayLoopedRaw(ptr, length, NULL, NULL, rate, pitchoffset, vol, left, right, priority, callbackval);
}

int32_t FX_Pan3D(int handle, int angle, int distance)
{
    fx_voice_t *v = get_voice(handle);
    int left, right;

    if (!v)
        return FX_Warning;
    pan_3d(angle, distance, &left, &right);
    set_volume(v, left, right);
    return FX_Ok;
}

int32_t FX_SoundActive(int32_t handle)
{
    return get_voice(handle) != NULL;
}

int32_t FX_SoundsPlaying(void)
{
    int i, n = 0;

    for (i = 0; i < num_voices; i++)
        if (voices[i].handle)
            n++;
    return n;
}

int32_t FX_StopSound(int handle)
{
    fx_voice_t *v = get_voice(handle);

    if (!v)
        return FX_Warning;
    kill_voice(v);
    return FX_Ok;
}

int32_t FX_StopAllSounds(void)
{
    int i;

    for (i = 0; i < num_voices; i++)
        if (voices[i].handle)
            kill_voice(&voices[i]);
    return FX_Ok;
}

// Streaming and recording are not used by Duke Nukem 3D.
int32_t FX_StartDemandFeedPlayback(void (*function)(char **ptr, uint32_t *length),
       int32_t rate, int32_t pitchoffset, int32_t vol, int32_t left, int32_t right,
       int32_t priority, uint32_t callbackval)
{
    return FX_Error;
}

int FX_StartRecording(int MixRate, void (*function)(char *ptr, int length))
{
    return FX_Error;
}

void FX_StopRecord(void)
{
}
