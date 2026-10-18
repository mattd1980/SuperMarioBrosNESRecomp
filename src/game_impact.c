/* Impact Effects (game_impact.h).
 *
 * Events come from SMB1's RAM (smb-disassembly smb.asm), compared with the
 * previous frame per enemy slot 0-4 (Enemy_Flag $0f, Enemy_ID $16,
 * Enemy_State $1e, position $6e page / $87 x / $cf y / $b6 y high):
 *   stomp        state 0 -> 4 (HandleStompedShellE: goomba flattened, koopa
 *                in its shell) or a paratroopa ($0e-$10) demoted to a koopa
 *   defeat       d5 ($20) newly set (ShellOrBlockDefeat, EnemyStompedPts);
 *                a fireball that started exploding this frame (Fireball_State
 *                $24/$25 d7, set on its enemy hit) makes it a fireball kill
 *   Bowser       Bowser ($2d) replaced by his disguise with d5 set (HurtBowser:
 *                beaten with fireballs), or erased with d6 ($40) set: the
 *                bridge went and he fell into the lava (KillAllEnemies). His
 *                rear is a second slot flagged $80 + front, skipped.
 *   bridge       BridgeCollapseOffset $0369 counts the metatiles removed, at
 *                name table $2200 + BridgeCollapseData[i] (axe, chain, bridge)
 *   brick        Block_State $26/$27 low nybble becomes 2 (brick chunks:
 *                PlayerHeadCollision sets $12 for big Mario and the block
 *                object leaves the frame as $02), at Block_* $76 page /
 *                $8f x / $d7 y
 * Positions are world pixels; the picture shows world x at x - camera + x0
 * (game_widescreen_picture_origin), y as is. Only the play modes (OperMode
 * $0770 1 and 2) are watched. */
#include "game_impact.h"
#include "cycle_bridge.h"
#include "cyc_fx.h"
#include "game_widescreen.h"

#include <string.h>

#define HUD_ROWS 32
#define BOWSER   0x2d
#define MAX_LATER 32
#define LAVA_Y   212.0f       /* the lava's surface in Bowser's castles */

typedef struct { uint8_t flag, id, state, visible; float x, y; } Slot;   /* x, y: centre, world pixels */
typedef struct { uint64_t due; float x, y; int kind; } Later;
enum { LATER_BLAST, LATER_SMOKE };

static int s_enabled, s_stomp = 1, s_fireball = 1, s_bricks = 1, s_bowser = 1;
static float s_strength = 0.7f;
static Slot s_prev[5];
static uint8_t s_prev_block[2], s_prev_fball[2], s_prev_bridge;
static int s_have_prev;
static Later s_later[MAX_LATER];
static int s_nlater;

static uint8_t peek(uint16_t a) { return cyc_mod_peek(a); }
static float rnd(void) { return cyc_fx_random(); }

void game_impact_reset(void)
{
    cyc_fx_reset();
    s_have_prev = 0;
    s_nlater = 0;
}

void game_impact_set_enabled(int enabled)
{
    s_enabled = enabled != 0;
    game_impact_reset();
}

void game_impact_configure(int strength_percent, int stomp, int fireball, int bricks, int bowser)
{
    if (strength_percent < 10) strength_percent = 10;
    if (strength_percent > 200) strength_percent = 200;
    s_strength = (float)strength_percent / 100.0f;
    s_stomp = stomp != 0; s_fireball = fireball != 0; s_bricks = bricks != 0; s_bowser = bowser != 0;
}

static void later(uint64_t due, float x, float y, int kind)
{
    if (s_nlater < MAX_LATER) s_later[s_nlater++] = (Later){ due, x, y, kind };
}

static CycFxParticle part(float x, float y, CycFxLook look, float life, float size, uint8_t r, uint8_t g, uint8_t b)
{
    CycFxParticle p;
    memset(&p, 0, sizeof p);
    p.x = x; p.y = y; p.look = look; p.life = life; p.size = size; p.alpha = 1.0f;
    p.r = r; p.g = g; p.b = b;
    return p;
}

