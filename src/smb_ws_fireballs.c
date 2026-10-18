/* Mario's fireballs across the wide view.
 *
 * FireballObjCore ($B689) erases a fireball as soon as its offscreen bits say
 * it left the 256-px screen (rel X outside 1..247), collides it in 8-bit
 * screen space, and DrawFireball writes its relative X into OAM, which wraps
 * past the native edges. With Widescreen the picture is wider, so:
 *  - FireballBGCollision ($E1C8) runs after the bits are computed and before
 *    that erase check: while the fireball is inside the wide view, its
 *    horizontal offscreen bits are cleared. The reach is also capped where the
 *    native block buffer still holds terrain (+-112 px past the screen; the
 *    buffer covers about +-128), so background collision stays real.
 *  - FireballEnemyCollision ($D6D9), for a fireball outside the native screen,
 *    is done here with the same rules in world coordinates, against native
 *    slots and against residents (moved into a slot first); a hit runs the
 *    game's HandleEnemyFBallCol ($D73E), so defeat, points and sound are SMB's.
 *  - Its sprites (fireball or explosion) are hidden from the native pass and
 *    drawn at their real position. The displayed OAM is the previous logic
 *    frame's, so they are matched against that frame's fireballs.
 * Score popups have the same problem for any enemy defeated outside the
 * native screen: SetupFloateyNumber ($DA11) stores the 8-bit Enemy_Rel_XPos.
 * Its hook records how far that byte is from the enemy's real X (a multiple of
 * 256), and the popup's two sprites are moved by it the same way. */
#include "smb_ws_fireballs.h"
#include "smb_ws_actors.h"
#include "game_widescreen.h"
#include "cycle_bridge.h"
#include "nes_video.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include <stdio.h>

enum { REACH = 112, BOX_DATA = 0xe1fd, HANDLE_HIT = 0xd73e };

static int s_enabled;

enum { POPUPS = 7 };   /* FloateyNum_* entries, one per enemy object slot */
typedef struct { uint8_t state, y; int x; } Ball;
typedef struct { uint8_t control, x, y; int16_t shift; } Popup;
typedef struct { uint64_t frame; int camera, valid; Ball ball[2]; Popup popup[POPUPS]; } Snapshot;
static Snapshot s_now, s_prev;
/* Per popup: the X byte SetupFloateyNumber stored, and what to add to it. */
static uint8_t s_popup_x[POPUPS];
static int16_t s_popup_shift[POPUPS];

static int camera(void) { return (g_ram[0x71a] << 8) | g_ram[0x71c]; }
static int ball_x(int i) { return (g_ram[0x74 + i] << 8) | g_ram[0x8d + i]; }   /* Fireball_PageLoc/X_Position */
static int gameplay(void) { return g_ram[0x770] == 1 && g_ram[0x772] == 3; }
static uint8_t rom(int addr) { return mapper_peek_prg((uint16_t)addr); }

/* The fireball's allowed X relative to ScreenLeft: the wide view, within the
 * block buffer's reach. 0 when the picture is not wider than the screen. */
static int reach(int cam, int *lo, int *hi) {
    if (!s_enabled || g_render_width <= 256 || !gameplay()) return 0;
    int left = game_widescreen_view_left(cam, g_render_width) - cam;
    *lo = left < -REACH ? -REACH : left;
    *hi = left + g_render_width > 256 + REACH ? 256 + REACH : left + g_render_width;
    return 1;
}

static void snapshot(void) {
    if (s_now.valid && s_now.frame == g_frame_count) return;
    s_prev = s_now;
    s_now.frame = g_frame_count; s_now.camera = camera(); s_now.valid = 1;
    for (int i = 0; i < 2; i++)
        s_now.ball[i] = (Ball){ g_ram[0x24 + i], g_ram[0xd5 + i], ball_x(i) };
    for (int k = 0; k < POPUPS; k++) {
        uint8_t x = g_ram[0x117 + k];
        /* a later write of the X (InitPlatformFall uses the player's) voids the shift */
        int16_t shift = x == s_popup_x[k] ? s_popup_shift[k] : 0;
        s_now.popup[k] = (Popup){ g_ram[0x110 + k], x, g_ram[0x11e + k], shift };
    }
}

