/*
 * Walls and parallax sky drawn by the RDP (n64_display.c), fed by the engine
 * (n64_rdpscan in engine.c).
 *
 * BUILD texture maps walls and skies on the CPU, column by column (wallscan),
 * and in the 8-bit frame every pixel written costs a cache line fill: most of
 * the frame time. On the N64 the main view records each wall/sky column
 * instead (screen column, rows, tile column, texel stepping, shade table) and
 * writes nothing: the floors, ceilings, sprites and HUD are still drawn by the
 * CPU into the 8-bit frame.
 *
 *  - n64_cols_end(), right after drawrooms(): the RDP marks the recorded
 *    columns as holes (N64_HOLE_INDEX) in the 8-bit frame, with a list of fill
 *    rectangles. The CPU then draws sprites and HUD over them as usual.
 *  - When the frame is presented, the RDP draws the columns (1 pixel wide
 *    texture rectangles, with the column's shade table as palette), then
 *    copies the 8-bit frame on top with the holes transparent.
 *
 * Both passes are raw RDP command lists built in memory and run with
 * rdpq_exec: a few 64-bit words per column instead of several rdpq calls.
 */
#ifndef N64_COLUMNS_H
#define N64_COLUMNS_H

#include <stdint.h>

// On by default; build with `make RDP_COLUMNS=0` to draw everything on the CPU.
#ifndef N64_RDP_COLUMNS
#define N64_RDP_COLUMNS 1
#endif

// Palette index of the holes. 255 is BUILD's transparent color, so normal
// walls, floors and sprites do not produce it on screen.
#define N64_HOLE_INDEX 255

// The RDP reads the 8-bit frame, its palettes and the wall tiles after
// present, while the CPU runs the game logic: anything that writes them
// waits for the RDP first.
void n64_wait_rdp(void);

// The 8-bit buffer BUILD draws the screen into.
void *n64_screen_buffer(void);

// Around the main view's drawrooms() (displayrooms in game.c): begin starts a
// new set of columns (on = 0: this view is drawn on the CPU, e.g. the tilted
// screen), end has the RDP punch their holes and waits for it.
void n64_cols_begin(int on);
void n64_cols_end(void);

// Forgets the last view's columns (the screen was cleared). Until then they
// are drawn with every presented frame, like the pixels of a CPU-drawn view
// stay in the 8-bit frame.
void n64_cols_forget(void);

// True while the main view records its columns.
int n64_cols_active(void);

// Makes room for count more columns. Returns 0 if the frame is full (the
// caller then draws that segment on the CPU).
int n64_cols_reserve(int count);

// Registers the tile a segment reads from (size bytes): keeps it locked in
// BUILD's cache until the RDP has drawn it. Returns 0 if too many tiles, or
// too many bytes of the cache, are locked already.
int n64_cols_use_tile(const uint8_t *data, int32_t size, uint8_t *lock);

// One column at screen column sx, rows [y1, y2). The texture is the tile
// (column-major, tile_h texels per column, tile_w columns); the column is
// column col and, like BUILD, wraps every 2^logw texels (reading past the
// column when the tile height is not a power of two). The texel at row y is
// ((vplce + vince * (y - y1)) >> shift), and palrow is the column's 256 byte
// shade table.
void n64_cols_add(int sx, int y1, int y2, const uint8_t *tile, int tile_h, int tile_w,
                  int col, int logw, uint32_t vplce, int32_t vince, int shift,
                  const uint8_t *palrow);

#endif