/* ---- the effects ---- */

static void stomp_puff(float x, float feet)
{
    for (int side = -1; side <= 1; side += 2)                   /* dust kicked out to both sides */
        for (int i = 0; i < 5; ++i) {
            CycFxParticle p = part(x + (float)side * (2.0f + rnd() * 5.0f), feet - 2.0f - rnd() * 4.0f, CYC_FX_DUST,
                                   16.0f + rnd() * 8.0f, 2.4f + rnd() * 1.4f, 250, 246, 236);
            p.vx = (float)side * (0.7f + rnd() * 1.0f); p.vy = -0.2f - rnd() * 0.4f;
            p.drag = 0.1f; p.grow = 0.14f; p.alpha = 1.6f;
            cyc_fx_particle(&p);
        }
    cyc_fx_ring(x, feet - 6.0f, 15.0f, 10.0f, 0.9f, 255, 255, 240);
}

static void smack_sparks(float x, float y)                       /* shell, star, bumped block */
{
    CycFxParticle p = part(x, y, CYC_FX_SPARK, 9.0f, 1.0f, 255, 250, 210);
    p.drag = 0.12f; p.alpha = 0.9f;
    cyc_fx_burst(&p, 7, 2.6f);
    cyc_fx_ring(x, y, 12.0f, 8.0f, 0.4f, 255, 255, 230);
}

/* Fireball Glow already lights the game's own explosion, so a kill adds what
 * it lacks: embers flung out and falling, sparks and a ring. */
static void fire_burst(float x, float y, float scale)
{
    CycFxParticle p = part(x, y, CYC_FX_GLOW, 22.0f * scale, 1.6f * scale, 0, 0, 0);
    p.fire = 1; p.drag = 0.06f; p.gravity = 0.06f; p.alpha = 1.0f;
    cyc_fx_burst(&p, (int)(14.0f * scale), 2.2f * scale);
    CycFxParticle s = part(x, y, CYC_FX_SPARK, 13.0f * scale, 1.0f, 255, 225, 130);
    s.drag = 0.07f; s.gravity = 0.08f; s.alpha = 1.0f;
    cyc_fx_burst(&s, (int)(8.0f * scale), 3.2f * scale);
    cyc_fx_ring(x, y, 20.0f * scale, 11.0f, 0.6f, 255, 200, 110);
    if (scale > 1.0f) cyc_fx_light(x, y, 34.0f * scale, 12.0f * scale, 0.5f, 1.0f, 0.6f, 0.25f);
}

static void smoke(float x, float y, int count)
{
    for (int i = 0; i < count; ++i) {
        CycFxParticle p = part(x + (rnd() - 0.5f) * 20.0f, y + (rnd() - 0.5f) * 16.0f, CYC_FX_DUST,
                               30.0f + rnd() * 20.0f, 3.0f + rnd() * 2.5f, 90, 80, 78);
        p.vx = (rnd() - 0.5f) * 0.5f; p.vy = -0.25f - rnd() * 0.35f; p.drag = 0.03f; p.grow = 0.12f; p.alpha = 0.55f;
        cyc_fx_particle(&p);
    }
}

static void bowser_explosion(float x, float y, uint64_t frame)
{
    fire_burst(x, y, 2.2f);
    cyc_fx_ring(x, y, 48.0f, 18.0f, 0.6f, 255, 210, 140);
    cyc_fx_light(x, y, 90.0f, 40.0f, 0.75f, 1.0f, 0.7f, 0.35f);
    for (int i = 1; i <= 5; ++i)                                  /* a rolling chain of smaller blasts */
        later(frame + (uint64_t)(i * 5), x + (rnd() - 0.5f) * 36.0f, y + (rnd() - 0.5f) * 30.0f, LATER_BLAST);
    later(frame + 14, x, y, LATER_SMOKE);
}

