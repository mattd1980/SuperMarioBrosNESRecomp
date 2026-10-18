/* Extra enemies in World 1-1: more Goombas and green Koopas on open ground.
 *
 * At GameCoreRoutine ($AEEA, once per gameplay frame) the plugin watches the
 * screen's right edge. When it reaches one of the extra spawn points, the
 * enemy enters a free slot exactly as SMB's own group spawner ($C750) does -
 * ID, page, x, y, Y_HighPos and Enemy_Flag - and then SMB's CheckpointEnemyID
 * ($C26C) runs the type's own init routine. From there the game moves, draws,
 * stomps and scores it like any authored enemy. Nothing is patched in ROM.
 *
 * Only the main ground area of 1-1 is touched; with all five enemy slots busy
 * a spawn point is skipped, as an authored enemy would wait. A death or reset
 * moves the screen back and re-arms every point. */
#include "mod_runtime.h"
#include "mod_function_hooks.h"
#include "cyc_mod.h"

#include <stdio.h>

#define HOOK_ID "super-mario-bros.extra-enemies.core"
#define ENEMY_GOOMBA 0x06
#define ENEMY_GREEN_KOOPA 0x00
#define GROUND_Y 0xB0            /* row $B; CheckpointEnemyID adds 8 */

typedef struct { uint16_t x; uint8_t id; } Spawn;
/* World x in pixels, clear of 1-1's pipes and of the pits at
 * 1104-1135, 1376-1423 and 2448-2479. */
static const Spawn SPAWNS[] = {
    { 520, ENEMY_GOOMBA },  { 690, ENEMY_GREEN_KOOPA },
    { 990, ENEMY_GOOMBA },  { 1014, ENEMY_GOOMBA },
    { 1290, ENEMY_GREEN_KOOPA },
    { 1600, ENEMY_GOOMBA }, { 1624, ENEMY_GOOMBA },
    { 1820, ENEMY_GREEN_KOOPA },
    { 2010, ENEMY_GOOMBA }, { 2034, ENEMY_GOOMBA },
};
enum { SPAWN_COUNT = sizeof SPAWNS / sizeof SPAWNS[0] };
static uint8_t s_done[SPAWN_COUNT];
static unsigned long s_spawned;

static uint8_t peek(uint16_t a) { return cyc_mod_peek(a); }

static int in_1_1(void)
{
    return peek(0x075f) == 0 && peek(0x075c) == 0 &&   /* WorldNumber, LevelNumber */
           peek(0x074e) == 1 &&                        /* AreaType: ground */
           peek(0x0747) == 0;                          /* TimerControl: not frozen */
}

static int spawn(const Spawn *sp)
{
    int slot = -1;
    for (int i = 0; i < 5; ++i)
        if (!peek((uint16_t)(0x0f + i))) { slot = i; break; }
    if (slot < 0) return 0;
    uint16_t s = (uint16_t)slot;
    cyc_mod_poke(0x16 + s, sp->id);                    /* Enemy_ID */
    cyc_mod_poke(0x6e + s, (uint8_t)(sp->x >> 8));     /* Enemy_PageLoc */
    cyc_mod_poke(0x87 + s, (uint8_t)sp->x);            /* Enemy_X_Position */
    cyc_mod_poke(0xcf + s, GROUND_Y);                  /* Enemy_Y_Position */
    cyc_mod_poke(0xb6 + s, 1);                         /* Enemy_Y_HighPos */
    cyc_mod_poke(0x1e + s, 0);                         /* Enemy_State */
    cyc_mod_poke(0x0f + s, 1);                         /* Enemy_Flag */
    CycModRegs r;
    cyc_mod_regs(&r);
    r.x = (uint8_t)slot;
    r.p &= (uint8_t)~0x01;                             /* carry clear for its ADC */
    if (!cyc_mod_call_commit(0xc26c, &r)) {            /* CheckpointEnemyID */
        cyc_mod_poke(0x0f + s, 0);
        fprintf(stderr, "[Extra enemies] init failed for slot %d\n", slot);
        return 0;
    }
    ++s_spawned;
    return 1;
}

/* GameCoreRoutine entry: its registers are dead (it loads X and A first), so
 * the committed init call may leave its own. Never handles the routine. */
static int core_hook(uint16_t addr)
{
    (void)addr;
    if (!in_1_1()) return 0;
    int right = (peek(0x071b) << 8) | peek(0x071d);    /* ScreenRight page:x */
    for (int i = 0; i < SPAWN_COUNT; ++i) {
        int x = SPAWNS[i].x;
        if (right < x - 48) s_done[i] = 0;             /* behind us again: re-arm */
        else if (!s_done[i] && right + 16 >= x && right < x + 8)
            s_done[i] = (uint8_t)spawn(&SPAWNS[i]);
        else if (right >= x + 8) s_done[i] = 1;        /* passed while slots were full */
    }
    return 0;
}

static void reset_extra_enemies(void)
{
    nes_mod_set_function_hook_enabled(HOOK_ID, 0);
}

static void activate_extra_enemies(void)
{
    for (int i = 0; i < SPAWN_COUNT; ++i) s_done[i] = 0;
    /* spawn bookkeeping lives outside the machine: keep it off rollback netplay */
    nes_mod_set_local_only("Extra 1-1 enemies", 1);
    nes_mod_set_function_hook_enabled(HOOK_ID, 1);
}

unsigned long extra_enemies_spawned(void) { return s_spawned; }

NES_MOD_CONSTRUCTOR(register_extra_enemies_plugin) {
    if (!nes_mod_register_function_entry_plugin(HOOK_ID, 0xaeea, core_hook) ||
        !nes_mod_register_reset_callback(reset_extra_enemies) ||
        !nes_mod_register_activation_plugin("super-mario-bros.extra-enemies", activate_extra_enemies))
        fprintf(stderr, "[Mods] Failed to register the extra 1-1 enemies plugin\n");
}
