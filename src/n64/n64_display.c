/*
 * Nintendo 64 (libdragon) replacement for BUILD's VESA/SDL display driver.
 *
 * Based on the SDL driver by Ryan C. Gordon (engine/display.c): the
 * platform-independent parts (2D line drawing, Ken's timer logic) are kept
 * as they were, the SDL parts are replaced by libdragon calls.
 *
 * Video: BUILD renders 8-bit paletted pixels into a 320x200 RAM buffer. At
 * _nextpage() that buffer is blitted by the RDP as a CI8 texture with the
 * palette loaded in TMEM as a TLUT, so the 8->16 bit conversion costs no CPU.
 * The VI stretches the 320x200 framebuffer to 4:3, like a DOS VGA mode 13h.
 *
 * Input: the N64 controller is translated into the PC scancodes BUILD expects.
 */

/*
 * "Build Engine & Tools" Copyright (c) 1993-1997 Ken Silverman
 * Ken Silverman's official web site: "http://www.advsys.net/ken"
 * See the included license file "BUILDLIC.TXT" for license info.
 * This file IS NOT A PART OF Ken Silverman's original release
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libdragon.h>
#include <rdpq_debug.h>

#include "platform.h"
#include "build.h"
#include "display.h"
#include "fixedPoint_math.h"
#include "engine.h"
#include "network.h"
#include "draw.h"
#include "cache.h"
#include "mmulti_unstable.h"
#include "n64_platform.h"
#include "n64_prof.h"
#include "n64_columns.h"
#include "n64_overlay.h"

#define N64_SCREEN_W 320
#define N64_SCREEN_H 200
// Bytes per row of the 8-bit buffer. The VR4300 data cache is 8 KB and direct
// mapped: with a 320 byte pitch, rows y and y+128 share a cache line, so the
// column renderers (walls, sprites, sky) keep evicting their own rows. A 336
// byte pitch only repeats every 512 rows. (No difference measured in ares,
// which may not model cache misses precisely: to be checked on hardware.)
#define N64_SCREEN_PITCH 336

int32_t xres, yres, bytesperline, imageSize, maxpages;
uint8_t* frameplace;
uint8_t* frameoffset;
uint8_t  *screen, vesachecked;
int32_t buffermode, origbuffermode, linearmode;
uint8_t  permanentupdate = 0, vgacompatible;

int32_t total_render_time = 1;
int32_t total_rendered_frames = 0;
static int32_t last_render_ticks = 0;

static unsigned int lastkey = 0;
static uint8_t drawpixel_color = 0;

static uint8_t *framebuffer8 = NULL;     // what BUILD draws into
static surface_t framebuffer8_surface;
static uint16_t *tlut = NULL;            // RGBA5551 palette, uncached for the RDP
static uint16_t tlut_cpu[256];           // same, for the CPU (uncached reads are slow)
uint8_t lastPalette[768];                // raw PALETTE.DAT, kept by initengine()
static uint8_t vgaPalette[256 * 4];      // BUILD format (B,G,R,unused; 0-63)
static int video_ready = 0;
static int palette_changes_since_frame = 0;   // see VBE_setPalette()

// Set when the RDP has finished the last presented frame (see n64_wait_rdp).
static volatile int rdp_done = 1;

static void rdp_done_callback(void *arg)
{
    rdp_done = 1;
}

// Walls and sky drawn by the RDP (n64_columns.h). The RDP commands of both
// passes are written while the columns are recorded.
#define COL_MAX         3072            // columns per frame
#define COL_MAX_TILES   192             // tiles per frame
#define COL_MAX_TLUTS   64              // shade tables per frame
#define COL_LIST_WORDS  16384           // raw RDP commands of the textured pass
#define COL_WORDS_MAX   14              // most commands one column can add
// Most bytes of BUILD's 2 MB tile cache the walls of a frame keep locked for
// the RDP (the overlay has its own budget, OVL_LOCK_BUDGET): past it a wall
// is drawn on the CPU, instead of risking a cache all locked up.
#define COL_LOCK_BUDGET (768 * 1024)

static int cols_on = 0;                 // the main view is recording its columns
static struct {
    const uint8_t *data;
    uint8_t *lock;
    uint8_t saved_lock;
} col_tiles[COL_MAX_TILES];
static int col_ntiles = 0;
static int32_t col_locked_bytes = 0;
static uint64_t *col_punch = NULL;      // raw RDP commands of the hole pass
static uint64_t *col_list = NULL;       // raw RDP commands of the textured pass
static uint64_t *col_pw = NULL, *col_lw = NULL;     // write positions (NULL: nothing recorded)
static uint16_t *col_tluts = NULL;      // COL_MAX_TLUTS palettes
static const uint8_t *col_tlut_src[COL_MAX_TLUTS];  // their shade tables
static int col_ntluts = 0;
static int col_tlut_version = 0;        // palette_version the palettes were built with
static int palette_version = 0;         // changes with every VBE_setPalette
// Texture state of the textured pass while it is written.
static const uint8_t *col_cur_pal, *col_cur_tile;
static int col_cur_col, col_cur_rows, col_cur_logw, col_off;
static uint16_t *tlut_keyed = NULL;     // screen palette with N64_HOLE_INDEX transparent

#ifdef N64_PROFILE
uint32_t n64_prof_ticks[PROF_COUNT];

uint32_t n64_prof_now(void)
{
    return get_ticks();
}

static void prof_report(int frames)
{
    static const char *names[PROF_COUNT] = { "rooms", "masks", "rest", "present", "weapon", "sbar", "console", "menus", "fta", "logic", "skydraw", "skyscan", "walls", "flats", "slopes", "sky", "tileload", "sndload", "audio", "con", "clipmove", "hitscan", "cansee", "getzrange", "scan", "animspr", "colend", "sprrdp", "sprcpu", "sprflat", "maskwall", "coladd", "wallcpu", "bunch", "drawalls", "wallmost" };
    char line[1024];
    int i, n;

    if (frames <= 0)
        return;
    n = snprintf(line, sizeof(line), "  ms/frame:");
    for (i = 0; i < PROF_COUNT; i++)
    {
        n += snprintf(line + n, sizeof(line) - n, " %s=%.1f", names[i],
                      TICKS_TO_US((uint64_t)n64_prof_ticks[i]) / 1000.0f / frames);
        n64_prof_ticks[i] = 0;
    }
    debugf("%s\n", line);
    {
        extern void n64_con_prof_report(int frames);
        n64_con_prof_report(frames);
    }
    {
        extern int n64_wall_fallback[5];
        debugf("  wall columns on the CPU: inactive=%d size=%d logw=%d full=%d lock=%d (per frame)\n",
               n64_wall_fallback[0] / frames, n64_wall_fallback[1] / frames, n64_wall_fallback[2] / frames,
               n64_wall_fallback[3] / frames, n64_wall_fallback[4] / frames);
        memset(n64_wall_fallback, 0, sizeof(n64_wall_fallback));
        n64_log_memory("now");
        extern int n64_cols_added;
        debugf("  columns: %d per frame\n", n64_cols_added / frames);
        n64_cols_added = 0;
    }
}
#endif


/*
 * ------------------------------------------------------------------------
 *  Video
 * ------------------------------------------------------------------------
 */

void* get_framebuffer(void){
    return framebuffer8;
}

static void init_new_res_vars(void)
{
    int i, j;

    xdim = xres = N64_SCREEN_W;
    ydim = yres = N64_SCREEN_H;
    bytesperline = N64_SCREEN_PITCH;
    imageSize = N64_SCREEN_PITCH * N64_SCREEN_H;
    maxpages = 1;
    vesachecked = 1;
    vgacompatible = 1;
    linearmode = 1;
    qsetmode = ydim;
    activepage = visualpage = 0;

    frameoffset = frameplace = framebuffer8;

    if (horizlookup)
        free(horizlookup);
    if (horizlookup2)
        free(horizlookup2);

    j = ydim*4*sizeof(int32_t);  /* Leave room for horizlookup&horizlookup2 */
    horizlookup = (int32_t*)malloc(j);
    horizlookup2 = (int32_t*)malloc(j);

    //Build lookup table (X screespace -> frambuffer offset.
    j = 0;
    for(i = 0; i <= ydim; i++)
    {
        ylookup[i] = j;
        j += bytesperline;
    }

    horizycent = ((ydim*4)>>1);

    /* Force drawrooms to call dosetaspect & recalculate stuff */
    oxyaspect = oxdimen = oviewingrange = -1;

    //Let the Assembly module how many pixels to skip when drawing a column
    setBytesPerLine(bytesperline);

    setview(0L,0L,xdim-1,ydim-1);

    setbrightness(curbrightness, palette);

    if (searchx < 0) {
        searchx = halfxdimen;
        searchy = (ydimen>>1);
    }
}

