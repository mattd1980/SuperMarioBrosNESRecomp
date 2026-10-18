#pragma once
#include <stdint.h>

/* Atmosphere (presentation only), effects tied to what SMB1 shows:
 *   dark levels  underground and castle areas (AreaType 2, 3) dim toward the
 *                screen edges with a soft light around Mario, so fire glows
 *   water        water areas (AreaType 0): moving caustic light, slanted light
 *                shafts, glints on air bubbles (tile $74)
 *   starman      a rainbow aura and sparkles around an invincible Mario
 *                (StarInvincibleTimer $079f)
 *   items        a glow on jumping coins ($60-$63), the star ($8d/$e4) and the
 *                fire flower ($d6/$d9), with twinkles on coins
 *   dust         dust puffs from breaking bricks (chunks, tile $84)
 * The status bar (the top 32 lines) is never touched. pre() runs before the
 * Fireball Glow pass (so fire lights the dark), post() after it. */
void game_atmosphere_set_enabled(int enabled);
void game_atmosphere_configure(int dark, int water, int starman, int items, int dust);
void game_atmosphere_pre(uint32_t *picture, int width, int height);
void game_atmosphere_post(uint32_t *picture, int width, int height);
