/* Atmosphere (game_atmosphere.h).
 *
 * Inputs are what SMB1 already keeps: OperMode $0770 (1 play, 2 victory),
 * AreaType $074e (0 water, 1 ground, 2 underground, 3 castle), the camera
 * ScreenLeft $071a:$071c, Mario's relative position $03ad / $03b8 and size
 * $0754, StarInvincibleTimer $079f, and the displayed OAM (cyc_render_oam)
 * for objects by tile (smb-disassembly: JumpingCoinTiles $60-$63,
 * PowerUpGfxTable star $8d/$e4 and fire flower $d6/$d9, DrawBrickChunks $84,
 * DrawBubble $74). Coordinates as in game_fireglow.c: sprite x is the native
 * view's, which starts at camera - view_left in widescreen.
 *
 * Lights here are coloured (three buffers), particles have three looks:
 * additive glow, blended dust and a twinkling cross. Particles live in world
 * coordinates and advance once per emulated frame. */
#include "game_atmosphere.h"
#include "cycle_bridge.h"
#include "game_widescreen.h"

#include <math.h>
#include <string.h>

#define MAX_W NES_MAX_RENDER_WIDTH
#define HUD_ROWS 32
#define MAX_PARTS 512

enum { GLOW, DUST, TWINKLE };
typedef struct { float x, y, vx, vy, age, life, size, r, g, b; int look; } Part;
typedef struct { int camera, x0, area, mode; uint64_t frame; } Ctx;

static int s_enabled, s_dark = 1, s_water = 1, s_star = 1, s_items = 1, s_dust = 1;
static float s_lr[MAX_W * 240], s_lg[MAX_W * 240], s_lb[MAX_W * 240];
static Part s_parts[MAX_PARTS];
static int s_count;
static uint64_t s_last;
static uint32_t s_rng = 0x2545f491u;

void game_atmosphere_set_enabled(int enabled)
{
    s_enabled = enabled != 0;
    s_count = 0;
}

void game_atmosphere_configure(int dark, int water, int starman, int items, int dust)
{
    s_dark = dark != 0; s_water = water != 0; s_star = starman != 0; s_items = items != 0; s_dust = dust != 0;
}

static uint8_t peek(uint16_t a) { return cyc_mod_peek(a); }

