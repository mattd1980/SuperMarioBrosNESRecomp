#pragma once
#include <stdint.h>

/* Fireball Glow (presentation only): a flickering warm light around every
 * fire sprite the frame shows (Mario's fireballs and castle firebars, tiles
 * $64/$65; explosions and fireworks, tiles $66-$68) and a trail of rising
 * embers behind them. Drawn into the presented picture; game state, the
 * machine's frame and save states are untouched. */
void game_fireglow_set_enabled(int enabled);
/* strength_percent: light and ember brightness (100 = normal); flames: the
 * ember trail on or off. */
void game_fireglow_configure(int strength_percent, int flames);
void game_fireglow_post_render(uint32_t *picture, int width, int height);
