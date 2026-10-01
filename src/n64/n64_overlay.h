/*
 * 2D sprites drawn by the RDP on top of the frame (n64_display.c).
 *
 * rotatesprite() draws the weapon, the crosshair and the menus into the
 * 8-bit frame on the CPU, and they are redrawn on every frame, above
 * everything else. While capture is on (n64_overlay_capture), unrotated
 * rotatesprite() calls are recorded instead (see dorotatesprite in
 * engine.c) and the RDP draws them over the 8-bit frame when it is
 * presented, in the same order: the CPU does not touch those pixels.
 *
 * Things that are not redrawn every frame (the status bar) must not be
 * captured: they would vanish on the frames that do not draw them.
 */
#ifndef N64_OVERLAY_H
#define N64_OVERLAY_H

#include <stdint.h>

// Flags of n64_overlay_add.
#define N64_OVL_MASKED      1   // palette index 255 is transparent
#define N64_OVL_TRANSLUCENT 2   // blended with what is below
#define N64_OVL_FLIP_Y      4   // tile drawn upside down
#define N64_OVL_FLIP_X      8   // tile drawn mirrored
#define N64_OVL_TRANSPOSED  16  // tile columns are screen rows (ANM frames)

// Turns capture on (nesting allowed) / off around the code drawing the
// weapon, the crosshair and the menus.
void n64_overlay_capture(int on);
int n64_overlay_capturing(void);

// Records one sprite: the tile (column-major, tile_w columns of tile_h
// texels) drawn with its top-left corner at (x0, y0) and scale_x / scale_y
// screen pixels per texel, clipped to [cx1, cx2) x [cy1, cy2), seen through
// the shade table palrow. lock is the tile's lock byte in BUILD's cache: the
// tile stays locked until the RDP has drawn it. Returns 0 if it cannot be
// recorded (the caller then draws it on the CPU).
int n64_overlay_add(float x0, float y0, float scale_x, float scale_y,
                    const uint8_t *tile, int tile_w, int tile_h, uint8_t *lock,
                    const uint8_t *palrow, int flags,
                    int cx1, int cy1, int cx2, int cy2);

// Texels the CPU wrote outside BUILD's tile loader (ANM frames): written
// back from the CPU cache so that the RDP reads them.
void n64_overlay_flush(const void *data, int bytes);

// Screen tilt (hits, looking aside, death): the view window x1..x2, y1..y2
// (inclusive) of the frame being drawn is shown rotated by ang (BUILD units,
// 2048 = full turn) and zoomed to cover the window. Replaces rendering the
// view into a tile and rotating it on the CPU.
void n64_view_tilt(int ang, int x1, int y1, int x2, int y2);

// Most clip rectangles one sprite can have (n64_overlay_add_strips).
#define N64_OVL_MAX_STRIPS  48

// Same, clipped to the union of nstrips rectangles (x1, y1, x2, y2 each,
// ends excluded): the columns of a world sprite partly hidden by the level.
int n64_overlay_add_strips(float x0, float y0, float scale_x, float scale_y,
                           const uint8_t *tile, int tile_w, int tile_h, uint8_t *lock,
                           const uint8_t *palrow, int flags,
                           const int16_t *strips, int nstrips);

#endif
