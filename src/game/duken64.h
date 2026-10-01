//
//  duken64.h
//  Duke3D
//
//  Game-side platform header for the Nintendo 64. Based on dukeunix.h, without
//  the POSIX directory API that newlib does not provide on the N64.
//

#ifndef Duke3D_duken64_h
#define Duke3D_duken64_h

#define cdecl
#define __far
#define __interrupt

#define STUBBED(x) fprintf(stderr,"STUB: %s (%s, %s:%d)\n",x,__FUNCTION__,__FILE__,__LINE__)

#define PATH_SEP_CHAR '/'
#define PATH_SEP_STR  "/"
#define ROOTDIR       "/"
#define CURDIR        "./"

#ifndef O_BINARY
#define O_BINARY 0
#endif

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>

// No directory listing on the N64: _dos_findfirst() never finds anything.
struct find_t
{
    char  name[MAX_PATH];
};
int _dos_findfirst(char  *filename, int x, struct find_t *f);
int _dos_findnext(struct find_t *f);

struct dosdate_t
{
    uint8_t  day;
    uint8_t  month;
    unsigned int year;
    uint8_t  dayofweek;
};

void _dos_getdate(struct dosdate_t *date);

#ifndef min
#define min(x, y) ((x) < (y) ? (x) : (y))
#endif

#ifndef max
#define max(x, y) ((x) > (y) ? (x) : (y))
#endif

#ifndef strcmpi
#define strcmpi(x, y) strcasecmp(x, y)
#endif

// Only used for the DOS "not enough memory" check; main() verifies the
// Expansion Pak instead (n64_early_init).
#define Z_AvailHeap() ((8 * 1024) * 1024)

#define printchrasm(x,y,ch) printf("%c", (uint8_t ) (ch & 0xFF))

#ifdef __GNUC__
#define GCC_PACK1_EXT __attribute__((packed,aligned(1)))
#endif

// The ROM filesystem is read-only: directories are never created.
#define mkdir(X) (-1)
#define getch() (0)

#endif