static void init_video(void)
{
    if (video_ready)
        return;

    resolution_t res = {
        .width = N64_SCREEN_W,
        .height = N64_SCREEN_H,
        .interlaced = INTERLACE_OFF,
    };
    display_init(res, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);
    rdpq_init();
#ifdef N64_RDP_DEBUG
    rdpq_debug_start();     // validate the RDP commands (reported in the log)
#endif

    framebuffer8 = memalign(64, N64_SCREEN_PITCH * N64_SCREEN_H);
    memset(framebuffer8, 0, N64_SCREEN_PITCH * N64_SCREEN_H);
    framebuffer8_surface = surface_make(framebuffer8, FMT_CI8, N64_SCREEN_W, N64_SCREEN_H, N64_SCREEN_PITCH);

    tlut = malloc_uncached(256 * sizeof(uint16_t));
    memset(tlut, 0, 256 * sizeof(uint16_t));
    tlut_keyed = malloc_uncached(256 * sizeof(uint16_t));
    memset(tlut_keyed, 0, 256 * sizeof(uint16_t));
    // Written through the CPU cache and written back before the RDP reads
    // them (uncached writes are much slower).
    col_punch = memalign(16, (COL_MAX + 16) * sizeof(uint64_t));
    col_list = memalign(16, COL_LIST_WORDS * sizeof(uint64_t));
    col_tluts = memalign(16, COL_MAX_TLUTS * 256 * sizeof(uint16_t));

    video_ready = 1;
}


/*
 * ------------------------------------------------------------------------
 *  Walls and sky drawn by the RDP (see n64_columns.h)
 * ------------------------------------------------------------------------
 */

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
           ((uint64_t)(width - 1) << 32) | PhysicalAddr(addr);
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

void *n64_screen_buffer(void)
{
    return framebuffer8;
}

// Unlocks the tiles of the recorded columns and forgets them. The RDP must be
// done with them.
static void cols_release(void)
{
    int i;
    for (i = col_ntiles - 1; i >= 0; i--)
        *col_tiles[i].lock = col_tiles[i].saved_lock;
    col_ntiles = 0;
    col_locked_bytes = 0;
    col_ntluts = 0;
    col_pw = col_lw = NULL;
}

static int menubg_frame;               // (see n64_menu_background)

void n64_cols_begin(int on)
{
    n64_wait_rdp();   // the RDP may still be drawing the last frame's columns
    cols_release();
    menubg_frame = 0;   // a view is drawn over the menu background's holes
    cols_on = on && video_ready;
    if (!cols_on)
        return;

    // Hole pass: fill mode into the 8-bit frame, every column a hole. (In
    // fill mode rectangles include their right and bottom edges.)
    col_pw = col_punch;
    *col_pw++ = RDP_SYNC_PIPE;
    *col_pw++ = rdp_image(0x3F, 4 /* I */, 1 /* 8 bpp */, N64_SCREEN_PITCH, framebuffer8);
    *col_pw++ = rdp_coords(0x2D, 0, 0, 0, N64_SCREEN_W << 2, N64_SCREEN_H << 2);    // scissor
    *col_pw++ = RDP_CMD(0x2F) | SOM_CYCLE_FILL;
    *col_pw++ = RDP_CMD(0x37) | 0xFFFFFFFFull;  // fill color: N64_HOLE_INDEX in every byte

    // Textured pass: 1 cycle mode, texture through the palette.
    col_lw = col_list;
    *col_lw++ = RDP_SYNC_PIPE;
    *col_lw++ = RDP_CMD(0x2F) | SOM_CYCLE_1 | SOM_TLUT_RGBA16 | SOM_SAMPLE_POINT |
                SOM_TF0_RGB | SOM_TF1_RGB | SOM_RGBDITHER_NONE | SOM_ALPHADITHER_NONE |
                SOM_COVERAGE_DEST_ZAP;
    *col_lw++ = RDP_CMD(0x3C) | (RDPQ_COMBINER1((0,0,0,TEX0), (0,0,0,TEX0)) & 0x00FFFFFFFFFFFFFFull);
    col_cur_pal = col_cur_tile = NULL;
    col_cur_col = col_cur_logw = -1;
    col_cur_rows = 1;
    col_off = 0;
}

void n64_cols_forget(void)
{
    n64_wait_rdp();
    cols_release();
    menubg_frame = 0;   // (clearview: the holes are gone)
}

int n64_cols_active(void)
{
    return cols_on;
}

int n64_cols_reserve(int count)
{
    return cols_on &&
           col_pw + count + 2 <= col_punch + COL_MAX + 16 &&
           col_lw + count * COL_WORDS_MAX + 4 <= col_list + COL_LIST_WORDS;
}

int n64_cols_use_tile(const uint8_t *data, int32_t size, uint8_t *lock)
{
    int i;

    for (i = 0; i < col_ntiles; i++)
        if (col_tiles[i].data == data)
            return 1;
    if (col_ntiles == COL_MAX_TILES || col_locked_bytes + size > COL_LOCK_BUDGET)
        return 0;
    col_locked_bytes += size;

    // Locked (>= 200) so that no tile loaded later in the frame evicts it
    // from BUILD's cache before the RDP reads it.
    col_tiles[col_ntiles].data = data;
    col_tiles[col_ntiles].lock = lock;
    col_tiles[col_ntiles].saved_lock = *lock;
    *lock = 255;
    col_ntiles++;

    // (tiles.c writes tiles back from the CPU cache when it loads them)
    return 1;
}

// Fills palette slot i: the screen palette seen through its shade table.
static void col_tlut_build(int i)
{
    uint16_t *buf = col_tluts + i * 256;
    const uint8_t *palrow = col_tlut_src[i];
    int j;

    for (j = 0; j < 256; j++)
        buf[j] = tlut_cpu[palrow[j]];
    data_cache_hit_writeback(buf, 256 * sizeof(uint16_t));
}

// The palette slot of a shade table (a row of BUILD's palookup).
static const uint16_t *col_tlut_for(const uint8_t *palrow)
{
    int i;

    for (i = 0; i < col_ntluts; i++)
        if (col_tlut_src[i] == palrow)
            return col_tluts + i * 256;
    // (A frame with more shade tables reuses the last one: slightly wrong
    // shades, never seen in practice.)
    i = (col_ntluts < COL_MAX_TLUTS) ? col_ntluts++ : COL_MAX_TLUTS - 1;
    col_tlut_src[i] = palrow;
    col_tlut_build(i);
    col_tlut_version = palette_version;
    return col_tluts + i * 256;
}

#ifdef N64_PROFILE
int n64_cols_added;
static void n64p_cols_add(int sx, int y1, int y2, const uint8_t *tile, int tile_h, int tile_w,
                  int col, int logw, uint32_t vplce, int32_t vince, int shift,
                  const uint8_t *palrow);
