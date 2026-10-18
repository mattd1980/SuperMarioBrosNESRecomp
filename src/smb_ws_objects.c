/* Objects SMB draws with their own routines - firebars, Bowser and his flame,
 * and the power-up in slot 5 - across the wide view.
 *
 * Their routines compute screen-relative X in 8 bits and hide every sprite
 * column the offscreen bits mark past the native screen (a firebar whose
 * centre is left of the screen is skipped whole), so beyond the native screen
 * they were invisible. When such an object is partly past the native screen,
 * its routine runs once more, isolated (cyc_mod_isolate_begin: nothing it
 * changes survives), with the camera centred on it and OAM cleared; every
 * sprite it writes is kept in world coordinates with its OAM index. Those OAM
 * entries are hidden from the native pass and the kept sprites are drawn at
 * their real position. Captures made during a frame's logic are shown with
 * the OAM that logic wrote (one frame later), like the residents' packets.
 * Behaviour is untouched: the native routine still runs as before. */
#include "smb_ws_objects.h"
#include "game_widescreen.h"
#include "cycle_bridge.h"
#include "nes_video.h"
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Captures: enemy slots 0-5, then misc objects 0-8 (hammers) at MISC+i. */
enum { MISC = 6, SLOTS = MISC + 9, SPRITES = 24 };
typedef struct { uint16_t offscreen_bits, rel_position; } Prepare;
static const Prepare ENEMY = { 0xf1af, 0xf152 }, MISC_OBJECT = { 0xf19b, 0xf148 };

typedef struct { int16_t x, y; uint8_t tile, attr, oam; } WideSprite;
typedef struct { int count; WideSprite sprite[SPRITES]; uint64_t oam_mask; } Capture;

static int s_enabled;
static Capture s_next[SLOTS], s_display[SLOTS];

static int camera(void) { return (g_ram[0x71a] << 8) | g_ram[0x71c]; }
static int enemy_x(int slot) { return (g_ram[0x6e + slot] << 8) | g_ram[0x87 + slot]; }
static int gameplay(void) { return g_ram[0x770] == 1 && g_ram[0x772] == 3; }

/* Is anything of an object `reach` px around X past the native screen, while
 * the object is still near the wide view? */
static int wanted(int x, int reach) {
    if (!s_enabled || g_render_width <= 256 || !gameplay()) return 0;
    int cam = camera(), rel = x - cam;
    if (rel >= reach && rel <= 256 - reach) return 0;
    int left, right;
    game_widescreen_actor_range(cam, g_render_width, 0, &left, &right);
    return x + reach >= left && x - reach <= right;
}

/* Run `routine` for object `object` (its X register) isolated, camera
 * centred on `x`, and keep what it draws in capture `index`. `prepare`: first
 * recompute the offscreen bits and relative position for that camera
 * (routines that use the caller's); NULL when the routine does it. */
static void capture_into(int index, int object, uint16_t routine, int x, const Prepare *prepare) {
    Capture *c = &s_next[index];
    c->count = 0; c->oam_mask = 0;
    int slot = object;
    if (!cyc_mod_isolate_begin()) return;
    int cam = x - 128;
    g_ram[0x71a] = (uint8_t)(cam >> 8); g_ram[0x71c] = (uint8_t)cam;
    g_ram[0x71b] = (uint8_t)((cam + 255) >> 8); g_ram[0x71d] = (uint8_t)(cam + 255);
    for (int i = 0; i < 64; i++) g_ram[0x200 + i * 4] = 0xf8;
    CycModRegs r; cyc_mod_regs(&r);
    int ok = 1;
    if (prepare) {
        r.x = (uint8_t)slot; ok = cyc_mod_call(prepare->offscreen_bits, &r);
        r.x = (uint8_t)slot; ok = ok && cyc_mod_call(prepare->rel_position, &r);
    }
    r.x = (uint8_t)slot;
    if (ok && cyc_mod_call(routine, &r))
        for (int i = 0; i < 64 && c->count < SPRITES; i++) {
            const uint8_t *o = g_ram + 0x200 + i * 4;
            if (o[0] >= 0xef) continue;
            c->sprite[c->count++] = (WideSprite){ (int16_t)(cam + o[3]), (int16_t)(o[0] + 1), o[1], o[2], (uint8_t)i };
            c->oam_mask |= 1ull << i;
        }
    cyc_mod_isolate_end();
    smb1_cycle_sync();
    if (getenv("NESRECOMP_WS_OBJECTS_TRACE") && c->count) {
        fprintf(stderr, "[ws-objects] capture %d routine %04x x %d:", index, routine, x);
        for (int i = 0; i < c->count; i++)
            fprintf(stderr, " %d:%02x/%02x@%d", c->sprite[i].oam, c->sprite[i].tile, c->sprite[i].attr, c->sprite[i].x - x);
        fprintf(stderr, "\n");
    }
}

