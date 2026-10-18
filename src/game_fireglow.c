/* Fireball Glow (game_fireglow.h).
 *
 * Fire is found where the frame drew it: the displayed OAM (cyc_render_oam),
 * by tile - DrawFirebar stores $64/$65 for a fireball or firebar segment,
 * ExplosionTiles is $68/$67/$66 for a fireball's explosion and fireworks
 * (smb-disassembly smb.asm). Sprite coordinates are the native 256-pixel
 * view's; in widescreen the native view starts at camera - view_left.
 *
 * Light: each fire sprite adds a radial falloff to a light buffer; every lit
 * pixel then gains light in proportion to its own colour (terrain catches
 * the light) plus a little warm glow (so a dark sky still shows a halo).
 * Flicker comes from a hash of the frame number, so a replay looks the same.
 *
 * Embers: fireballs shed particles that rise and fade white -> yellow ->
 * orange -> red. They live in world coordinates, so they stay behind as the
 * screen scrolls, and advance once per emulated frame (a paused game, an
 * open menu, keeps them still). */
#include "game_fireglow.h"
#include "cycle_bridge.h"
#include "game_widescreen.h"

#include <math.h>
#include <string.h>

#define MAX_W NES_MAX_RENDER_WIDTH
#define MAX_EMBERS 384

typedef struct { float x, y, vx, vy, age, life, size; } Ember;

static int s_enabled, s_flames = 1;
static float s_strength = 1.0f;
static float s_light[MAX_W * 240];
static Ember s_embers[MAX_EMBERS];
static int s_ember_count;
static uint64_t s_last_frame;
static uint32_t s_rng = 0x9e3779b9u;

void game_fireglow_set_enabled(int enabled)
{
    s_enabled = enabled != 0;
    s_ember_count = 0;
}

void game_fireglow_configure(int strength_percent, int flames)
{
    if (strength_percent < 10) strength_percent = 10;
    if (strength_percent > 300) strength_percent = 300;
    s_strength = (float)strength_percent / 100.0f;
    s_flames = flames != 0;
}

static float rnd(void)                     /* 0..1, xorshift */
{
    s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
    return (float)(s_rng & 0xffffff) / 16777216.0f;
}

static float hash01(uint32_t v)            /* 0..1 from an integer, for flicker */
{
    v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15; v *= 0x846ca68bu; v ^= v >> 16;
    return (float)(v & 0xffffff) / 16777216.0f;
}

/* kind 0: fireball / firebar, 1: explosion / fireworks, 2: Bowser's flame
 * (ProcBowserFlame writes tiles $51-$53), 3: Podoboo (EnemyGraphicsTable $d0/$d7) */
typedef struct { int x, y, kind; } Fire;

static int find_fire(Fire *out, int max)
{
    const uint8_t *oam = cyc_render_oam();
    int n = 0;
    for (int i = 0; i < 64 && n < max; ++i) {
        uint8_t y = oam[i * 4], tile = oam[i * 4 + 1], x = oam[i * 4 + 3];
        if (y >= 0xef) continue;                                   /* hidden */
        int kind = tile == 0x64 || tile == 0x65 ? 0 : tile >= 0x66 && tile <= 0x68 ? 1
                 : tile >= 0x51 && tile <= 0x53 ? 2 : tile == 0xd0 || tile == 0xd7 ? 3 : -1;
        if (kind < 0) continue;
        out[n++] = (Fire){ x + 4, y + 1 + 4, kind };              /* centre of the 8x8 sprite */
    }
    return n;
}

static void add_light(int w, int h, int cx, int cy, float radius, float amount, int *x0, int *y0, int *x1, int *y1)
{
    int r = (int)radius + 1;
    int ax = cx - r < 0 ? 0 : cx - r, bx = cx + r >= w ? w - 1 : cx + r;
    int ay = cy - r < 0 ? 0 : cy - r, by = cy + r >= h ? h - 1 : cy + r;
    if (ax > bx || ay > by) return;
    if (ax < *x0) *x0 = ax; if (bx > *x1) *x1 = bx;
    if (ay < *y0) *y0 = ay; if (by > *y1) *y1 = by;
    float inv = 1.0f / radius;
    for (int y = ay; y <= by; ++y) {
        float dy = (float)(y - cy);
        for (int x = ax; x <= bx; ++x) {
            float dx = (float)(x - cx), d = sqrtf(dx * dx + dy * dy) * inv;
            if (d >= 1.0f) continue;
            float f = 1.0f - d;
            s_light[y * w + x] += amount * f * f;
        }
    }
}

static uint32_t clamp_rgb(float r, float g, float b, uint32_t a)
{
    int ir = r > 255.0f ? 255 : (int)r, ig = g > 255.0f ? 255 : (int)g, ib = b > 255.0f ? 255 : (int)b;
    return a | ((uint32_t)ir << 16) | ((uint32_t)ig << 8) | (uint32_t)ib;
}