void n64_cols_add(int sx, int y1, int y2, const uint8_t *tile, int tile_h, int tile_w,
                  int col, int logw, uint32_t vplce, int32_t vince, int shift,
                  const uint8_t *palrow)
{
    PROF_BEGIN(PROF_COLADD);
    n64p_cols_add(sx, y1, y2, tile, tile_h, tile_w, col, logw, vplce, vince, shift, palrow);
    PROF_END(PROF_COLADD);
    n64_cols_added++;
}
#define n64_cols_add n64p_cols_add
static
#endif
void n64_cols_add(int sx, int y1, int y2, const uint8_t *tile, int tile_h, int tile_w,
                  int col, int logw, uint32_t vplce, int32_t vince, int shift,
                  const uint8_t *palrow)
{
    uint64_t *w = col_lw;
    int32_t ds, sfx;

    // Hole pass.
    *col_pw++ = rdp_coords(0x36, sx << 2, (y2 - 1) << 2, 0, sx << 2, y1 << 2);

    // Textured pass: a 1 pixel wide flipped texture rectangle over the tile
    // column, with the column's shade table as palette.
    if (palrow != col_cur_pal)
    {
        // The palette goes to the upper half of TMEM through tile 7. (A load
        // also rewrites its tile's size: on the real RDP, the primitive before
        // may still be reading the tiles, hence SYNC_TILE as well as
        // SYNC_LOAD. Emulators run commands one at a time and never show it.)
        *w++ = RDP_SYNC_LOAD;
        *w++ = RDP_SYNC_TILE;
        *w++ = rdp_image(0x3D, 0 /* RGBA */, 2 /* 16 bpp */, 256, col_tlut_for(palrow));
        *w++ = rdp_set_tile(4, 0, 0, 0x100, 7, 0);
        *w++ = rdp_coords(0x30, 0, 0, 7, 255 << 2, 0);        // LOAD_TLUT
        col_cur_pal = palrow;
        col_cur_tile = NULL;    // (its texture image is ours to set again)
    }
    if (tile != col_cur_tile)
    {
        // The tile seen as an image with one row per tile column. The RDP
        // wants an 8 byte aligned base: the rest goes in the s offset.
        uintptr_t base = (uintptr_t)tile & ~7;
        col_off = (uintptr_t)tile - base;
        *w++ = rdp_image(0x3D, 2 /* CI */, 1 /* 8 bpp */, tile_h, (const void *)base);
        col_cur_tile = tile;
        col_cur_col = -1;
    }
    if (logw != col_cur_logw)
    {
        // 2^logw texels per TMEM row, wrapping like BUILD's shift.
        *w++ = RDP_SYNC_TILE;
        *w++ = rdp_set_tile(2 /* CI */, 1 /* 8 bpp */, (1 << logw) >> 3, 0, 0, logw);
        col_cur_logw = logw;
        col_cur_col = -1;
    }
    if (col_cur_col < 0 || col < col_cur_col || col >= col_cur_col + col_cur_rows)
    {
        // Load as many consecutive columns as fit in the 2 KB of TMEM left
        // by the palette. Past the end of a column the load goes on into the
        // next one, as BUILD reads it when the height is not a power of two.
        col_cur_rows = 2048 >> logw;
        if (col + col_cur_rows > tile_w)
            col_cur_rows = (tile_w - col > 1) ? tile_w - col : 1;
        *w++ = RDP_SYNC_LOAD;
        *w++ = RDP_SYNC_TILE;       // the load rewrites tile 0's size (see above)
        *w++ = rdp_coords(0x34, col_off << 2, col << 2, 0,
                          (col_off + (1 << logw) - 1) << 2, (col + col_cur_rows - 1) << 2);
        col_cur_col = col;
    }

    // Same texel stepping as vlineasm (texel = vplce >> shift): s in 10.5
    // fixed point down the column, ds in 5.10.
    sfx = (col_off << 5) + (int32_t)(vplce >> (shift - 5));
    ds = vince >> (shift - 10);
    if (ds > 32767) ds = 32767;
    if (ds < -32768) ds = -32768;
    *w++ = rdp_coords(0x25, (sx + 1) << 2, y2 << 2, 0, sx << 2, y1 << 2);
    *w++ = ((uint64_t)(uint16_t)sfx << 48) | ((uint64_t)(uint16_t)(col << 5) << 32) |
           ((uint64_t)(uint16_t)ds << 16);
    col_lw = w;
}

void n64_cols_end(void)
{
    int n;

    cols_on = 0;
    if (!col_pw)
        return;
    PROF_BEGIN(PROF_COLEND);

    // rdpq does not know what the textured pass does: it leaves the pipe,
    // the loads and the tiles synced for the commands rdpq sends next.
    *col_lw++ = RDP_SYNC_PIPE;
    *col_lw++ = RDP_SYNC_LOAD;
    *col_lw++ = RDP_SYNC_TILE;
    data_cache_hit_writeback(col_list, (col_lw - col_list) * sizeof(uint64_t));

    // The RDP writes the frame: nothing of it may stay in the CPU cache.
    data_cache_hit_writeback_invalidate(framebuffer8, N64_SCREEN_PITCH * N64_SCREEN_H);

    *col_pw++ = RDP_SYNC_PIPE;
    n = (col_pw - col_punch) * sizeof(uint64_t);
    data_cache_hit_writeback(col_punch, n);
    rdpq_exec(col_punch, n);

    // The CPU draws sprites and HUD over the holes next.
    rspq_wait();
    PROF_END(PROF_COLEND);
}

/*
 * ------------------------------------------------------------------------
 *  2D sprites drawn by the RDP over the frame (see n64_overlay.h)
 * ------------------------------------------------------------------------
 */

#define OVL_MAX_ITEMS   768
#define OVL_MAX_TILES   256
#define OVL_MAX_TLUTS   16
#define OVL_MAX_STRIPS  2048
#define OVL_LOCK_BUDGET (384 * 1024)    // see COL_LOCK_BUDGET

typedef struct {
    float x0, y0, scale_x, scale_y;
    const uint8_t *tile;
    const uint8_t *pal;
    int16_t tile_w, tile_h;
    int16_t cx1, cy1, cx2, cy2;
    int16_t strip0, nstrips;            // clip rectangles in ovl_strips (0: the box above)
    uint8_t flags;
} ovl_item_t;

static ovl_item_t ovl_items[OVL_MAX_ITEMS];
static int ovl_count = 0;
static struct { const uint8_t *data; uint8_t *lock; uint8_t saved_lock; } ovl_tiles[OVL_MAX_TILES];
static int ovl_ntiles = 0;
static int32_t ovl_locked_bytes = 0;
static int16_t ovl_strips[OVL_MAX_STRIPS][4];
static int ovl_nstrips = 0;
static int ovl_capture = 0;
static int ovl_new_frame = 1;          // the list belongs to a presented frame
static uint16_t *ovl_tluts = NULL;     // OVL_MAX_TLUTS palettes, uncached

void n64_overlay_capture(int on)
{
    ovl_capture += on ? 1 : -1;
    if (ovl_capture < 0)
        ovl_capture = 0;
}

int n64_overlay_capturing(void)
{
    return ovl_capture > 0 && video_ready;
}

// Forgets the recorded sprites and unlocks their tiles. The RDP must be done
// with them (callers run after n64_wait_rdp).
static void ovl_clear(void)
{
    int i;
    // (A lock the game changed meanwhile, e.g. an ANM buffer released at
    // the end of playanm, is left as the game set it.)
    for (i = ovl_ntiles - 1; i >= 0; i--)
        if (*ovl_tiles[i].lock == 255)
            *ovl_tiles[i].lock = ovl_tiles[i].saved_lock;
    ovl_ntiles = 0;
    ovl_locked_bytes = 0;
    ovl_count = 0;
    ovl_nstrips = 0;
}

int n64_overlay_add(float x0, float y0, float scale_x, float scale_y,
                    const uint8_t *tile, int tile_w, int tile_h, uint8_t *lock,
                    const uint8_t *palrow, int flags,
                    int cx1, int cy1, int cx2, int cy2)
{
    ovl_item_t *it;
    int i;

    n64_wait_rdp();
    if (ovl_new_frame)
    {
        // First sprite of a new frame: the previous frame's list is done.
        ovl_clear();
        ovl_new_frame = 0;
    }

    // Texture loads: a tile column must fit in TMEM with its palette, and
    // coordinates stay below 1024.
    if (tile_w <= 0 || tile_h <= 0 || tile_w >= 1024 || tile_h > 1016 || !tile)
        return 0;
    if (ovl_count == OVL_MAX_ITEMS || cx2 <= cx1 || cy2 <= cy1)
        return 0;

    for (i = 0; i < ovl_ntiles; i++)
        if (ovl_tiles[i].data == tile)
            break;
    if (i == ovl_ntiles)
    {
        if (ovl_ntiles == OVL_MAX_TILES || ovl_locked_bytes + tile_w * tile_h > OVL_LOCK_BUDGET)
            return 0;
        ovl_locked_bytes += tile_w * tile_h;
        // Locked (>= 200) so that no tile loaded later evicts it from
        // BUILD's cache before the RDP draws it.
        ovl_tiles[i].data = tile;
        ovl_tiles[i].lock = lock;
        ovl_tiles[i].saved_lock = *lock;
        *lock = 255;
        ovl_ntiles++;
    }

    it = &ovl_items[ovl_count++];
    it->x0 = x0; it->y0 = y0; it->scale_x = scale_x; it->scale_y = scale_y;
    it->tile = tile; it->tile_w = tile_w; it->tile_h = tile_h;
    it->pal = palrow; it->flags = flags;
    it->cx1 = cx1; it->cy1 = cy1; it->cx2 = cx2; it->cy2 = cy2;
    it->strip0 = 0; it->nstrips = 0;
    return 1;
}