static void capture(int slot, uint16_t routine, int x, const Prepare *prepare) {
    capture_into(slot, slot, routine, x, prepare);
}

static int slot_of(void) { CycModRegs r; cyc_mod_regs(&r); return r.x; }

/* ProcFirebar ($CD3C): spins, draws up to 12 balls (a long firebar spills
 * into its duplicate object's OAM) out to $58 px from the centre. */
static int firebar_hook(uint16_t addr) {
    (void)addr;
    int slot = slot_of();
    if (slot < 5 && wanted(enemy_x(slot) + 4, 0x60)) capture(slot, 0xcd3c, enemy_x(slot) + 4, NULL);
    return 0;
}

/* BowserGfxHandler ($D17B): both halves, through RunRetainerObj. */
static int bowser_hook(uint16_t addr) {
    (void)addr;
    int slot = slot_of();
    if (slot < 5 && wanted(enemy_x(slot) + 8, 32)) capture(slot, 0xd17b, enemy_x(slot) + 8, NULL);
    return 0;
}

/* ProcBowserFlame ($D1EB): moves, then hides sprites by the offscreen bits
 * computed before it, so those are recomputed for the private camera. */
static int flame_hook(uint16_t addr) {
    (void)addr;
    int slot = slot_of();
    if (slot < 5 && wanted(enemy_x(slot) + 12, 24)) capture(slot, 0xd1eb, enemy_x(slot) + 12, &ENEMY);
    return 0;
}

/* DrawPowerUp ($E6D2), slot 5: RunPUSubs computed position and bits for the
 * real camera just before, so recompute them for the private one. */
static int powerup_hook(uint16_t addr) {
    (void)addr;
    if (slot_of() == 5 && wanted(enemy_x(5) + 8, 16)) capture(5, 0xe6d2, enemy_x(5) + 8, &ENEMY);
    return 0;
}

/* DrawHammer ($E4DC), X = misc object: it draws, then erases the hammer if
 * Misc_OffscreenBits ($03D6, computed just before for this object) has any of
 * %11111100 set - so a hammer died at the native edge. Inside the wide view
 * its horizontal half is cleared first (hammers do not touch the terrain, so
 * the native block buffer is no limit); its sprites, whose 8-bit X wraps past
 * the native screen, are captured and drawn at their real place. */
static int hammer_hook(uint16_t addr) {
    (void)addr;
    int i = slot_of();
    if (i >= 9 || !s_enabled || g_render_width <= 256 || !gameplay()) return 0;
    int x = (g_ram[0x7a + i] << 8) | g_ram[0x93 + i], cam = camera(), left, right;
    game_widescreen_actor_range(cam, g_render_width, 0, &left, &right);
    if (x > left - 8 && x < right) cyc_mod_poke(0x03d6, (uint8_t)(g_ram[0x03d6] & 0xf0));
    if (wanted(x + 8, 16)) capture_into(MISC + i, i, 0xe4dc, x + 8, &MISC_OBJECT);
    return 0;
}

void smb_ws_objects_begin_frame(void) {
    if (!s_enabled) return;
    memcpy(s_display, s_next, sizeof s_display);
    memset(s_next, 0, sizeof s_next);
}