/* Enemy `slot`'s popup X is (or will be) the 8-bit Enemy_Rel_XPos: remember
 * the multiple of 256 between it and the enemy's real X. */
static void record_popup(int slot) {
    if (slot < 0 || slot >= POPUPS) return;
    int rel = ((g_ram[0x6e + slot] << 8) | g_ram[0x87 + slot]) - camera(), stored = g_ram[0x3ae];
    int shift = (rel - stored + 128) & ~255;                 /* round to a multiple of 256 */
    s_popup_x[slot] = (uint8_t)stored;
    s_popup_shift[slot] = (int16_t)(shift >= -512 && shift <= 512 ? shift : 0);
}

/* SetupFloateyNumber, X = the enemy slot. Hooks do not run inside committed
 * calls, so hit() records its own popups. */
static int popup_hook(uint16_t addr) {
    (void)addr;
    CycModRegs r; cyc_mod_regs(&r);
    record_popup(r.x);
    return 0;
}

/* Keep a fireball inside the wide view: clear the horizontal half of
 * FBall_OffscreenBits ($03D2) that FireballObjCore is about to test. */
static int bg_hook(uint16_t addr) {
    (void)addr;
    CycModRegs r; cyc_mod_regs(&r);
    int lo, hi, rel = ball_x(r.x) - camera();
    if (r.x < 2 && reach(camera(), &lo, &hi) && rel > lo && rel <= hi - 8)
        cyc_mod_poke(0x03d2, (uint8_t)(g_ram[0x03d2] & 0xf0));
    return 0;
}

static void hit(int ball, int slot) {
    cyc_mod_poke((uint16_t)(0x24 + ball), 0x80);       /* exploding, as the original sets it */
    cyc_mod_poke(0x0001, (uint8_t)slot);
    CycModRegs r; cyc_mod_regs(&r);
    r.x = (uint8_t)slot;
    if (!cyc_mod_call_commit(HANDLE_HIT, &r)) fprintf(stderr, "[Widescreen] fireball hit on slot %d failed\n", slot);
    /* SetupFloateyNumber sets FloateyNum_Timer to $30: points were awarded, and
     * the popup stands at Enemy_Rel_XPos (a stale guess is voided in snapshot) */
    if (g_ram[0x12c + slot] == 0x30) record_popup(slot);
    cyc_mod_regs(&r); r.x = (uint8_t)ball; cyc_mod_set_regs(&r);   /* ExitFBallEnemy: X = ObjectOffset */
}

/* FireballEnemyCollision for a fireball outside the native screen. */
static int enemy_hook(uint16_t addr) {
    (void)addr;
    CycModRegs r; cyc_mod_regs(&r);
    int i = r.x, lo, hi, cam = camera(), rel = ball_x(i) - cam;
    if (i >= 2 || (rel >= 1 && rel <= 247) || !reach(cam, &lo, &hi)) return 0;
    uint8_t state = g_ram[0x24 + i];
    if (!state || (state & 0x80) || (g_ram[0x09] & 1)) return 1;    /* every other frame, as SMB */
    int fb = BOX_DATA + g_ram[0x4a0 + i] * 4, fx = ball_x(i), fy = g_ram[0xd5 + i];
    int left = fx + rom(fb), top = fy + rom(fb + 1), right = fx + rom(fb + 2), bottom = fy + rom(fb + 3);
    for (int slot = 4; slot >= 0; slot--) {
        int id = g_ram[0x16 + slot], st = g_ram[0x1e + slot];
        if ((st & 0x20) || !g_ram[0x0f + slot] || (id >= 0x24 && id < 0x2b) || (id == 6 && st >= 2)) continue;
        int eb = BOX_DATA + g_ram[0x49a + slot] * 4;
        int ex = (g_ram[0x6e + slot] << 8) | g_ram[0x87 + slot], ey = g_ram[0xcf + slot];
        if (ex + rom(eb) > right || ex + rom(eb + 2) < left || ey + rom(eb + 1) > bottom || ey + rom(eb + 3) < top) continue;
        hit(i, slot);
    }
    if (!(g_ram[0x24 + i] & 0x80)) {
        int slot = smb_ws_actors_fireball_target(left, top, right, bottom);
        if (slot >= 0) hit(i, slot);
    }
    return 1;
}