void n64_overlay_flush(const void *data, int bytes)
{
    data_cache_hit_writeback(data, bytes);
}

int n64_overlay_add_strips(float x0, float y0, float scale_x, float scale_y,
                           const uint8_t *tile, int tile_w, int tile_h, uint8_t *lock,
                           const uint8_t *palrow, int flags,
                           const int16_t *strips, int nstrips)
{
    int cx1 = 32767, cy1 = 32767, cx2 = -32768, cy2 = -32768;
    int i, first;

    if (nstrips <= 0 || nstrips > N64_OVL_MAX_STRIPS)
        return 0;
    for (i = 0; i < nstrips; i++)
    {
        const int16_t *r = strips + i * 4;
        if (r[0] < cx1) cx1 = r[0];
        if (r[1] < cy1) cy1 = r[1];
        if (r[2] > cx2) cx2 = r[2];
        if (r[3] > cy2) cy2 = r[3];
    }
    // (n64_overlay_add may start a new list: count the free strips after it.)
    if (!n64_overlay_add(x0, y0, scale_x, scale_y, tile, tile_w, tile_h, lock,
                         palrow, flags, cx1, cy1, cx2, cy2))
        return 0;
    if (ovl_nstrips + nstrips > OVL_MAX_STRIPS)
    {
        ovl_count--;        // (its tile stays locked until the list is cleared)
        return 0;
    }
    first = ovl_nstrips;
    memcpy(ovl_strips[first], strips, nstrips * 4 * sizeof(int16_t));
    ovl_nstrips += nstrips;
    ovl_items[ovl_count - 1].strip0 = first;
    ovl_items[ovl_count - 1].nstrips = nstrips;
    return 1;
}

// Screen palette seen through a shade table, with index 255 transparent
// when the sprite is masked.
static const uint16_t *ovl_tlut_for(const uint8_t *palrow, int masked,
                                    const uint8_t **srcs, uint8_t *srcmask, int *count)
{
    uint16_t *buf;
    int i, j;

    for (i = 0; i < *count; i++)
        if (srcs[i] == palrow && srcmask[i] == masked)
            return ovl_tluts + i * 256;
    i = (*count < OVL_MAX_TLUTS) ? (*count)++ : OVL_MAX_TLUTS - 1;

    buf = ovl_tluts + i * 256;
    for (j = 0; j < 256; j++)
        buf[j] = tlut_cpu[palrow[j]];
    if (masked)
        buf[255] = 0;
    srcs[i] = palrow;
    srcmask[i] = masked;
    return buf;
}

static void draw_overlay(void)
{
    if (!ovl_tluts)
        ovl_tluts = malloc_uncached(OVL_MAX_TLUTS * 256 * sizeof(uint16_t));

    const uint8_t *tlut_srcs[OVL_MAX_TLUTS];
    uint8_t tlut_masked[OVL_MAX_TLUTS];
    int ntluts = 0;
    const uint16_t *cur_tlut = NULL;
    int cur_translucent = -1;
    int i;

    rdpq_set_mode_standard();
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_mode_filter(FILTER_POINT);
    rdpq_mode_alphacompare(1);          // drops the transparent (alpha 0) texels
    rdpq_set_prim_color(RGBA32(255, 255, 255, 128));

    for (i = 0; i < ovl_count; i++)
    {
        const ovl_item_t *it = &ovl_items[i];
        const uint16_t *tl;
        int translucent = (it->flags & N64_OVL_TRANSLUCENT) != 0;
        int pitch = (it->tile_h + 7) & ~7;
        int rows = 2048 / pitch;                // tile columns per TMEM load
        uintptr_t base = (uintptr_t)it->tile & ~7;
        int off = (uintptr_t)it->tile - base;
        float ds = 1.0f / it->scale_y, dt = 1.0f / it->scale_x;
        float s0 = off;
        int flip_x = (it->flags & N64_OVL_FLIP_X) != 0;
        int nrects = it->nstrips ? it->nstrips : 1;
        int c0, k;

        if (translucent != cur_translucent)
        {
            if (translucent)
            {
                // Approximates BUILD's translucency table with a 50% blend.
                rdpq_mode_combiner(RDPQ_COMBINER1((0,0,0,TEX0), (TEX0,0,PRIM,0)));
                rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            }
            else
            {
                rdpq_mode_combiner(RDPQ_COMBINER_TEX);
                rdpq_mode_blender(0);
            }
            cur_translucent = translucent;
        }

        tl = ovl_tlut_for(it->pal, (it->flags & N64_OVL_MASKED) != 0, tlut_srcs, tlut_masked, &ntluts);
        if (tl != cur_tlut)
        {
            rdpq_tex_upload_tlut((uint16_t *)tl, 0, 256);
            cur_tlut = tl;
        }

        if (it->flags & N64_OVL_FLIP_Y)
        {
            s0 = off + it->tile_h;
            ds = -ds;
        }

        rdpq_set_scissor(it->cx1, it->cy1, it->cx2, it->cy2);
        // The palette load sets its own texture image: set the tile's after it.
        rdpq_set_texture_image_raw(0, PhysicalAddr((void *)base), FMT_CI8, it->tile_h, it->tile_w + 1);
        rdpq_set_tile(TILE0, FMT_CI8, 0, pitch, &(rdpq_tileparms_t){ .s.clamp = true, .t.clamp = true });

        if (it->flags & N64_OVL_TRANSPOSED)
        {
            // Row-major image (an ANM frame): each tile column is a screen
            // row, so a plain rectangle walks s across and t down.
            float dsx = 1.0f / it->scale_x, dty = 1.0f / it->scale_y;
            for (c0 = 0; c0 < it->tile_w; c0 += rows)
            {
                int c1 = c0 + rows < it->tile_w ? c0 + rows : it->tile_w;
                float x0 = it->x0, x1 = it->x0 + it->tile_h * it->scale_x;
                float y0 = it->y0 + c0 * it->scale_y, y1 = it->y0 + c1 * it->scale_y;
                float s = off, t = c0;

                if (x0 < it->cx1) { s += (it->cx1 - x0) * dsx; x0 = it->cx1; }
                if (x1 > it->cx2) x1 = it->cx2;
                if (y0 < it->cy1) { t += (it->cy1 - y0) * dty; y0 = it->cy1; }
                if (y1 > it->cy2) y1 = it->cy2;
                if (x1 <= x0 || y1 <= y0)
                    continue;
                rdpq_load_tile(TILE0, off, c0, off + it->tile_h, c1);
                rdpq_texture_rectangle_raw(TILE0, x0, y0, x1, y1, s, t, dsx, dty);
            }
            continue;
        }

        for (c0 = 0; c0 < it->tile_w; c0 += rows)
        {
            int c1 = c0 + rows < it->tile_w ? c0 + rows : it->tile_w;
            float gx0, gx1, gt, gdt;
            int loaded = 0;

            if (!flip_x)
            {
                gx0 = it->x0 + c0 * it->scale_x;
                gx1 = it->x0 + c1 * it->scale_x;
                gt = c0;
                gdt = dt;
            }
            else
            {
                // Mirrored: t walks down from the group's last column (the
                // clamp keeps the edge on it).
                gx0 = it->x0 + (it->tile_w - c1) * it->scale_x;
                gx1 = it->x0 + (it->tile_w - c0) * it->scale_x;
                gt = c1 - 0.01f;
                gdt = -dt;
            }

            for (k = 0; k < nrects; k++)
            {
                const int16_t *r = it->nstrips ? ovl_strips[it->strip0 + k] : &it->cx1;
                float x0 = gx0, x1 = gx1;
                float y0 = it->y0;
                float y1 = it->y0 + it->tile_h * it->scale_y;
                float s = s0, t = gt;

                // Clip to the rectangle here (rectangle coordinates cannot be
                // negative), moving the texture coordinates along.
                if (x0 < r[0]) { t += (r[0] - x0) * gdt; x0 = r[0]; }
                if (x1 > r[2]) x1 = r[2];
                if (y0 < r[1]) { s += (r[1] - y0) * ds; y0 = r[1]; }
                if (y1 > r[3]) y1 = r[3];
                if (x1 <= x0 || y1 <= y0)
                    continue;

                // One row of TMEM per tile column (BUILD's tiles are column-major):
                // the flipped rectangle walks s down a column and t across columns.
                if (!loaded)
                {
                    rdpq_load_tile(TILE0, off, c0, off + it->tile_h, c1);
                    loaded = 1;
                }
                rdpq_texture_rectangle_flip_raw(TILE0, x0, y0, x1, y1, s, t, ds, gdt);
            }
        }
    }
    rdpq_set_scissor(0, 0, N64_SCREEN_W, N64_SCREEN_H);
}

