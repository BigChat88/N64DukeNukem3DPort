/*
 * Music driver for the N64: the game's MIDI songs played by libdragon's
 * MID64 sequencer through an SF64 (SoundFont) synthesizer.
 *
 * The desktop port plays the .MID files through SDL_mixer. Here the Makefile
 * extracts them from the GRP and converts them to rom:/music/<name>.mid64,
 * and converts a General MIDI soundfont (assets/soundfont) to
 * rom:/soundfont.sf64. The synthesizer voices are mixed on the RSP, on the
 * N64_MUSIC_CHANNELS mixer channels after the sound effect voices.
 *
 * The music volume of the menu is applied by scaling every MIDI channel
 * volume (CC7) on its way from the sequencer to the synthesizer.
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <libdragon.h>

#include "audiolib/music.h"
#include "n64_platform.h"

#define SOUNDFONT_PATH  "rom:/soundfont.sf64"
#define MUSIC_DIR       "rom:/music/"
#define GM_DEFAULT_CC7  100         // General MIDI power-on channel volume

int MUSIC_ErrorCode = MUSIC_Ok;

static sf64_bank_t *bank = NULL;
static sf64_synth_t *synth = NULL;
static mid64player_t *player = NULL;
static int music_volume = 255;      // MUSIC_SetVolume, 0-255
static int paused = 0;


/*
 * ------------------------------------------------------------------------
 *  Volume: a MIDI target that forwards everything to the synthesizer,
 *  scaling CC7 by the music volume.
 * ------------------------------------------------------------------------
 */

typedef struct {
    midi_target_t base;             // must stay first
    midi_target_t *synth;
    int cc7[SF64_MIDI_CHANNELS];    // volume requested by the song
} volume_target_t;

static volume_target_t volume_target;

#define SYNTH_OPS (volume_target.synth->ops)

static int scaled_volume(int value)
{
    return value * music_volume / 255;
}

static void vt_reset_volumes(void)
{
    int ch;
    for (ch = 0; ch < SF64_MIDI_CHANNELS; ch++)
        volume_target.cc7[ch] = GM_DEFAULT_CC7;
}

static void vt_note_on(midi_target_t *t, int ch, int key, int vel, int64_t now)
{ SYNTH_OPS->note_on(volume_target.synth, ch, key, vel, now); }

static void vt_note_off(midi_target_t *t, int ch, int key, int vel, int64_t now)
{ SYNTH_OPS->note_off(volume_target.synth, ch, key, vel, now); }

static void vt_control_change(midi_target_t *t, int ch, int cc, int value, int64_t now)
{
    if (cc == 7 && ch >= 0 && ch < SF64_MIDI_CHANNELS)
    {
        volume_target.cc7[ch] = value;
        value = scaled_volume(value);
    }
    else if (cc == 121)             // Reset All Controllers
    {
        if (ch >= 0 && ch < SF64_MIDI_CHANNELS)
            volume_target.cc7[ch] = GM_DEFAULT_CC7;
    }
    SYNTH_OPS->control_change(volume_target.synth, ch, cc, value, now);
    if (cc == 121 && ch >= 0 && ch < SF64_MIDI_CHANNELS)
        SYNTH_OPS->control_change(volume_target.synth, ch, 7, scaled_volume(GM_DEFAULT_CC7), now);
}

static void vt_program_change(midi_target_t *t, int ch, int program, int64_t now)
{ SYNTH_OPS->program_change(volume_target.synth, ch, program, now); }

static void vt_pitch_bend(midi_target_t *t, int ch, int value, int64_t now)
{ SYNTH_OPS->pitch_bend(volume_target.synth, ch, value, now); }

static void vt_poly_pressure(midi_target_t *t, int ch, int key, int pressure, int64_t now)
{ if (SYNTH_OPS->poly_pressure) SYNTH_OPS->poly_pressure(volume_target.synth, ch, key, pressure, now); }

static void vt_channel_pressure(midi_target_t *t, int ch, int pressure, int64_t now)
{ if (SYNTH_OPS->channel_pressure) SYNTH_OPS->channel_pressure(volume_target.synth, ch, pressure, now); }

static void vt_finish(midi_target_t *t, int64_t now)
{ if (SYNTH_OPS->finish) SYNTH_OPS->finish(volume_target.synth, now); }

static void apply_volumes(int64_t now)
{
    int ch;
    for (ch = 0; ch < SF64_MIDI_CHANNELS; ch++)
        SYNTH_OPS->control_change(volume_target.synth, ch, 7, scaled_volume(volume_target.cc7[ch]), now);
}

static void vt_reset(midi_target_t *t, int64_t now)
{
    if (SYNTH_OPS->reset) SYNTH_OPS->reset(volume_target.synth, now);
    vt_reset_volumes();
    apply_volumes(now);
}