static void bridge_break(float x, float y)
{
    for (int i = 0; i < 5; ++i) {                                 /* bridge pieces dropping into the lava */
        CycFxParticle p = part(x + (rnd() - 0.5f) * 14.0f, y + (rnd() - 0.5f) * 8.0f, CYC_FX_CHUNK,
                               26.0f + rnd() * 10.0f, 2.0f + rnd() * 1.5f, 200, 110, 60);
        p.vx = (rnd() - 0.5f) * 1.2f; p.vy = -1.0f - rnd() * 0.8f; p.gravity = 0.16f; p.alpha = 0.95f;
        cyc_fx_particle(&p);
    }
    CycFxParticle d = part(x, y, CYC_FX_DUST, 16.0f, 2.2f, 170, 120, 90);
    d.drag = 0.12f; d.grow = 0.12f; d.alpha = 0.6f;
    cyc_fx_burst(&d, 5, 0.8f);
}

static void lava_splash(float x, float y)
{
    for (int i = 0; i < 36; ++i) {                                /* a column of molten drops */
        CycFxParticle p = part(x + (rnd() - 0.5f) * 30.0f, y, CYC_FX_GLOW, 26.0f + rnd() * 18.0f, 1.8f + rnd() * 1.8f, 0, 0, 0);
        p.fire = 1; p.vx = (rnd() - 0.5f) * 1.8f; p.vy = -2.0f - rnd() * 2.8f; p.gravity = 0.13f; p.alpha = 1.2f;
        cyc_fx_particle(&p);
    }
    cyc_fx_ring(x, y, 40.0f, 16.0f, 0.7f, 255, 170, 80);
    cyc_fx_light(x, y - 10.0f, 80.0f, 36.0f, 0.7f, 1.0f, 0.55f, 0.2f);
    smoke(x, y - 16.0f, 10);
}

static void brick_burst(float x, float y)
{
    CycFxParticle d = part(x, y, CYC_FX_DUST, 20.0f, 3.0f, 225, 200, 165);
    d.drag = 0.12f; d.grow = 0.14f; d.gravity = -0.01f; d.alpha = 1.6f;
    cyc_fx_burst(&d, 10, 1.3f);
    CycFxParticle f = part(x, y, CYC_FX_CHUNK, 20.0f, 1.0f, 120, 60, 30);   /* small flecks besides the game's chunks */
    f.gravity = 0.2f; f.alpha = 0.95f;
    for (int i = 0; i < 6; ++i) {
        CycFxParticle q = f;
        q.vx = (rnd() - 0.5f) * 2.6f; q.vy = -1.5f - rnd() * 1.8f; q.life += rnd() * 8.0f;
        cyc_fx_particle(&q);
    }
}

/* ---- detection ---- */

static float world_x(uint8_t page, uint8_t x) { return (float)page * 256.0f + (float)x; }

static int fireball_hit(void)                                     /* a fireball began exploding this frame */
{
    int hit = 0;
    for (int i = 0; i < 2; ++i) {
        uint8_t s = peek(0x24 + i);
        if ((s & 0x80) && !(s_prev_fball[i] & 0x80)) hit = 1;
        s_prev_fball[i] = s;
    }
    return hit;
}

static void run_later(uint64_t frame)
{
    int k = 0;
    for (int i = 0; i < s_nlater; ++i) {
        Later l = s_later[i];
        if (l.due > frame) { s_later[k++] = l; continue; }
        if (l.kind == LATER_BLAST) { fire_burst(l.x, l.y, 1.1f); cyc_fx_ring(l.x, l.y, 22.0f, 10.0f, 0.4f, 255, 200, 120); }
        else smoke(l.x, l.y, 10);
    }
    s_nlater = k;
}