/*
 * Main menu background of an add-on ROM: its picture (rom:/menubg.sprite,
 * see the Makefile), drawn by the RDP under the 8-bit frame, whose holes
 * (N64_HOLE_INDEX) show it. Dimmed with the palette fades, as the frame is.
 */
static sprite_t *menubg = NULL;
static int menubg_state = 0;            // 0: not looked for yet, 1: loaded, -1: none
// Behind the frames presented while the 8-bit frame keeps its holes: a fade
// (palto) presents the same frame again and again without drawing the menu.
static int menubg_frame = 0;
static float menubg_fade = 1.0f;        // brightness of the palette (VBE_setPalette)

int n64_menu_background(void)
{
    if (!video_ready)
        return 0;
    if (menubg_state == 0)
    {
        menubg = access("rom:/menubg.sprite", F_OK) == 0 ? sprite_load("rom:/menubg.sprite") : NULL;
        menubg_state = menubg ? 1 : -1;
    }
    if (menubg_state < 0)
        return 0;
    n64_wait_rdp();   // the RDP may still be reading the last frame
    memset(framebuffer8, N64_HOLE_INDEX, N64_SCREEN_PITCH * N64_SCREEN_H);
    menubg_frame = 1;
    return 1;
}

/*
 * Or a BUILD tile with its own palette (the title screen, BETASCREEN, is
 * drawn with titlepal: through the game's palette it came out in false
 * colours). Turned once into a row-major image and an RGBA16 palette.
 */
static const uint8_t *menubg_tile_src = NULL;
static uint8_t *menubg_pixels = NULL;   // 320x200, row-major
static uint16_t *menubg_tlut = NULL;    // uncached
static surface_t menubg_surface;

int n64_menu_background_tile(const uint8_t *tile, int w, int h, const uint8_t *pal768)
{
    int x, y, i;

    if (!video_ready || !tile || w != N64_SCREEN_W || h != N64_SCREEN_H)
        return 0;
    n64_wait_rdp();   // the RDP may still be reading the last frame
    if (!menubg_pixels)
    {
        menubg_pixels = memalign(64, N64_SCREEN_W * N64_SCREEN_H);
        menubg_tlut = malloc_uncached(256 * sizeof(uint16_t));
        if (!menubg_pixels || !menubg_tlut)
            return 0;
        menubg_surface = surface_make(menubg_pixels, FMT_CI8, N64_SCREEN_W, N64_SCREEN_H, N64_SCREEN_W);
    }
    if (tile != menubg_tile_src)
    {
        // Tiles are column-major.
        for (x = 0; x < w; x++)
            for (y = 0; y < h; y++)
                menubg_pixels[y * w + x] = tile[x * h + y];
        data_cache_hit_writeback(menubg_pixels, w * h);
        for (i = 0; i < 256; i++)       // 6-bit VGA components
            menubg_tlut[i] = ((pal768[i*3+0] >> 1) << 11) | ((pal768[i*3+1] >> 1) << 6) |
                             ((pal768[i*3+2] >> 1) << 1) | 1;
        menubg_tile_src = tile;
    }
    memset(framebuffer8, N64_HOLE_INDEX, N64_SCREEN_PITCH * N64_SCREEN_H);
    menubg_frame = 2;
    return 1;
}

static void draw_menubg(void)
{
    uint8_t f = (uint8_t)(menubg_fade * 255.0f);

    rdpq_set_mode_standard();
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_mode_combiner(RDPQ_COMBINER1((TEX0,0,PRIM,0), (0,0,0,1)));
    rdpq_set_prim_color(RGBA32(f, f, f, 255));
    if (menubg_frame == 2)
    {
        rdpq_tex_upload_tlut(menubg_tlut, 0, 256);
        rdpq_tex_blit(&menubg_surface, 0, 0, NULL);
    }
    else
    {
        rdpq_tex_upload_tlut(sprite_get_palette(menubg), 0, 256);
        rdpq_sprite_blit(menubg, 0, 0, NULL);
    }
}

// Screen tilt of the frame being drawn (n64_view_tilt), 0 = none.
static int tilt_ang = 0;
static int tilt_x1, tilt_y1, tilt_x2, tilt_y2;

void n64_view_tilt(int ang, int x1, int y1, int x2, int y2)
{
    tilt_ang = ang & 2047;
    tilt_x1 = x1; tilt_y1 = y1; tilt_x2 = x2; tilt_y2 = y2;
}

// Draws the view window of the 8-bit frame again, rotated about its centre
// and zoomed just enough for the rotated picture to cover the window.
static void draw_tilted_view(void)
{
    int w = tilt_x2 - tilt_x1 + 1, h = tilt_y2 - tilt_y1 + 1;
    float theta = tilt_ang * (2.0f * (float)M_PI / 2048.0f);
    float c = fabsf(cosf(theta)), s = fabsf(sinf(theta));
    float zoom = c + (w > h ? (float)w / h : (float)h / w) * s;

    if (w <= 0 || h <= 0)
        return;
    rdpq_set_mode_standard();
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_mode_filter(FILTER_POINT);
    rdpq_tex_upload_tlut(tlut, 0, 256);
    rdpq_set_scissor(tilt_x1, tilt_y1, tilt_x2 + 1, tilt_y2 + 1);
    rdpq_tex_blit(&framebuffer8_surface, tilt_x1 + w / 2, tilt_y1 + h / 2, &(rdpq_blitparms_t){
        .s0 = tilt_x1, .t0 = tilt_y1, .width = w, .height = h,
        .cx = w / 2, .cy = h / 2,
        .scale_x = zoom, .scale_y = zoom,
        .theta = theta,
    });
    rdpq_set_scissor(0, 0, N64_SCREEN_W, N64_SCREEN_H);
}

int n64_video_ready(void)
{
    return video_ready;
}

static void present_frame(void)
{
    n64_wait_rdp();   // the RDP may still be reading the last frame

    surface_t *disp;

    if (!video_ready)
        return;

    // BUILD wrote the frame through the CPU cache: flush it so the RDP sees it.
    data_cache_hit_writeback(framebuffer8, N64_SCREEN_PITCH * N64_SCREEN_H);

    PROF_BEGIN(PROF_PRESENT);
    disp = display_get();
    if (col_lw)
        rdpq_attach_clear(disp, NULL);  // (holes without a column show black)
    else
        rdpq_attach(disp, NULL);
    if ((menubg_frame == 1 && menubg) || menubg_frame == 2)
        draw_menubg();
    if (col_lw || (menubg_frame == 1 && menubg) || menubg_frame == 2)
    {
        // Walls and sky first, then the frame on top with the holes
        // transparent. (A palette change since they were recorded, e.g. a
        // damage flash, rebuilds their shaded palettes.)
        PROF_BEGIN(PROF_SKYDRAW);
        if (col_tlut_version != palette_version)
        {
            int i;
            for (i = 0; i < col_ntluts; i++)
                col_tlut_build(i);
            col_tlut_version = palette_version;
        }
        if (col_lw)
            rdpq_exec(col_list, (col_lw - col_list) * sizeof(uint64_t));
        PROF_END(PROF_SKYDRAW);
        rdpq_set_mode_copy(true);
        rdpq_mode_tlut(TLUT_RGBA16);
        rdpq_tex_upload_tlut(tlut_keyed, 0, 256);
    }
    else
    {
        rdpq_set_mode_copy(false);
        rdpq_mode_tlut(TLUT_RGBA16);
        rdpq_tex_upload_tlut(tlut, 0, 256);
    }
    rdpq_tex_blit(&framebuffer8_surface, 0, 0, NULL);
    if (tilt_ang != 0)
        draw_tilted_view();
    if (ovl_count > 0)
        draw_overlay();
    rdpq_detach_show();

    // BUILD draws the next frame into the same buffer, but not right away:
    // the game logic runs first. Instead of waiting for the RDP here, the
    // code that writes the buffer, the palettes or the wall tiles waits for
    // it (n64_wait_rdp), usually when it is long done.
    rdp_done = 0;
    rdpq_sync_full(rdp_done_callback, NULL);
    rspq_flush();
    PROF_END(PROF_PRESENT);
}