static void vt_system_reset(midi_target_t *t, midi_system_t system, int64_t now)
{
    if (SYNTH_OPS->system_reset) SYNTH_OPS->system_reset(volume_target.synth, system, now);
    vt_reset_volumes();
    apply_volumes(now);
}

static int64_t vt_process(midi_target_t *t, int64_t now)
{ return SYNTH_OPS->process ? SYNTH_OPS->process(volume_target.synth, now) : 0; }

static const midi_target_ops_t volume_target_ops = {
    .note_on = vt_note_on,
    .note_off = vt_note_off,
    .control_change = vt_control_change,
    .program_change = vt_program_change,
    .pitch_bend = vt_pitch_bend,
    .poly_pressure = vt_poly_pressure,
    .channel_pressure = vt_channel_pressure,
    .finish = vt_finish,
    .reset = vt_reset,
    .system_reset = vt_system_reset,
    .process = vt_process,
};


/*
 * ------------------------------------------------------------------------
 *  Setup
 * ------------------------------------------------------------------------
 */

// Loads the soundfont and creates the synthesizer the first time a song
// plays: the mixer only exists once the sound effects are initialized.
static int ensure_synth(void)
{
    int first;

    if (synth)
        return 1;

    first = n64_music_first_channel();
    if (first < 0)
        return 0;

    if (access(SOUNDFONT_PATH, F_OK) != 0)
    {
        debugf("music: %s missing, music disabled\n", SOUNDFONT_PATH);
        return 0;
    }

    {
        heap_stats_t before, after;
        sys_get_heap_stats(&before);
        bank = sf64_load(SOUNDFONT_PATH);
        sys_get_heap_stats(&after);
        debugf("music: soundfont uses %d KB of RAM (%d KB left)\n",
               (after.used - before.used) / 1024, after.free / 1024);
    }
    synth = sf64_synth_create(bank);
    sf64_synth_set_mode(synth, SF64_MODE_GM1);
    sf64_synth_set_channels(synth, first, N64_MUSIC_CHANNELS, MIXER_PRIORITY_MUSIC);

    volume_target.base.ops = &volume_target_ops;
    volume_target.synth = sf64_synth_midi_target(synth);
    vt_reset_volumes();
    return 1;
}

// "GRABBAG.MID" -> "rom:/music/grabbag.mid64"
static void song_path(const char *filename, char *out, size_t size)
{
    const char *base = strrchr(filename, '/');
    char name[64];
    char *dot;
    size_t i;

    base = base ? base + 1 : filename;
    for (i = 0; base[i] && i < sizeof(name) - 1; i++)
        name[i] = tolower((unsigned char)base[i]);
    name[i] = '\0';

    dot = strrchr(name, '.');
    if (dot)
        *dot = '\0';
    snprintf(out, size, MUSIC_DIR "%s.mid64", name);
}


/*
 * ------------------------------------------------------------------------
 *  MUSIC API (game/audiolib/music.h)
 * ------------------------------------------------------------------------
 */

char *MUSIC_ErrorString(int ErrorNumber)
{
    return ErrorNumber == MUSIC_Ok ? "Music ok." : "N64 music error.";
}

int MUSIC_Init(int SoundCard, int Address)
{
    return MUSIC_Ok;
}

int MUSIC_Shutdown(void)
{
    MUSIC_StopSong();
    if (synth)
    {
        sf64_synth_close(synth);
        sf64_close(bank);
        synth = NULL;
        bank = NULL;
    }
    return MUSIC_Ok;
}

void MUSIC_SetVolume(int volume)
{
    int ch;

    music_volume = volume < 0 ? 0 : (volume > 255 ? 255 : volume);
    if (synth)
        for (ch = 0; ch < SF64_MIDI_CHANNELS; ch++)
            sf64_synth_set_volume(synth, ch, scaled_volume(volume_target.cc7[ch]));
}

int MUSIC_GetVolume(void)
{
    return music_volume;
}

void MUSIC_Continue(void)
{
    // The sequencer cannot resume mid-song: the song restarts.
    if (player && paused)
        mid64player_play(player, &volume_target.base);
    paused = 0;
}

void MUSIC_Pause(void)
{
    if (player && !paused)
        mid64player_stop(player);
    paused = 1;
}

int MUSIC_StopSong(void)
{
    if (player)
    {
        mid64player_close(player);
        player = NULL;
    }
    paused = 0;
    return MUSIC_Ok;
}

void MUSIC_RegisterTimbreBank(uint8_t  *timbres)
{
    // FM (OPL) instrument definitions: the soundfont replaces them.
}

void PlayMusic(char* filename)
{
    char path[128];

    MUSIC_StopSong();

    if (!filename || !ensure_synth())
        return;

    song_path(filename, path, sizeof(path));
    if (access(path, F_OK) != 0)
    {
        debugf("music: %s not found\n", path);
        return;
    }

    player = mid64player_load(path);
    mid64player_set_loop(player, true);
    mid64player_play(player, &volume_target.base);
}
