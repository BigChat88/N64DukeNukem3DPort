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

#include <stddef.h>
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

/*
 * The recorder's state (n64_display.c owns it). Recording a column is a few
 * stores, done for every column of every wall: the functions below are
 * inline, a call into another file cost about as much as the work.
 */
#define N64_COL_WORDS_MAX   14          // most commands one column can add
typedef struct {
    int masked;                         // 0: the walls (with their holes), 1: the overlay's
                                        // masked columns (palette index 255 transparent)
    int on;                             // the main view is recording
    uint64_t *pw, *lw;                  // where the hole / textured pass go on (NULL: nothing recorded)
    uint64_t *punch_end, *list_end;     // ends of their buffers
    // Texture state of the textured pass while it is written.
    const uint8_t *cur_pal, *cur_tile;
    int cur_col, cur_rows, cur_logw, off;
    int added;                          // columns recorded (PROFILE builds)
} n64_cols_t;
extern n64_cols_t n64_cols;
// The masked columns of the overlay (wall sprites, see n64_overlay.h).
extern n64_cols_t n64_mcols;

// The palette slot of a shade table, built if new; NULL when all are taken.
const uint16_t *n64_cols_tlut_for(const uint8_t *palrow);
// Same for the masked columns (index 255 transparent); never NULL: when all
// are taken, the nearest shade of the same table.
const uint16_t *n64_mcols_tlut_for(const uint8_t *palrow);

// Makes room for count more columns. Returns 0 if the frame is full (the
// caller then draws that segment on the CPU).
static inline int n64_cols_reserve(int count)
{
    return n64_cols.on &&
           n64_cols.pw + count + 2 <= n64_cols.punch_end &&
           n64_cols.lw + count * N64_COL_WORDS_MAX + 4 <= n64_cols.list_end;
}

/*
 * Raw RDP commands (see the RDP command reference): the column passes are
 * written straight into memory and run with rdpq_exec.
 */
#define RDP_CMD(id)     ((uint64_t)(id) << 56)
#define RDP_SYNC_PIPE   RDP_CMD(0x27)
#define RDP_SYNC_TILE   RDP_CMD(0x28)
#define RDP_SYNC_LOAD   RDP_CMD(0x31)

// SET_TEXTURE_IMAGE (0x3D) / SET_COLOR_IMAGE (0x3F).
static inline uint64_t rdp_image(int id, int fmt, int size, int width, const void *addr)
{
    return RDP_CMD(id) | ((uint64_t)fmt << 53) | ((uint64_t)size << 51) |
           ((uint64_t)(width - 1) << 32) | ((uint32_t)(uintptr_t)addr & 0x1FFFFFFF);
}

// The layout shared by the rectangles, loads and the scissor: two 12-bit
// coordinate pairs (10.2 fixed point) and a tile number.
static inline uint64_t rdp_coords(int id, int a, int b, int tile, int c, int d)
{
    return RDP_CMD(id) | ((uint64_t)(a & 0xFFF) << 44) | ((uint64_t)(b & 0xFFF) << 32) |
           ((uint64_t)tile << 24) | ((uint64_t)(c & 0xFFF) << 12) | (uint64_t)(d & 0xFFF);
}

// SET_TILE (0x35): s wraps every 2^masks texels (0: no wrap), t clamps.
static inline uint64_t rdp_set_tile(int fmt, int size, int line, int tmem, int tile, int masks)
{
    return RDP_CMD(0x35) | ((uint64_t)fmt << 53) | ((uint64_t)size << 51) |
           ((uint64_t)line << 41) | ((uint64_t)tmem << 32) | ((uint64_t)tile << 24) |
           (1ull << 19) | ((uint64_t)masks << 4);
}

// Registers the tile a segment reads from (size bytes): keeps it locked in
// BUILD's cache until the RDP has drawn it. Returns 0 if too many tiles, or
// too many bytes of the cache, are locked already.
int n64_cols_use_tile(const uint8_t *data, int32_t size, uint8_t *lock);