void n64_wait_rdp(void)
{
    uint64_t start = get_ticks_ms();

    while (!rdp_done)
    {
        if (get_ticks_ms() - start > 1000)
        {
            debugf("n64_wait_rdp: RDP did not finish the frame in 1 s\n");
            rdp_done = 1;
        }
    }
}

int _setgamemode(uint8_t  davidoption, int32_t daxdim, int32_t daydim)
{
    init_video();
    getvalidvesamodes();
    init_new_res_vars();

    qsetmode = 200;
    last_render_ticks = getticks();

    return(0);
}

void getvalidvesamodes(void)
{
    static int already_checked = 0;

    if (already_checked)
        return;

    already_checked = 1;
    vidoption = 1;
    validmodecnt = 1;
    validmode[0] = 0;
    validmodexdim[0] = N64_SCREEN_W;
    validmodeydim[0] = N64_SCREEN_H;
}

void setvmode(int mode)
{
    /* Text mode (0x3) and others: nothing to do on the N64. */
}

int VBE_setPalette(uint8_t  *palettebuffer)
/*
 * (From Ken's docs:)
 *   palette entries are in a 4-byte format in this order:
 *       0: Blue (0-63)
 *       1: Green (0-63)
 *       2: Red (0-63)
 *       3: Reserved
 */
{
    uint8_t *p = palettebuffer;
    int i;

    n64_wait_rdp();   // the RDP may still be reading the last frame

    memcpy(vgaPalette, palettebuffer, sizeof(vgaPalette));
    palette_version++;

    // How bright the palette is next to PALETTE.DAT: the fades (palto) dim
    // the menu background picture the same way.
    {
        int32_t cur = 0, base = 0;
        for (i = 0; i < 256; i++)
            cur += p[i*4+0] + p[i*4+1] + p[i*4+2];
        for (i = 0; i < 768; i++)
            base += lastPalette[i];
        menubg_fade = base > 0 ? (float)cur / (float)base : 1.0f;
        if (menubg_fade > 1.0f) menubg_fade = 1.0f;
    }

    for (i = 0; i < 256; i++, p += 4)
    {
        // 6-bit VGA components to RGBA5551, alpha always set.
        tlut_cpu[i] = ((p[2] >> 1) << 11) | ((p[1] >> 1) << 6) | ((p[0] >> 1) << 1) | 1;
        tlut[i] = tlut_cpu[i];
        tlut_keyed[i] = tlut_cpu[i];
    }
    tlut_keyed[N64_HOLE_INDEX] = 0;     // alpha 0: the columns drawn below show through

    // On a VGA card a new palette shows up at once on the displayed picture,
    // and the game relies on it for fades: palto() in a loop with no frame
    // drawn in between (e.g. the "ENTERING <level>" screen fading in). Here the
    // palette is only applied when a frame is sent to the screen, so when the
    // palette changes a second time without a new frame, show the current
    // frame again. A single change per frame (damage/pickup flashes) waits
    // for the next _nextpage() as usual and costs nothing.
    if (++palette_changes_since_frame >= 2)
    {
        n64_audio_poll();
        present_frame();
    }

    return 1;
}

int VBE_getPalette(int32_t start, int32_t num, uint8_t  *palettebuffer)
{
    memcpy(palettebuffer + start * 4, vgaPalette + start * 4, num * 4);
    return(1);
}

int screencapture(char  *filename, uint8_t  inverseit)
{
    return 0;
}

void _uninitengine(void)
{
}

void _updateScreenRect(int32_t x, int32_t y, int32_t w, int32_t h)
{
    present_frame();
}

void _nextpage(void)
{
    uint32_t ticks;

    _handle_events();
    n64_wait_rdp();
    if (ovl_new_frame)
        ovl_clear();        // no 2D sprites were recorded for this frame
    present_frame();
    ovl_new_frame = 1;      // the next sprite recorded starts a new list
    tilt_ang = 0;           // set again by each frame that tilts
    palette_changes_since_frame = 0;
    n64_audio_poll();
#ifdef N64_PROFILE
    profile_next_frame();   // libdragon's profiler (see n64_early_init)
#endif

    ticks = getticks();
    total_render_time = (ticks - last_render_ticks);
    if (total_render_time > 1000){
        debugf("fps: %ld\n", (long)total_rendered_frames);
#ifdef N64_PROFILE
        prof_report(total_rendered_frames);
        n64_audio_stats();

#endif
        total_rendered_frames = 0;
        total_render_time = 1;
        last_render_ticks = ticks;
    }
    total_rendered_frames++;
}

void *_getVideoBase(void)
{
    return framebuffer8;
}

uint8_t  readpixel(uint8_t  * offset)
{
    return *offset;
}

void drawpixel(uint8_t  * location, uint8_t  pixel)
{
    *location = pixel;
}

/* Fix this up The Right Way (TM) - DDOI */
void setcolor16(uint8_t col)
{
    drawpixel_color = col;
}

void drawpixel16(int32_t offset)
{
    n64_wait_rdp();   // the RDP may still be reading the last frame

    drawpixel(framebuffer8 + offset, drawpixel_color);
}

void fillscreen16(int32_t offset, int32_t color, int32_t blocksize)
{
    n64_wait_rdp();   // the RDP may still be reading the last frame

    uint8_t *surface_end;
    uint8_t *wanted_end;
    uint8_t *pixels = framebuffer8;

    /* Make this function pageoffset aware - DDOI */
    if (!pageoffset) {
        offset = offset << 3;
        offset += 640*336;
    }

    surface_end = (pixels + (N64_SCREEN_PITCH * N64_SCREEN_H)) - 1;
    wanted_end = (pixels + offset) + blocksize;

    if (offset < 0)
        offset = 0;

    if (wanted_end > surface_end)
        blocksize = ((uint32_t) surface_end) - ((uint32_t) pixels + offset);

    if (blocksize > 0)
        memset(pixels + offset, (int) color, blocksize);

    _nextpage();
}

/* Most of this line code is taken from Abrash's "Graphics Programming Blackbook".
Remember, sharing code is A Good Thing. AH */
static __inline void DrawHorizontalRun (uint8_t  **ScreenPtr, int XAdvance, int RunLength, uint8_t  Color)
{
    int i;
    uint8_t  *WorkingScreenPtr = *ScreenPtr;

    for (i=0; i<RunLength; i++)
    {
        *WorkingScreenPtr = Color;
        WorkingScreenPtr += XAdvance;
    }
    WorkingScreenPtr += N64_SCREEN_PITCH;
    *ScreenPtr = WorkingScreenPtr;
}

static __inline void DrawVerticalRun (uint8_t  **ScreenPtr, int XAdvance, int RunLength, uint8_t  Color)
{
    int i;
    uint8_t  *WorkingScreenPtr = *ScreenPtr;

    for (i=0; i<RunLength; i++)
    {
        *WorkingScreenPtr = Color;
        WorkingScreenPtr += N64_SCREEN_PITCH;
    }
    WorkingScreenPtr += XAdvance;
    *ScreenPtr = WorkingScreenPtr;
}