int smb_ws_objects_suppress_sprite(int oam_slot) {
    if (!s_enabled || g_render_width <= 256 || oam_slot < 0 || oam_slot >= 64) return 0;
    for (int k = 0; k < SLOTS; k++)
        if (s_display[k].oam_mask & (1ull << oam_slot)) return 1;
    return 0;
}

/* The n-th host-drawn sprite of the shown frame (world X, Y, tile), for
 * passes that light what the frame draws (Fireball Glow). 0 past the end. */
int smb_ws_objects_sprite(int n, int *world_x, int *y, int *tile) {
    if (!s_enabled || g_render_width <= 256) return 0;
    for (int k = 0; k < SLOTS; k++) {
        if (n < s_display[k].count) {
            const WideSprite *v = &s_display[k].sprite[n];
            *world_x = v->x; *y = v->y; *tile = v->tile;
            return 1;
        }
        n -= s_display[k].count;
    }
    return 0;
}

void smb_ws_objects_draw(uint32_t *out, int width, int play_x0, int render_camera, const uint8_t *opaque) {
    if (!s_enabled || width <= 256 || !gameplay() || !(g_ppumask & 16)) return;
    int pattern = (g_ppuctrl & 8) ? 0x1000 : 0;
    const uint8_t *pal = game_widescreen_frame_palette();
    for (int oam = 63; oam >= 0; oam--)            /* lower OAM index on top, as the PPU does */
        for (int k = 0; k < SLOTS; k++) {
            const Capture *c = &s_display[k];
            if (!(c->oam_mask & (1ull << oam))) continue;
            for (int i = 0; i < c->count; i++) {
                const WideSprite *v = &c->sprite[i];
                if (v->oam != oam) continue;
                for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
                    int dx = v->x - render_camera + play_x0 + x, dy = v->y + y;
                    if (dx < 0 || dx >= width || dy >= 240) continue;
                    int off = pattern + v->tile * 16 + ((v->attr & 128) ? 7 - y : y), bit = (v->attr & 64) ? x : 7 - x;
                    int color = ((g_chr_ram[off] >> bit) & 1) | (((g_chr_ram[off + 8] >> bit) & 1) << 1);
                    if (!color || ((v->attr & 32) && opaque[dy * width + dx])) continue;
                    out[dy * width + dx] = g_nes_palette[pal[16 + (v->attr & 3) * 4 + color] & 63];
                }
            }
        }
}

void smb_ws_objects_configure(int enabled) {
    s_enabled = enabled != 0;
    memset(s_next, 0, sizeof s_next); memset(s_display, 0, sizeof s_display);
    nes_mod_set_function_hook_enabled("super-mario-bros.widescreen.firebar", s_enabled);
    nes_mod_set_function_hook_enabled("super-mario-bros.widescreen.bowser", s_enabled);
    nes_mod_set_function_hook_enabled("super-mario-bros.widescreen.bowser-flame", s_enabled);
    nes_mod_set_function_hook_enabled("super-mario-bros.widescreen.power-up", s_enabled);
    nes_mod_set_function_hook_enabled("super-mario-bros.widescreen.hammer", s_enabled);
}

NES_MOD_CONSTRUCTOR(register_objects) {
    if (!nes_mod_register_function_entry_plugin("super-mario-bros.widescreen.firebar", 0xcd3c, firebar_hook) ||
        !nes_mod_register_function_entry_plugin("super-mario-bros.widescreen.bowser", 0xd17b, bowser_hook) ||
        !nes_mod_register_function_entry_plugin("super-mario-bros.widescreen.bowser-flame", 0xd1eb, flame_hook) ||
        !nes_mod_register_function_entry_plugin("super-mario-bros.widescreen.power-up", 0xe6d2, powerup_hook) ||
        !nes_mod_register_function_entry_plugin("super-mario-bros.widescreen.hammer", 0xe4dc, hammer_hook))
        fprintf(stderr, "[Widescreen] Failed to register object drawing hooks\n");
}
