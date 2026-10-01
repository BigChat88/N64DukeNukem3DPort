/*
 * Minimal SDL stand-in for the N64 build.
 *
 * A few game files (menues.c, global.c, audiolib) still call SDL for things
 * that do not exist on a console: grabbing the mouse, quitting the video
 * subsystem, or locking the audio thread (the N64 mixes audio from the main
 * loop, see n64_dsl.c). These become no-ops so the original code can stay
 * untouched.
 */
#ifndef N64_SDL_SHIM_H
#define N64_SDL_SHIM_H

typedef struct SDL_Surface SDL_Surface;
typedef struct SDL_mutex SDL_mutex;

#define SDL_INIT_VIDEO   0x00000020
#define SDL_GRAB_QUERY   -1
#define SDL_GRAB_OFF     0
#define SDL_GRAB_ON      1

static inline void SDL_Quit(void) {}
static inline void SDL_QuitSubSystem(unsigned flags) { (void)flags; }
static inline int SDL_WM_GrabInput(int mode) { (void)mode; return SDL_GRAB_OFF; }
static inline int SDL_ShowCursor(int toggle) { (void)toggle; return 0; }

static inline SDL_mutex *SDL_CreateMutex(void) { return (SDL_mutex *)0; }
static inline void SDL_DestroyMutex(SDL_mutex *m) { (void)m; }
static inline int SDL_mutexP(SDL_mutex *m) { (void)m; return 0; }
static inline int SDL_mutexV(SDL_mutex *m) { (void)m; return 0; }

#endif