void drawline16(int32_t XStart, int32_t YStart, int32_t XEnd, int32_t YEnd, uint8_t  Color)
{
    n64_wait_rdp();   // the RDP may still be reading the last frame

    int Temp, AdjUp, AdjDown, ErrorTerm, XAdvance, XDelta, YDelta;
    int WholeStep, InitialPixelCount, FinalPixelCount, i, RunLength;
    uint8_t  *ScreenPtr;
    int32_t dx, dy;

    dx = XEnd-XStart;
    dy = YEnd-YStart;

    // The 2D mode clips against the 640 wide BUILD editor screen; the N64
    // buffer is only 320x200 so clip against that instead.
    if (dx >= 0)
    {
        if ((XStart > N64_SCREEN_W-1) || (XEnd < 0)) return;
        if (XStart < 0) { if (dy) YStart += scale(0-XStart,dy,dx); XStart = 0; }
        if (XEnd > N64_SCREEN_W-1) { if (dy) YEnd += scale(N64_SCREEN_W-1-XEnd,dy,dx); XEnd = N64_SCREEN_W-1; }
    }
    else
    {
        if ((XEnd > N64_SCREEN_W-1) || (XStart < 0)) return;
        if (XEnd < 0) { if (dy) YEnd += scale(0-XEnd,dy,dx); XEnd = 0; }
        if (XStart > N64_SCREEN_W-1) { if (dy) YStart += scale(N64_SCREEN_W-1-XStart,dy,dx); XStart = N64_SCREEN_W-1; }
    }
    if (dy >= 0)
    {
        if ((YStart >= N64_SCREEN_H) || (YEnd < 0)) return;
        if (YStart < 0) { if (dx) XStart += scale(0-YStart,dx,dy); YStart = 0; }
        if (YEnd >= N64_SCREEN_H) { if (dx) XEnd += scale(N64_SCREEN_H-1-YEnd,dx,dy); YEnd = N64_SCREEN_H-1; }
    }
    else
    {
        if ((YEnd >= N64_SCREEN_H) || (YStart < 0)) return;
        if (YEnd < 0) { if (dx) XEnd += scale(0-YEnd,dx,dy); YEnd = 0; }
        if (YStart >= N64_SCREEN_H) { if (dx) XStart += scale(N64_SCREEN_H-1-YStart,dx,dy); YStart = N64_SCREEN_H-1; }
    }

    /* We'll always draw top to bottom */
    if (YStart > YEnd) {
        Temp = YStart;
        YStart = YEnd;
        YEnd = Temp;
        Temp = XStart;
        XStart = XEnd;
        XEnd = Temp;
    }

    /* Point to the bitmap address first pixel to draw */
    ScreenPtr = framebuffer8 + XStart + (N64_SCREEN_PITCH * YStart);

    /* Figure out whether we're going left or right, and how far we're going horizontally */
    if ((XDelta = XEnd - XStart) < 0)
    {
        XAdvance = (-1);
        XDelta = -XDelta;
    } else {
        XAdvance = 1;
    }

    /* Figure out how far we're going vertically */
    YDelta = YEnd - YStart;

    /* Special cases: Horizontal, vertical, and diagonal lines */
    if (XDelta == 0)
    {
        for (i=0; i <= YDelta; i++)
        {
            *ScreenPtr = Color;
            ScreenPtr += N64_SCREEN_PITCH;
        }
        return;
    }
    if (YDelta == 0)
    {
        for (i=0; i <= XDelta; i++)
        {
            *ScreenPtr = Color;
            ScreenPtr += XAdvance;
        }
        return;
    }
    if (XDelta == YDelta)
    {
        for (i=0; i <= XDelta; i++)
        {
            *ScreenPtr = Color;
            ScreenPtr += XAdvance + N64_SCREEN_PITCH;
        }
        return;
    }

    /* Determine whether the line is X or Y major, and handle accordingly */
    if (XDelta >= YDelta) /* X major line */
    {
        WholeStep = XDelta / YDelta;
        AdjUp = (XDelta % YDelta) * 2;
        AdjDown = YDelta * 2;
        ErrorTerm = (XDelta % YDelta) - (YDelta * 2);

        InitialPixelCount = (WholeStep / 2) + 1;
        FinalPixelCount = InitialPixelCount;

        if ((AdjUp == 0) && ((WholeStep & 0x01) == 0)) InitialPixelCount--;
        if ((WholeStep & 0x01) != 0) ErrorTerm += YDelta;

        DrawHorizontalRun(&ScreenPtr, XAdvance, InitialPixelCount, Color);

        for (i=0; i<(YDelta-1); i++)
        {
            RunLength = WholeStep;
            if ((ErrorTerm += AdjUp) > 0)
            {
                RunLength ++;
                ErrorTerm -= AdjDown;
            }

            DrawHorizontalRun(&ScreenPtr, XAdvance, RunLength, Color);
        }

        DrawHorizontalRun(&ScreenPtr, XAdvance, FinalPixelCount, Color);
    } else {	/* Y major line */
        WholeStep = YDelta / XDelta;
        AdjUp = (YDelta % XDelta) * 2;
        AdjDown = XDelta * 2;
        ErrorTerm = (YDelta % XDelta) - (XDelta * 2);
        InitialPixelCount = (WholeStep / 2) + 1;
        FinalPixelCount = InitialPixelCount;

        if ((AdjUp == 0) && ((WholeStep & 0x01) == 0)) InitialPixelCount --;
        if ((WholeStep & 0x01) != 0) ErrorTerm += XDelta;

        DrawVerticalRun(&ScreenPtr, XAdvance, InitialPixelCount, Color);

        for (i=0; i<(XDelta-1); i++)
        {
            RunLength = WholeStep;
            if ((ErrorTerm += AdjUp) > 0)
            {
                RunLength ++;
                ErrorTerm -= AdjDown;
            }

            DrawVerticalRun(&ScreenPtr, XAdvance, RunLength, Color);
        }

        DrawVerticalRun(&ScreenPtr, XAdvance, FinalPixelCount, Color);
    }
}

void clear2dscreen(void)
{
    n64_wait_rdp();   // the RDP may still be reading the last frame

    memset(framebuffer8, 0, N64_SCREEN_PITCH * N64_SCREEN_H);
}


/*
 * ------------------------------------------------------------------------
 *  Input: N64 controller -> PC scancodes
 * ------------------------------------------------------------------------
 *
 * Buttons are turned into the keys of Duke3D's default keyboard layout (see
 * _functio.h), so no DUKE3D.CFG is needed. There are two layouts:
 *
 *   Menus (and any screen outside the game):
 *     stick / D-pad   arrows          A      select (Space)
 *     B / Start       back (Escape)
 *
 *   In game:
 *     stick up/down   walk forward/back (Up/Down; B runs)
 *     stick left/right turn, analog (n64_stick_turn, added in getinput)
 *     Z               fire              R        open / use
 *     A               jump              B        run
 *     C-left/right    strafe            C-down   crouch
 *     C-up            center view       L        use inventory item
 *     R + C-up/down   aim up/down       D-pad left/right  previous/next weapon
 *     D-pad up/down   previous/next inventory item
 *     Start           menu
 *
 * Every poll computes the set of keys that should be down and sends the
 * press/release scancodes for the ones that changed, so switching layouts or
 * releasing the R modifier never leaves a key stuck.
 */

// Scancodes (extended ones in the 0xE0xx form).
#define SC_ESCAPE       0x01
#define SC_ENTER        0x1C
#define SC_LCTRL        0x1D
#define SC_A            0x1E
#define SC_SEMICOLON    0x27
#define SC_QUOTE        0x28
#define SC_LSHIFT       0x2A    // run
#define SC_C            0x2E    // quick kick
#define SC_Z            0x2C
#define SC_COMMA        0x33
#define SC_PERIOD       0x34
#define SC_SPACE        0x39
#define SC_LBRACKET     0x1A
#define SC_RBRACKET     0x1B
#define SC_KPAD5        0x4C
#define SC_HOME         0xE047
#define SC_UP           0xE048
#define SC_LEFT         0xE04B
#define SC_RIGHT        0xE04D
#define SC_END          0xE04F
#define SC_DOWN         0xE050

static const uint32_t all_keys[] = {
    SC_ESCAPE, SC_ENTER, SC_LCTRL, SC_A, SC_SEMICOLON, SC_QUOTE, SC_LSHIFT, SC_C, SC_Z,
    SC_COMMA, SC_PERIOD, SC_SPACE, SC_LBRACKET, SC_RBRACKET, SC_KPAD5,
    SC_HOME, SC_UP, SC_LEFT, SC_RIGHT, SC_END, SC_DOWN,
};
#define NUM_KEYS (sizeof(all_keys) / sizeof(all_keys[0]))

static uint8_t keys_down[NUM_KEYS];     // what the game currently sees

// Dead zone and practical maximum of the stick, out of its -128..127 range
// (real sticks rarely report past ~100), as in the N64 Doom port.
#define STICK_DEADZONE  32
#define STICK_MAX_RAW   100
// Turn rate (angvel per tic) at full deflection: a bit above the keyboard's
// running turn (2 * NORMALTURN = 30), so a hard push turns at least as fast.
#define STICK_TURN_MAX  40

static int stick_x = 0;                 // last polled stick position

static void set_key(uint8_t *want, uint32_t scancode)
{
    unsigned i;
    for (i = 0; i < NUM_KEYS; i++)
        if (all_keys[i] == scancode)
            want[i] = 1;
}

static void send_scancode(uint32_t scancode, int released)
{
    int extended = (scancode >> 8) & 0xFF;

    if (extended != 0)
    {
        lastkey = extended;
        keyhandler();
    }

    lastkey = scancode & 0xFF;
    if (released)
        lastkey += 128;  /* +128 signifies that the key is released in DOS. */

    keyhandler();
}

