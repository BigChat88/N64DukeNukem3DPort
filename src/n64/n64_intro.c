/*
 * Boot-time "powered by libdragon" dragon logo animation.
 *
 * Ported from the N64 Doom port (src/i_intro.c), itself adapted from
 * lambertjamesd/n64brew2025's src/intro/logo.c (logo_libdragon), which
 * credits:
 *
 *   logo_libdragon and logo_n64brew sourced from the N64brew-GameJam2024
 *   repository
 *
 *   Copyright (c) 2024 N64brew
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the
 *   "Software"), to deal in the Software without restriction, including
 *   without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to
 *   permit persons to whom the Software is furnished to do so, subject to
 *   the following conditions:
 *
 *   The above copyright notice and this permission notice shall be included
 *   in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 *   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 *   CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 *   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 *   SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Differences with the Doom port version:
 *  - it runs at Duke's 320x200 (same as Doom), the 640x480 design is scaled
 *    by height and centered;
 *  - any controller button skips it;
 *  - it is silent, like the Doom port (the audio is set up later by FX_Init).
 */
#include <libdragon.h>

#include "n64_platform.h"

#define SCREEN_W 320
#define SCREEN_H 200

// The animation was authored in 640x480 design-space coordinates. Scaling by
// height keeps everything visible on the wider 320x200 (8:5) screen, and the
// result is centered horizontally.
#define INTRO_SCALE ((float)SCREEN_H / 480.0f)
#define INTRO_X_MARGIN (((float)SCREEN_W - 640.0f*INTRO_SCALE) / 2.0f)
#define SXF(v) ((v)*INTRO_SCALE + INTRO_X_MARGIN)
#define SYF(v) ((v)*INTRO_SCALE)
#define SX(v) ((int)SXF(v))
#define SY(v) ((int)SYF(v))

static int any_button_held(void)
{
    joypad_buttons_t b;

    joypad_poll();
    b = joypad_get_buttons(JOYPAD_PORT_1);
    return b.raw != 0;
}

