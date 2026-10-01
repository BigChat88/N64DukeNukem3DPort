//
//  n64_compat.h
//  Duke3D
//
//  Platform header for the Nintendo 64 (libdragon). Based on unix_compat.h.
//

#ifndef Duke3D_n64_compat_h
#define Duke3D_n64_compat_h

#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#define PLATFORM_N64 1

#define kmalloc(x) malloc(x)
#define kkmalloc(x) malloc(x)
#define kfree(x) free(x)
#define kkfree(x) free(x)

#ifdef FP_OFF
#undef FP_OFF
#endif

// Watcom allowed a memory pointer to be cast to a 32 bits integer.
// Pointers are 32 bits on the N64 (o64 ABI) so this is safe here.
#define FP_OFF(x) ((int32_t) (x))

#ifndef max
#define max(x, y)  (((x) > (y)) ? (x) : (y))
#endif

#ifndef min
#define min(x, y)  (((x) < (y)) ? (x) : (y))
#endif

#define __int64 int64_t

#define O_BINARY 0

#define stricmp strcasecmp
#define strcmpi strcasecmp

#ifndef S_IREAD
#define S_IREAD S_IRUSR
#endif

// No networking on the N64: only the dummy (single player / fake multi) layer.
#define USER_DUMMY_NETWORK 1

// Size of BUILD's tile cache. Tiles are streamed from ROM on demand, so this
// only needs to hold the art used by the current level. 2 MB leaves room for
// the music soundfont (~1.25 MB of sample headers, see n64_music.c) with
// ~800 KB of heap to spare.
#define N64_TILE_CACHE_SIZE (2 * 1024 * 1024)

// All game data lives in the DragonFS image appended to the ROM.
#define N64_DATA_PREFIX "rom:/"

// Called by main() before anything touches the filesystem.
void n64_early_init(void);
// Replaces main()'s argc/argv with the arguments baked in at build time.
void n64_get_args(int *argc, char ***argv);
// Analog stick turn for getinput(), in angvel units (n64_display.c).
int n64_stick_turn(void);
// True while the controller should drive menus rather than the player
// (implemented by the game, player.c).
int n64_in_menu(void);
// Logs the time since power on, to find slow boot phases.
void n64_boot_mark(const char *phase);
// Logs the heap in use and free (label: where), see n64_system.c.
void n64_log_memory(const char *label);
// Shows a fatal error on screen and halts.
void n64_fatal(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

#endif