/*
 * Turn amount for the analog stick, in getinput()'s angvel units. Squared
 * response curve (from the N64 Doom port): small pushes turn slowly and
 * precisely, the rate ramps up towards the edge of the stick.
 */
int n64_stick_turn(void)
{
    int mag = abs(stick_x) - STICK_DEADZONE;
    int range = STICK_MAX_RAW - STICK_DEADZONE;
    int turn;

    if (mag <= 0 || n64_in_menu())
        return 0;
    if (mag > range)
        mag = range;

    turn = (mag * mag * STICK_TURN_MAX) / (range * range);
    return stick_x < 0 ? -turn : turn;
}

#ifdef N64_INPUT_SCRIPT
/*
 * Scripted controller input for testing without a player, baked in at build
 * time: `libdragon make INPUT="8000:A 9500:A 12000-14000:U"` presses A at 8 s and
 * 9.5 s and holds the stick up from 12 to 14 s (times since boot; a single
 * time presses for 150 ms). Buttons: A B Z L R S(tart), stick U D < >, C u d l r.
 */
static void apply_input_script(joypad_inputs_t *in)
{
    static const char script[] = N64_INPUT_SCRIPT;
    uint64_t now = get_ticks_ms();
    const char *p = script;

    while (*p)
    {
        char *end;
        unsigned long t = strtoul(p, &end, 10);
        unsigned long t2 = t + 150;

        if (end != p && *end == '-')    // "start-end:X" holds X for that span
            t2 = strtoul(end + 1, &end, 10);
        if (end == p || *end != ':' || !end[1])
            break;
        if (now >= t && now < t2)
        {
            switch (end[1])
            {
                case 'A': in->btn.a = 1; break;
                case 'B': in->btn.b = 1; break;
                case 'Z': in->btn.z = 1; break;
                case 'L': in->btn.l = 1; break;
                case 'R': in->btn.r = 1; break;
                case 'S': in->btn.start = 1; break;
                case 'U': in->stick_y = 80; break;
                case 'D': in->stick_y = -80; break;
                case '<': in->stick_x = -80; break;
                case '>': in->stick_x = 80; break;
                case 'u': in->btn.c_up = 1; break;
                case 'd': in->btn.c_down = 1; break;
                case 'l': in->btn.c_left = 1; break;
                case 'r': in->btn.c_right = 1; break;
            }
        }
        p = end + 2;
        while (*p == ' ')
            p++;
    }
}
#endif

void _handle_events(void)
{
    joypad_inputs_t in;
    uint8_t want[NUM_KEYS] = { 0 };
    unsigned i;

    if (!video_ready)
        return;

    joypad_poll();
    in = joypad_get_inputs(JOYPAD_PORT_1);
#ifdef N64_INPUT_SCRIPT
    apply_input_script(&in);
#endif
    stick_x = in.stick_x;

    if (n64_in_menu())
    {
        if (in.stick_y >  STICK_DEADZONE || in.btn.d_up)    set_key(want, SC_UP);
        if (in.stick_y < -STICK_DEADZONE || in.btn.d_down)  set_key(want, SC_DOWN);
        if (in.stick_x < -STICK_DEADZONE || in.btn.d_left)  set_key(want, SC_LEFT);
        if (in.stick_x >  STICK_DEADZONE || in.btn.d_right) set_key(want, SC_RIGHT);
        if (in.btn.a)                    set_key(want, SC_SPACE);
        if (in.btn.b || in.btn.start)    set_key(want, SC_ESCAPE);
    }
    else
    {
        if (in.stick_y >  STICK_DEADZONE) set_key(want, SC_UP);
        if (in.stick_y < -STICK_DEADZONE) set_key(want, SC_DOWN);
        if (in.btn.z)       set_key(want, SC_LCTRL);
        if (in.btn.r)       set_key(want, SC_SPACE);
        if (in.btn.a)       set_key(want, SC_A);
        if (in.btn.b)       set_key(want, in.btn.r ? SC_C : SC_LSHIFT);    // R+B: quick kick, B: run
        if (in.btn.l)       set_key(want, SC_ENTER);
        if (in.btn.c_left)  set_key(want, SC_COMMA);
        if (in.btn.c_right) set_key(want, SC_PERIOD);
        if (in.btn.r)
        {
            // R held: C-up/C-down aim up/down.
            if (in.btn.c_up)   set_key(want, SC_HOME);
            if (in.btn.c_down) set_key(want, SC_END);
        }
        else
        {
            if (in.btn.c_up)   set_key(want, SC_KPAD5);
            if (in.btn.c_down) set_key(want, SC_Z);
        }
        if (in.btn.d_left)  set_key(want, SC_SEMICOLON);
        if (in.btn.d_right) set_key(want, SC_QUOTE);
        if (in.btn.d_up)    set_key(want, SC_LBRACKET);
        if (in.btn.d_down)  set_key(want, SC_RBRACKET);
        if (in.btn.start)   set_key(want, SC_ESCAPE);
    }

    for (i = 0; i < NUM_KEYS; i++)
    {
        if (want[i] != keys_down[i])
        {
            keys_down[i] = want[i];
            send_scancode(all_keys[i], !want[i]);
        }
    }
}

uint8_t  _readlastkeyhit(void)
{
    return(lastkey);
}

void _idle(void)
{
    _handle_events();
    n64_audio_poll();
}

void initkeys(void)
{
}

void uninitkeys(void)
{
}

/* The analog stick is exposed through the keyboard mapping for now. */
void _joystick_init(void)
{
}

void _joystick_deinit(void)
{
}

int _joystick_update(void)
{
    return(0);
}

int _joystick_axis(int axis)
{
    return(0);
}

int _joystick_hat(int hat)
{
    return(-1);
}

int _joystick_button(int button)
{
    return(0);
}

int setupmouse(void)
{
    moustat = 0;
    return(0);
}

void readmousexy(short *x, short *y)
{
    if (x)
        *x = 0;
    if (y)
        *y = 0;
}

void readmousebstatus(short *bstatus)
{
    if (bstatus)
        *bstatus = 0;
}

void fullscreen_toggle_and_change_driver(void)
{
}


/*
 * ------------------------------------------------------------------------
 *  Platform init
 * ------------------------------------------------------------------------
 */

void _platform_init(int argc, char  **argv, const char  *title, const char  *iconName)
{
    int64_t timeElapsed;

    TIMER_GetPlatformTicks(&timeElapsed);
    srand(timeElapsed&0xFFFFFFFF);

    Setup_UnstableNetworking();

    joypad_init();
}


/*
 * ------------------------------------------------------------------------
 *  TIMER
 *  (Ken's timer logic from the SDL driver, fed by the N64 CPU counter.)
 * ------------------------------------------------------------------------
 */

int TIMER_GetPlatformTicksInOneSecond(int64_t* t);
void TIMER_GetPlatformTicks(int64_t* t);

static int64_t timerfreq=0;
static int32_t timerlastsample=0;
static int timerticspersec=0;
static void (*usertimercallback)(void) = NULL;

void (*installusertimercallback(void (*callback)(void)))(void)
{
    void (*oldtimercallback)(void);

    oldtimercallback = usertimercallback;
    usertimercallback = callback;

    return oldtimercallback;
}

int inittimer(int tickspersecond)
{
    int64_t t;

    if (timerfreq) return 0;	// already installed

    TIMER_GetPlatformTicksInOneSecond(&t);
    timerfreq = t;
    timerticspersec = tickspersecond;
    TIMER_GetPlatformTicks(&t);
    timerlastsample = (int32_t)(t*timerticspersec / timerfreq);

    usertimercallback = NULL;

    return 0;
}

void uninittimer(void)
{
    if (!timerfreq) return;

    timerfreq=0;
    timerticspersec = 0;
}

void sampletimer(void)
{
    int64_t i;
    int32_t n;

    n64_audio_poll();

    if (!timerfreq) return;

    TIMER_GetPlatformTicks(&i);

    n = (int32_t)(i*timerticspersec / timerfreq) - timerlastsample;
    if (n>0) {
        totalclock += n;
        timerlastsample += n;
    }

    if (usertimercallback) for (; n>0; n--) usertimercallback();
}

uint32_t getticks(void)
{
    return (uint32_t)get_ticks_ms();
}

int gettimerfreq(void)
{
    return timerticspersec;
}

int TIMER_GetPlatformTicksInOneSecond(int64_t* t)
{
    *t = 1000000;
    return 1;
}

void TIMER_GetPlatformTicks(int64_t* t)
{
    *t = (int64_t)get_ticks_us();
}
