#ifndef N64_PLATFORM_H
#define N64_PLATFORM_H

// Mounts "save:/", the save files in the cartridge FlashRAM (n64_savefs.c).
void n64_savefs_init(void);
// True if the last save file written did not fit in the FlashRAM.
int n64_savefs_full(void);

// Plays the "powered by libdragon" dragon logo (n64_intro.c). Sets up and
// closes its own display: call it before the game sets its video mode.
void n64_dragon_intro(void);

// Runs the mixer and reports the sound effects that ended (n64_fx.c).
// Called from the main loop: _nextpage(), _idle() and sampletimer().
void n64_audio_poll(void);
// Same, at most every few ms: for the renderer's long loops.
void n64_audio_poll_soon(void);

// A sound played straight from ROM (PI address and size of the file): the
// pointer to pass to the FX_Play functions in place of the loaded file.
// NULL if too many are in use.
const uint8_t *n64_fx_stream_sound(uint32_t rom, int32_t size);

// PROFILE builds: logs the output peak and voice counts since the last call.
void n64_audio_stats(void);

// Mixer channels reserved for the music synthesizer (n64_music.c), placed
// after the sound effect voices. The mixer supports 32 channels in total.
// Duke's songs hold up to 13-18 notes at once (gloomy, stalker, missimp),
// plus the release tails: with fewer voices the synthesizer keeps stealing
// notes, the quieter ones first, and the mix loses its balance. Only the
// voices sounding cost mixer time, but each channel that has played keeps a
// sample buffer (see n64_fx.c), hence 16 and not the 24 left free.
#define N64_MUSIC_CHANNELS 16

// First mixer channel available for music, or -1 while audio is not set up
// (FX_Init owns audio_init/mixer_init).
int n64_music_first_channel(void);

// The expansion pack of an add-on ROM (n64_system.c): the name of its GRP in
// rom:/addon/, or NULL in the base game's ROM. tools/pack_rom.py writes it in
// rom:/addon/ADDON.TXT, with the main CON script on a second line when it was
// chosen by hand (n64_addon_con, else NULL: the game picks it).
const char *n64_addon_grp(void);
const char *n64_addon_con(void);

// Behind the main menu with no game going (game.c): shows the add-on's
// picture this frame (1), or returns 0 if the ROM has none.
int n64_menu_background(void);
// Else a BUILD tile (column-major, the screen's size) with its own palette
// (768 bytes, 6-bit VGA): the game's title screen. 0 if it cannot.
int n64_menu_background_tile(const uint8_t *tile, int w, int h, const uint8_t *pal768);

// True once _setgamemode() has set up the display and the RDP (n64_display.c).
int n64_video_ready(void);

#endif