void game_impact_frame_end(void)
{
    if (!s_enabled) return;
    uint64_t frame = g_frame_count;
    cyc_fx_set_strength(s_strength);
    cyc_fx_advance(frame);
    uint8_t mode = peek(0x0770);
    if (mode != 1 && mode != 2) { s_have_prev = 0; s_nlater = 0; return; }
    run_later(frame);

    Slot now[5];
    for (int i = 0; i < 5; ++i)
        now[i] = (Slot){ peek(0x0f + i), peek(0x16 + i), peek(0x1e + i), peek(0xb6 + i) == 1,
                         world_x(peek(0x6e + i), peek(0x87 + i)) + 8.0f, (float)peek(0xcf + i) + 9.0f };
    uint8_t block[2] = { peek(0x26), peek(0x27) }, bridge = peek(0x0369);
    int fhit = fireball_hit();

    if (s_have_prev) {
        for (int i = 0; i < 5; ++i) {
            const Slot *a = &s_prev[i], *b = &now[i];
            if (!a->flag || (a->flag & 0x80)) continue;                /* empty, or a part of another object (Bowser's rear) */
            float x = b->x, y = b->y;
            if (a->id == BOWSER) {
                if (!s_bowser || !a->visible) continue;
                if (b->flag && b->id != BOWSER && (b->state & 0x20)) bowser_explosion(x, y, frame);   /* fireballs */
                else if (b->id == BOWSER && (b->state & 0x40) && a->y < LAVA_Y && b->y >= LAVA_Y)
                    lava_splash(b->x, LAVA_Y);                                                         /* into the lava */
                continue;
            }
            if (!b->flag || !b->visible) continue;
            if (b->id == a->id && b->state == 4 && a->state == 0) { if (s_stomp) stomp_puff(x, y + 8.0f); }
            else if (a->id >= 0x0e && a->id <= 0x10 && b->id <= 1 && b->state == 0) { if (s_stomp) stomp_puff(x, y + 8.0f); }
            else if (b->id == a->id && (b->state & 0x20) && !(a->state & 0x20)) {
                if (fhit && s_fireball) fire_burst(x, y, 1.0f);
                else if (!fhit && s_stomp) smack_sparks(x, y);
            }
        }
        for (int i = 0; i < 2; ++i)
            if (s_bricks && (block[i] & 0x0f) == 2 && (s_prev_block[i] & 0x0f) != 2)
                brick_burst(world_x(peek(0x76 + i), peek(0x8f + i)) + 8.0f, (float)peek(0xd7 + i) + 8.0f);
        if (s_bowser && bridge > s_prev_bridge && bridge <= 0x0f) {
            static const uint8_t data[15] = { 0x1a, 0x58, 0x98, 0x96, 0x94, 0x92, 0x90, 0x8e, 0x8c,
                                              0x8a, 0x88, 0x86, 0x84, 0x82, 0x80 };   /* BridgeCollapseData */
            unsigned off = 0x200u + data[bridge - 1];
            int col = (int)(off & 31), row = (int)(off >> 5);
            int left = peek(0x071a) * 256 + peek(0x071c), page = (left >> 8) & ~1;
            float x = (float)(page * 256 + col * 8);
            if (x < (float)left - 16.0f) x += 512.0f;               /* name table 0 is the even page in view */
            bridge_break(x + 8.0f, (float)(row * 8) + 8.0f);
        }
    }
    memcpy(s_prev, now, sizeof now);
    memcpy(s_prev_block, block, sizeof block);
    s_prev_bridge = bridge;
    s_have_prev = 1;
}

void game_impact_post_render(uint32_t *pic, int w, int h)
{
    if (!s_enabled || !pic || w <= 0 || h != 240 || !cyc_fx_active()) return;
    int camera, x0 = game_widescreen_picture_origin(w, &camera);
    cyc_fx_draw(pic, w, h, (float)(x0 - camera), 0.0f, HUD_ROWS);
}
