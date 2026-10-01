/*
 * Tiny frame profiler for the N64 port. Build with `make PROFILE=1`: the time
 * spent in each section is printed to the debug log once per second, next
 * to the frame rate (see _nextpage() in n64_display.c).
 */
#ifndef N64_PROF_H
#define N64_PROF_H

#include <stdint.h>

enum {
    PROF_DRAWROOMS,     // walls, floors and ceilings
    PROF_DRAWMASKS,     // sprites and masked walls (incl. animatesprites)
    PROF_DISPLAYREST,   // status bar, HUD, menus
    PROF_PRESENT,       // RDP blit of the 8-bit frame to the framebuffer
    PROF_WEAPON,        //   part of "rest": first person weapon
    PROF_STATUSBAR,     //   part of "rest": status bar
    PROF_CONSOLE,       //   part of "rest": console overlay
    PROF_MENUS,         //   part of "rest": menus()
    PROF_FTA,           //   part of "rest": operatefta() on-screen messages
    PROF_LOGIC,         // game simulation (domovethings, all ticks of the frame)
    PROF_SKYDRAW,       // present: queueing the sky columns for the RDP
    PROF_SKYSCAN,       // part of "sky": n64_skyscan
    PROF_WALLS,         //   part of "rooms": wallscan()
    PROF_FLATS,         //   part of "rooms": ceilscan()/florscan() (flat)
    PROF_SLOPES,        //   part of "rooms": grouscan() (sloped)
    PROF_SKY,           //   part of "rooms": parascan() (parallax sky)
    PROF_TILELOAD,      // tiles read from ROM (not in the cache)
    PROF_SNDLOAD,       // sounds read from ROM (not in the cache)
    PROF_AUDIO,         // n64_audio_poll: CPU side of the mixer (music synth, FX samples)
    PROF_CON,           //   part of "logic": CON script interpreter (execute)
    PROF_CLIPMOVE,      // clipmove (collisions, mostly "logic")
    PROF_HITSCAN,       // hitscan
    PROF_CANSEE,        // cansee
    PROF_GETZRANGE,     // getzrange
    PROF_SCAN,          //   part of "rooms": scansector (visibility)
    PROF_ANIMSPR,       //   part of "masks": animatesprites
    PROF_COLEND,        //   part of "rooms": n64_cols_end (RDP punches the column holes)
    PROF_SPRRDP,        //   part of "masks": face sprites drawn by the RDP (setup)
    PROF_SPRCPU,        //   part of "masks": face sprites drawn on the CPU
    PROF_SPRFLAT,       //   part of "masks": wall and floor sprites (CPU)
    PROF_MASKWALL,      //   part of "masks": masked walls (CPU)
    PROF_COLADD,        //   part of "walls": n64_cols_add
    PROF_WALLCPU,
    PROF_BUNCH,         //   part of "rooms": choosing the closest bunch (bunchfront)
    PROF_DRAWALLS,      //   part of "rooms": drawalls (walls, flats, sky of a bunch)
    PROF_WALLMOST,      //   part of "drawalls": wallmost/owallmost       //   part of "walls": walls drawn on the CPU (not the RDP)
    PROF_COUNT
};

#ifdef N64_PROFILE
extern uint32_t n64_prof_ticks[PROF_COUNT];
uint32_t n64_prof_now(void);
#define PROF_BEGIN(id) uint32_t prof_start_##id = n64_prof_now()
#define PROF_END(id)   (n64_prof_ticks[id] += n64_prof_now() - prof_start_##id)
#else
#define PROF_BEGIN(id) do {} while (0)
#define PROF_END(id)   do {} while (0)
#endif

#endif