/* A displayed fireball/explosion sprite whose real position is outside the
 * native screen: its X relative to ScreenLeft. */
int smb_ws_fireballs_sprite_rel(int slot, int *rel) {
    if (!s_enabled || g_render_width <= 256 || slot < 0 || slot >= 64) return 0;
    snapshot();
    const Snapshot *s = s_prev.valid && s_prev.frame + 1 == s_now.frame ? &s_prev : &s_now;
    const uint8_t *o = cyc_render_oam() + slot * 4;
    if (o[0] >= 0xef) return 0;
    for (int k = 0; k < POPUPS; k++) {            /* a score popup's left or right sprite */
        const Popup *p = &s->popup[k];
        int dy = o[0] - (p->y - 8);
        if (!p->control || !p->shift || dy < -2 || dy > 2) continue;
        if (o[3] == p->x || o[3] == ((p->x + 8) & 255)) { *rel = o[3] + p->shift; return 1; }
    }
    int tile = o[1], explosion = tile >= 0x66 && tile <= 0x68;
    if (!(tile == 0x64 || tile == 0x65 || explosion)) return 0;
    for (int i = 0; i < 2; i++) {
        const Ball *b = &s->ball[i];
        if (!b->state || explosion != !!(b->state & 0x80)) continue;
        int r = b->x - s->camera, d = (o[3] - r) & 255, dy = o[0] + 1 - b->y;
        if (d >= 128) d -= 256;
        if (d < -16 || d > 16 || dy < -16 || dy > 16) continue;
        if (r + d >= 0 && r + d <= 255) return 0;          /* the native position is the real one */
        *rel = r + d;
        return 1;
    }
    return 0;
}

static int place(int slot, int x, int y, int *out_x, void *user) {
    (void)x; (void)y;
    int rel;
    if (!smb_ws_fireballs_sprite_rel(slot, &rel)) return 0;
    *out_x = *(const int *)user + rel;
    return 1;
}

void smb_ws_fireballs_draw(uint32_t *out, int width, int height, int play_x0, const uint8_t *opaque) {
    if (!s_enabled || width <= 256) return;
    cyc_render_sprites(out, width, height, play_x0, opaque, place, &play_x0);
}

void smb_ws_fireballs_configure(int enabled) {
    s_enabled = enabled != 0;
    s_now.valid = s_prev.valid = 0;
    nes_mod_set_function_hook_enabled("super-mario-bros.widescreen.fireball-bg", s_enabled);
    nes_mod_set_function_hook_enabled("super-mario-bros.widescreen.fireball-enemy", s_enabled);
    nes_mod_set_function_hook_enabled("super-mario-bros.widescreen.score-popup", s_enabled);
    for (int k = 0; k < POPUPS; k++) s_popup_shift[k] = 0;
}

NES_MOD_CONSTRUCTOR(register_fireballs) {
    if (!nes_mod_register_function_entry_plugin("super-mario-bros.widescreen.fireball-bg", 0xe1c8, bg_hook) ||
        !nes_mod_register_function_entry_plugin("super-mario-bros.widescreen.fireball-enemy", 0xd6d9, enemy_hook) ||
        !nes_mod_register_function_entry_plugin("super-mario-bros.widescreen.score-popup", 0xda11, popup_hook))
        fprintf(stderr, "[Widescreen] Failed to register fireball hooks\n");
}