// One column at screen column sx, rows [y1, y2). The texture is the tile
// (column-major, tile_h texels per column, tile_w columns); the column is
// column col and, like BUILD, wraps every 2^logw texels (reading past the
// column when the tile height is not a power of two). The texel at row y is
// ((vplce + vince * (y - y1)) >> shift), and palrow is the column's 256 byte
// shade table. Returns 0 if it cannot be recorded (too many shade tables in
// the frame): the caller then draws the column on the CPU. Room for it must
// have been made with n64_cols_reserve.
static inline int n64_cols_add_to(n64_cols_t *c, int sx, int y1, int y2,
                                  const uint8_t *tile, int tile_h, int tile_w,
                                  int col, int logw, uint32_t vplce, int32_t vince, int shift,
                                  const uint8_t *palrow)
{
    uint64_t *w = c->lw;
    int32_t ds, sfx;

    // Hole pass (walls only: masked columns are drawn over the frame).
    if (!c->masked)
        *c->pw++ = rdp_coords(0x36, sx << 2, (y2 - 1) << 2, 0, sx << 2, y1 << 2);

    // Textured pass: a 1 pixel wide flipped texture rectangle over the tile
    // column, with the column's shade table as palette.
    if (palrow != c->cur_pal)
    {
        const uint16_t *pal = c->masked ? n64_mcols_tlut_for(palrow) : n64_cols_tlut_for(palrow);
        if (!pal)
        {
            if (!c->masked)
                c->pw--;    // (no palette slot left: drawn on the CPU)
            return 0;
        }
        // The palette goes to the upper half of TMEM through tile 7. (A load
        // also rewrites its tile's size: on the real RDP, the primitive before
        // may still be reading the tiles, hence SYNC_TILE as well as
        // SYNC_LOAD. Emulators run commands one at a time and never show it.)
        *w++ = RDP_SYNC_LOAD;
        *w++ = RDP_SYNC_TILE;
        *w++ = rdp_image(0x3D, 0 /* RGBA */, 2 /* 16 bpp */, 256, pal);
        *w++ = rdp_set_tile(4, 0, 0, 0x100, 7, 0);
        *w++ = rdp_coords(0x30, 0, 0, 7, 255 << 2, 0);        // LOAD_TLUT
        c->cur_pal = palrow;
        c->cur_tile = NULL;     // (its texture image is ours to set again)
    }
    if (tile != c->cur_tile)
    {
        // The tile seen as an image with one row per tile column. The RDP
        // wants an 8 byte aligned base: the rest goes in the s offset.
        uintptr_t base = (uintptr_t)tile & ~(uintptr_t)7;
        c->off = (uintptr_t)tile - base;
        *w++ = rdp_image(0x3D, 2 /* CI */, 1 /* 8 bpp */, tile_h, (const void *)base);
        c->cur_tile = tile;
        c->cur_col = -1;
    }
    if (logw != c->cur_logw)
    {
        // 2^logw texels per TMEM row, wrapping like BUILD's shift.
        *w++ = RDP_SYNC_TILE;
        *w++ = rdp_set_tile(2 /* CI */, 1 /* 8 bpp */, (1 << logw) >> 3, 0, 0, logw);
        c->cur_logw = logw;
        c->cur_col = -1;
    }
    if (c->cur_col < 0 || col < c->cur_col || col >= c->cur_col + c->cur_rows)
    {
        // Load as many consecutive columns as fit in the 2 KB of TMEM left
        // by the palette. Past the end of a column the load goes on into the
        // next one, as BUILD reads it when the height is not a power of two.
        c->cur_rows = 2048 >> logw;
        if (col + c->cur_rows > tile_w)
            c->cur_rows = (tile_w - col > 1) ? tile_w - col : 1;
        *w++ = RDP_SYNC_LOAD;
        *w++ = RDP_SYNC_TILE;       // the load rewrites tile 0's size (see above)
        *w++ = rdp_coords(0x34, c->off << 2, col << 2, 0,
                          (c->off + (1 << logw) - 1) << 2, (col + c->cur_rows - 1) << 2);
        c->cur_col = col;
    }

    // Same texel stepping as vlineasm (texel = vplce >> shift): s in 10.5
    // fixed point down the column, ds in 5.10.
    sfx = (c->off << 5) + (int32_t)(vplce >> (shift - 5));
    ds = vince >> (shift - 10);
    if (ds > 32767) ds = 32767;
    if (ds < -32768) ds = -32768;
    *w++ = rdp_coords(0x25, (sx + 1) << 2, y2 << 2, 0, sx << 2, y1 << 2);
    *w++ = ((uint64_t)(uint16_t)sfx << 48) | ((uint64_t)(uint16_t)(col << 5) << 32) |
           ((uint64_t)(uint16_t)ds << 16);
    c->lw = w;
#ifdef N64_PROFILE
    c->added++;
#endif
    return 1;
}

static inline int n64_cols_add(int sx, int y1, int y2, const uint8_t *tile, int tile_h, int tile_w,
                               int col, int logw, uint32_t vplce, int32_t vince, int shift,
                               const uint8_t *palrow)
{
    return n64_cols_add_to(&n64_cols, sx, y1, y2, tile, tile_h, tile_w, col, logw, vplce, vince,
                           shift, palrow);
}

#endif