void n64_dragon_intro(void)
{
    const color_t RED = RGBA32(221, 46, 26, 255);
    const color_t WHITE = RGBA32(255, 255, 255, 255);
    // Translation offset of the animation (simplifies centering).
    const int X0 = 10, Y0 = 30;

    sprite_t *d1 = sprite_load("rom:/intro/dragon1.sprite");
    sprite_t *d2 = sprite_load("rom:/intro/dragon2.sprite");
    sprite_t *d3 = sprite_load("rom:/intro/dragon3.sprite");
    sprite_t *d4 = sprite_load("rom:/intro/dragon4.sprite");

    float angle1 = 3.2f, angle2 = 1.9f, angle3 = 0.9f;
    float scale1 = 0.0f, scale2 = 0.4f, scale3 = 0.8f, scroll4 = 400;
    uint32_t ms0;
    int anim_part;

    // Missing assets (e.g. a ROM built without them): skip the intro.
    if (!d1 || !d2 || !d3 || !d4)
    {
        debugf("intro: sprites missing, skipped\n");
        if (d1) sprite_free(d1);
        if (d2) sprite_free(d2);
        if (d3) sprite_free(d3);
        if (d4) sprite_free(d4);
        return;
    }

    joypad_init();
    display_init((resolution_t){ .width = SCREEN_W, .height = SCREEN_H, .interlaced = INTERLACE_OFF },
                 DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
    rdpq_init();

    ms0 = get_ticks_ms();
    while (1)
    {
        // 0: rotate dragon head
        // 1: rotate dragon body and tail, scale up
        // 2: scroll dragon logo
        // 3: fade out
        uint32_t tt = get_ticks_ms() - ms0;
        color_t red = RED, white = WHITE;
        surface_t *fb;

        if (tt < 1000) anim_part = 0;
        else if (tt < 1500) anim_part = 1;
        else if (tt < 4000) anim_part = 2;
        else if (tt < 5000) anim_part = 3;
        else break;

        if (tt > 300 && any_button_held())
            break;

        // Quadratic ease-out of the animation parameters.
        angle1 -= angle1 * 0.04f; if (angle1 < 0.010f) angle1 = 0;
        if (anim_part >= 1)
        {
            angle2 -= angle2 * 0.06f; if (angle2 < 0.01f) angle2 = 0;
            angle3 -= angle3 * 0.06f; if (angle3 < 0.01f) angle3 = 0;
            scale2 -= scale2 * 0.06f; if (scale2 < 0.01f) scale2 = 0;
            scale3 -= scale3 * 0.06f; if (scale3 < 0.01f) scale3 = 0;
        }
        if (anim_part >= 2)
            scroll4 -= scroll4 * 0.08f;

        // Fade out.
        if (anim_part >= 3)
        {
            red.a = 255 - (tt-4000) * 255 / 1000;
            white.a = 255 - (tt-4000) * 255 / 1000;
        }

        fb = display_get();
        rdpq_attach_clear(fb, NULL);

        // The head is scissored so that it appears to jump out; at first also
        // horizontally, so that the tail of the head is not visible on the right.
        if (angle1 > 1.0f)
            rdpq_set_scissor(SX(0), SY(0), SX(X0+300), SY(Y0+240));
        else
            rdpq_set_scissor(SX(0), SY(0), SX(640), SY(Y0+240));

        // Dragon head.
        rdpq_set_mode_standard();
        rdpq_mode_alphacompare(1);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_combiner(RDPQ_COMBINER1((0,0,0,PRIM),(TEX0,0,PRIM,0)));
        rdpq_set_prim_color(red);
        rdpq_sprite_blit(d1, SX(X0+216), SY(Y0+205), &(rdpq_blitparms_t){
            .theta = angle1, .scale_x = (scale1+1)*INTRO_SCALE, .scale_y = (scale1+1)*INTRO_SCALE,
            .cx = 176, .cy = 171,
        });

        rdpq_set_scissor(0, 0, SCREEN_W, SCREEN_H);

        // Black rectangle with an alpha gradient to cover the tail of the head.
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        rdpq_mode_dithering(DITHER_NOISE_NOISE);
        {
            float vtx[4][6] = {
                //  x,             y,          r,g,b,a
                { SXF(X0+0),   SYF(Y0+180), 0,0,0,0 },
                { SXF(X0+200), SYF(Y0+180), 0,0,0,0 },
                { SXF(X0+200), SYF(Y0+240), 0,0,0,1 },
                { SXF(X0+0),   SYF(Y0+240), 0,0,0,1 },
            };
            rdpq_triangle(&TRIFMT_SHADE, vtx[0], vtx[1], vtx[2]);
            rdpq_triangle(&TRIFMT_SHADE, vtx[0], vtx[2], vtx[3]);
        }

        if (anim_part >= 1)
        {
            // Dragon body and tail, fading in.
            color_t color = red;

            rdpq_set_mode_standard();
            rdpq_mode_alphacompare(1);
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_mode_combiner(RDPQ_COMBINER1((0,0,0,PRIM),(TEX0,0,PRIM,0)));

            color.r *= 1-scale3; color.g *= 1-scale3; color.b *= 1-scale3;
            rdpq_set_prim_color(color);

            rdpq_sprite_blit(d2, SX(X0+246), SY(Y0+230), &(rdpq_blitparms_t){
                .theta = angle2, .scale_x = (1-scale2)*INTRO_SCALE, .scale_y = (1-scale2)*INTRO_SCALE,
                .cx = 145, .cy = 113,
            });
            rdpq_sprite_blit(d3, SX(X0+266), SY(Y0+256), &(rdpq_blitparms_t){
                .theta = -angle3, .scale_x = (1-scale3)*INTRO_SCALE, .scale_y = (1-scale3)*INTRO_SCALE,
                .cx = 91, .cy = 24,
            });
        }

        // Scrolling wordmark.
        if (anim_part >= 2)
        {
            rdpq_set_prim_color(white);
            rdpq_sprite_blit(d4, SX(X0 + 161 + (int)scroll4), SY(Y0 + 182), &(rdpq_blitparms_t){
                .scale_x = INTRO_SCALE, .scale_y = INTRO_SCALE,
            });
        }

        rdpq_detach_show();
    }

    // If skipped with a button, wait until it is released so the same press
    // does not also skip Duke's own logo screens (2 s at most).
    {
        uint32_t t0 = get_ticks_ms();
        while (any_button_held() && get_ticks_ms() - t0 < 2000)
            ;
    }

    rspq_wait();
    sprite_free(d1);
    sprite_free(d2);
    sprite_free(d3);
    sprite_free(d4);
    display_close();
}