static void step_embers(const Fire *fire, int n, int camera, uint64_t frame)
{
    /* age and move */
    int k = 0;
    for (int i = 0; i < s_ember_count; ++i) {
        Ember e = s_embers[i];
        e.age += 1.0f;
        if (e.age >= e.life) continue;
        e.x += e.vx; e.y += e.vy;
        e.vy -= 0.035f;                                            /* flames rise */
        e.vx *= 0.94f;
        s_embers[k++] = e;
    }
    s_ember_count = k;
    if (!s_flames) return;
    /* a fresh puff from every flying fireball (explosions burst once, bigger) */
    for (int f = 0; f < n; ++f) {
        int count = fire[f].kind == 0 ? 2 : fire[f].kind == 1 ? ((frame & 3) == 0 ? 5 : 0) : 1;
        for (int c = 0; c < count && s_ember_count < MAX_EMBERS; ++c) {
            Ember *e = &s_embers[s_ember_count++];
            e->x = (float)(fire[f].x + camera) + (rnd() - 0.5f) * 5.0f;
            e->y = (float)fire[f].y + (rnd() - 0.5f) * 5.0f;
            int burst = fire[f].kind == 1;
            e->vx = (rnd() - 0.5f) * (burst ? 1.6f : 0.6f);
            e->vy = -0.25f - rnd() * (burst ? 1.1f : 0.5f);
            e->age = 0.0f;
            e->life = 9.0f + rnd() * 9.0f;
            e->size = 1.2f + rnd() * (burst ? 1.6f : 0.9f);
        }
    }
}

static void draw_embers(uint32_t *pic, int w, int h, int camera, int x0)
{
    for (int i = 0; i < s_ember_count; ++i) {
        const Ember *e = &s_embers[i];
        float t = e->age / e->life;                                /* 0 new .. 1 gone */
        float cr, cg, cb;
        if (t < 0.25f)      { cr = 255; cg = 245; cb = 180; }       /* white-yellow core */
        else if (t < 0.55f) { cr = 255; cg = 175; cb = 50; }        /* yellow-orange */
        else if (t < 0.80f) { cr = 235; cg = 95;  cb = 25; }        /* orange-red */
        else                { cr = 160; cg = 40;  cb = 20; }        /* dying red */
        float alpha = (1.0f - t) * 0.9f * (s_strength > 1.0f ? 1.0f : s_strength);
        float radius = e->size * (1.0f - 0.5f * t);
        float sx = e->x - (float)camera + (float)x0, sy = e->y;
        int ax = (int)(sx - radius - 1), bx = (int)(sx + radius + 1);
        int ay = (int)(sy - radius - 1), by = (int)(sy + radius + 1);
        for (int y = ay < 0 ? 0 : ay; y <= by && y < h; ++y)
            for (int x = ax < 0 ? 0 : ax; x <= bx && x < w; ++x) {
                float dx = (float)x + 0.5f - sx, dy = (float)y + 0.5f - sy;
                float d = sqrtf(dx * dx + dy * dy) / radius;
                if (d >= 1.0f) continue;
                float a = alpha * (1.0f - d * d);
                uint32_t p = pic[y * w + x];
                float r = (float)((p >> 16) & 255), g = (float)((p >> 8) & 255), b = (float)(p & 255);
                /* mostly blended, so the flame keeps its colour on a pale sky; a little added for heat */
                r += (cr - r) * a + cr * a * 0.2f;
                g += (cg - g) * a + cg * a * 0.2f;
                b += (cb - b) * a + cb * a * 0.2f;
                pic[y * w + x] = clamp_rgb(r < 0 ? 0 : r, g < 0 ? 0 : g, b < 0 ? 0 : b, p & 0xff000000u);
            }
    }
}

void game_fireglow_post_render(uint32_t *pic, int w, int h)
{
    if (!s_enabled || !pic || w <= 0 || w > MAX_W || h != 240) return;
    int camera;
    int x0 = game_widescreen_picture_origin(w, &camera);              /* where sprite x 0 is drawn */
    Fire fire[64];
    int n = find_fire(fire, 64);
    uint64_t frame = g_frame_count;
    if (frame != s_last_frame) {                                   /* once per emulated frame */
        if (frame < s_last_frame || frame - s_last_frame > 4) s_ember_count = 0;   /* reset, state load */
        step_embers(fire, n, camera, frame);
        s_last_frame = frame;
    }
    if (n) {
        int lx0 = w, ly0 = h, lx1 = -1, ly1 = -1;
        for (int i = 0; i < n; ++i) {
            float flicker = 0.84f + 0.10f * hash01((uint32_t)(frame * 7 + (uint64_t)i * 131)) +
                            0.06f * sinf((float)frame * 0.7f + (float)i * 1.9f);
            static const float RADIUS[] = { 26.0f, 40.0f, 34.0f, 30.0f }, AMOUNT[] = { 0.48f, 0.55f, 0.42f, 0.50f };
            float radius = RADIUS[fire[i].kind];
            float amount = AMOUNT[fire[i].kind] * flicker * s_strength;
            add_light(w, h, fire[i].x + x0, fire[i].y, radius, amount, &lx0, &ly0, &lx1, &ly1);
        }
        for (int y = ly0; y <= ly1; ++y)
            for (int x = lx0; x <= lx1; ++x) {
                float L = s_light[y * w + x];
                if (L <= 0.0f) continue;
                s_light[y * w + x] = 0.0f;
                if (L > 1.6f) L = 1.6f;
                uint32_t p = pic[y * w + x];
                float r = (float)((p >> 16) & 255), g = (float)((p >> 8) & 255), b = (float)(p & 255);
                /* warm light: catches the surface's own colour, plus a soft glow
                 * that grows on dark surfaces (a black cave still shows the halo);
                 * blue is held back so the light reads as fire, not white */
                float glow = 70.0f + 150.0f * (1.0f - (r * 0.3f + g * 0.59f + b * 0.11f) / 255.0f);
                r += (r * 1.1f + glow) * L;
                g += (g * 1.1f + glow) * L * 0.55f;
                b += (b * 1.1f + glow) * L * 0.10f - b * 0.35f * L;
                pic[y * w + x] = clamp_rgb(r, g, b < 0 ? 0 : b, p & 0xff000000u);
            }
    }
    if (s_ember_count) draw_embers(pic, w, h, camera, x0);
}