static float rnd(void)
{
    s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
    return (float)(s_rng & 0xffffff) / 16777216.0f;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static uint32_t pack(float r, float g, float b, uint32_t a)
{
    return a | ((uint32_t)clampf(r, 0, 255) << 16) | ((uint32_t)clampf(g, 0, 255) << 8) | (uint32_t)clampf(b, 0, 255);
}

static void hue(float h, float *r, float *g, float *b)       /* h in turns, full saturation */
{
    h -= floorf(h);
    float x = h * 6.0f;
    *r = clampf(fabsf(x - 3.0f) - 1.0f, 0, 1);
    *g = clampf(2.0f - fabsf(x - 2.0f), 0, 1);
    *b = clampf(2.0f - fabsf(x - 4.0f), 0, 1);
}

static int ctx(Ctx *c, int w)
{
    c->mode = peek(0x0770);
    if (c->mode != 1 && c->mode != 2) return 0;               /* play and victory only */
    c->area = peek(0x074e);
    c->x0 = game_widescreen_picture_origin(w, &c->camera);          /* where sprite x 0 is drawn */
    c->frame = g_frame_count;
    return 1;
}

static void mario_centre(const Ctx *c, float *x, float *y)
{
    *x = (float)(c->x0 + peek(0x03ad) + 8);
    *y = (float)(peek(0x03b8) + (peek(0x0754) == 0 ? 16 : 24));   /* big Mario fills the 32-line box */
}

/* ---- before Fireball Glow ---- */

static void darken(uint32_t *pic, int w, const Ctx *c)
{
    float mx, my, half = (float)w * 0.5f, ambient = c->area == 3 ? 0.66f : 0.72f;
    mario_centre(c, &mx, &my);
    for (int y = HUD_ROWS; y < 240; ++y) {
        float ny = ((float)y - 136.0f) / 104.0f;
        for (int x = 0; x < w; ++x) {
            float nx = ((float)x - half) / half;
            float d = sqrtf(nx * nx * 0.6f + ny * ny);
            float v = clampf((d - 0.45f) / 0.7f, 0, 1);
            float f = ambient * (1.0f - 0.32f * v * v * (3.0f - 2.0f * v));   /* vignette */
            float dx = (float)x - mx, dy = (float)y - my, l = sqrtf(dx * dx + dy * dy) / 84.0f;
            if (l < 1.0f) f += (1.0f - f) * 0.8f * (1.0f - l) * (1.0f - l);     /* Mario's lantern */
            uint32_t p = pic[y * w + x];
            pic[y * w + x] = pack((float)((p >> 16) & 255) * f, (float)((p >> 8) & 255) * f,
                                  (float)(p & 255) * f * 1.04f, p & 0xff000000u);
        }
    }
}

static void water(uint32_t *pic, int w, const Ctx *c)
{
    float t = (float)c->frame;
    for (int y = HUD_ROWS; y < 240; ++y) {
        float depth = 1.0f - (float)(y - HUD_ROWS) / (float)(240 - HUD_ROWS);
        for (int x = 0; x < w; ++x) {
            float wx = (float)(x - c->x0 + c->camera), u = wx * 0.07f, v = (float)y * 0.07f;
            float a = sinf(u + t * 0.045f) + sinf(v * 1.3f - t * 0.035f) + sinf((u + v) * 0.7f + t * 0.05f) +
                      sinf((u - v) * 0.5f - t * 0.03f);
            float p = 1.0f - fabsf(sinf(a * 1.2f));
            p = p * p * p; p *= p;                                  /* thin bright caustic lines */
            float s = sinf(wx * 0.045f + (float)y * 0.018f - t * 0.008f);
            s = s > 0.0f ? powf(s, 10.0f) * depth * 0.9f : 0.0f;    /* slanted shafts from the surface */
            float L = p * 0.22f + s * 0.16f;
            if (L <= 0.004f) continue;
            uint32_t q = pic[y * w + x];
            float r = (float)((q >> 16) & 255), g = (float)((q >> 8) & 255), b = (float)(q & 255);
            pic[y * w + x] = pack(r + (r * 0.6f + 60.0f) * L, g + (g * 0.6f + 110.0f) * L,
                                  b + (b * 0.6f + 140.0f) * L, q & 0xff000000u);
        }
    }
}

void game_atmosphere_pre(uint32_t *pic, int w, int h)
{
    Ctx c;
    if (!s_enabled || !pic || w <= 0 || w > MAX_W || h != 240 || !ctx(&c, w)) return;
    if (s_dark && (c.area == 2 || c.area == 3)) darken(pic, w, &c);
    else if (s_water && c.area == 0) water(pic, w, &c);
}

/* ---- after Fireball Glow ---- */

static void light(int w, float cx, float cy, float radius, float amount, float r, float g, float b, int box[4])
{
    int ax = (int)(cx - radius), bx = (int)(cx + radius), ay = (int)(cy - radius), by = (int)(cy + radius);
    if (ax < 0) ax = 0; if (bx > w - 1) bx = w - 1; if (ay < HUD_ROWS) ay = HUD_ROWS; if (by > 239) by = 239;
    if (ax > bx || ay > by) return;
    if (ax < box[0]) box[0] = ax; if (ay < box[1]) box[1] = ay; if (bx > box[2]) box[2] = bx; if (by > box[3]) box[3] = by;
    for (int y = ay; y <= by; ++y)
        for (int x = ax; x <= bx; ++x) {
            float dx = (float)x - cx, dy = (float)y - cy, d = sqrtf(dx * dx + dy * dy) / radius;
            if (d >= 1.0f) continue;
            float f = amount * (1.0f - d) * (1.0f - d);
            s_lr[y * w + x] += f * r; s_lg[y * w + x] += f * g; s_lb[y * w + x] += f * b;
        }
}

static void spawn(float x, float y, float vx, float vy, float life, float size, float r, float g, float b, int look)
{
    if (s_count >= MAX_PARTS) return;
    s_parts[s_count++] = (Part){ x, y, vx, vy, 0.0f, life, size, r, g, b, look };
}

static void step(void)
{
    int k = 0;
    for (int i = 0; i < s_count; ++i) {
        Part p = s_parts[i];
        if ((p.age += 1.0f) >= p.life) continue;
        p.x += p.vx; p.y += p.vy;
        if (p.look == DUST) { p.vx *= 0.9f; p.vy *= 0.9f; p.size += 0.12f; }
        s_parts[k++] = p;
    }
    s_count = k;
}

static void draw_parts(uint32_t *pic, int w, const Ctx *c)
{
    for (int i = 0; i < s_count; ++i) {
        const Part *p = &s_parts[i];
        float t = p->age / p->life, sx = p->x - (float)c->camera + (float)c->x0, sy = p->y;
        if (p->look == TWINKLE) {                                  /* a cross that grows then shrinks */
            float a = sinf(t * 3.14159f), arm = p->size * a;
            for (int k = -(int)arm; k <= (int)arm; ++k) {
                float fall = 1.0f - fabsf((float)k) / (arm + 1.0f);
                int pts[2][2] = { { (int)sx + k, (int)sy }, { (int)sx, (int)sy + k } };
                for (int j = 0; j < 2; ++j) {
                    int x = pts[j][0], y = pts[j][1];
                    if (x < 0 || x >= w || y < HUD_ROWS || y > 239) continue;
                    uint32_t q = pic[y * w + x];
                    float m = a * fall;
                    pic[y * w + x] = pack((float)((q >> 16) & 255) + p->r * m, (float)((q >> 8) & 255) + p->g * m,
                                          (float)(q & 255) + p->b * m, q & 0xff000000u);
                }
            }
            continue;
        }
        float radius = p->look == DUST ? p->size : p->size * (1.0f - 0.5f * t);
        float alpha = p->look == DUST ? 0.5f * (1.0f - t) : 0.9f * (1.0f - t);
        for (int y = (int)(sy - radius - 1); y <= (int)(sy + radius + 1); ++y) {
            if (y < HUD_ROWS || y > 239) continue;
            for (int x = (int)(sx - radius - 1); x <= (int)(sx + radius + 1); ++x) {
                if (x < 0 || x >= w) continue;
                float dx = (float)x + 0.5f - sx, dy = (float)y + 0.5f - sy, d = sqrtf(dx * dx + dy * dy) / radius;
                if (d >= 1.0f) continue;
                float a = alpha * (1.0f - d * d);
                uint32_t q = pic[y * w + x];
                float r = (float)((q >> 16) & 255), g = (float)((q >> 8) & 255), b = (float)(q & 255);
                if (p->look == DUST) { r += (p->r - r) * a; g += (p->g - g) * a; b += (p->b - b) * a; }
                else { r += p->r * a; g += p->g * a; b += p->b * a; }
                pic[y * w + x] = pack(r, g, b, q & 0xff000000u);
            }
        }
    }
}

void game_atmosphere_post(uint32_t *pic, int w, int h)
{
    Ctx c;
    if (!s_enabled || !pic || w <= 0 || w > MAX_W || h != 240 || !ctx(&c, w)) { s_count = 0; return; }
    int advance = c.frame != s_last;
    if (advance) {
        if (c.frame < s_last || c.frame - s_last > 4) s_count = 0;   /* reset, state load */
        step();
        s_last = c.frame;
    }
    int box[4] = { w, 240, -1, -1 };
    float ft = (float)c.frame;
    const uint8_t *oam = cyc_render_oam();
    for (int i = 0; i < 64; ++i) {
        uint8_t y = oam[i * 4], tile = oam[i * 4 + 1], x = oam[i * 4 + 3];
        if (y >= 0xef) continue;
        float cx = (float)(c.x0 + x + 4), cy = (float)(y + 1 + 4), wx = (float)(x + 4 + c.camera);
        if (s_items && tile >= 0x60 && tile <= 0x63) {                     /* jumping coin */
            light(w, cx, cy, 22.0f, 0.38f, 1.0f, 0.78f, 0.22f, box);
            if (advance && rnd() < 0.18f)
                spawn(wx + (rnd() - 0.5f) * 12.0f, cy + (rnd() - 0.5f) * 14.0f, 0, -0.1f, 9.0f, 3.0f, 255, 240, 170, TWINKLE);
        } else if (s_items && (tile == 0x8d || tile == 0xe4)) {            /* starman item */
            float r, g, b;
            hue(ft * 0.02f, &r, &g, &b);
            light(w, cx, cy, 24.0f, 0.20f, 0.5f + 0.5f * r, 0.5f + 0.5f * g, 0.3f + 0.4f * b, box);
            if (advance && rnd() < 0.08f)
                spawn(wx + (rnd() - 0.5f) * 16.0f, cy + (rnd() - 0.5f) * 16.0f, 0, -0.1f, 10.0f, 3.0f, 255, 255, 200, TWINKLE);
        } else if (s_items && (tile == 0xd6 || tile == 0xd9)) {            /* fire flower */
            float pulse = 0.85f + 0.15f * sinf(ft * 0.15f);
            light(w, cx, cy, 22.0f, 0.22f * pulse, 1.0f, 0.55f, 0.30f, box);
        } else if (s_dust && tile == 0x84) {                               /* brick chunk */
            if (advance && rnd() < 0.7f)
                spawn(wx + (rnd() - 0.5f) * 6.0f, cy + (rnd() - 0.5f) * 6.0f, (rnd() - 0.5f) * 0.5f, -0.15f - rnd() * 0.2f,
                      16.0f + rnd() * 8.0f, 1.8f + rnd(), 170, 140, 105, DUST);
        } else if (s_water && c.area == 0 && tile == 0x74) {               /* air bubble */
            light(w, cx, cy, 10.0f, 0.35f, 0.6f, 0.9f, 1.0f, box);
        }
    }
    if (s_star && c.mode == 1 && peek(0x079f)) {                           /* Starman */
        float mx, my, r, g, b;
        mario_centre(&c, &mx, &my);
        hue(ft * 0.045f, &r, &g, &b);
        float pulse = 0.85f + 0.15f * sinf(ft * 0.5f);
        light(w, mx, my, 42.0f, 0.55f * pulse, 0.35f + 0.65f * r, 0.35f + 0.65f * g, 0.35f + 0.65f * b, box);
        if (advance)
            for (int k = 0; k < 2; ++k) {
                float hr, hg, hb;
                hue(ft * 0.045f + rnd() * 0.3f, &hr, &hg, &hb);
                spawn(mx - (float)c.x0 + (float)c.camera + (rnd() - 0.5f) * 22.0f, my + (rnd() - 0.5f) * 30.0f,
                      (rnd() - 0.5f) * 0.4f, -0.3f - rnd() * 0.4f, 12.0f + rnd() * 8.0f, 1.2f + rnd() * 0.8f,
                      140 + 115 * hr, 140 + 115 * hg, 140 + 115 * hb, GLOW);
            }
    }
    for (int y = box[1]; y <= box[3]; ++y)                                 /* coloured lights */
        for (int x = box[0]; x <= box[2]; ++x) {
            int i = y * w + x;
            float lr = s_lr[i], lg = s_lg[i], lb = s_lb[i];
            if (lr <= 0.0f && lg <= 0.0f && lb <= 0.0f) continue;
            s_lr[i] = s_lg[i] = s_lb[i] = 0.0f;
            lr = clampf(lr, 0, 1.4f); lg = clampf(lg, 0, 1.4f); lb = clampf(lb, 0, 1.4f);
            uint32_t q = pic[i];
            float r = (float)((q >> 16) & 255), g = (float)((q >> 8) & 255), b = (float)(q & 255);
            pic[i] = pack(r + (r + 70.0f) * lr, g + (g + 70.0f) * lg, b + (b + 70.0f) * lb, q & 0xff000000u);
        }
    if (s_count) draw_parts(pic, w, &c);
}
