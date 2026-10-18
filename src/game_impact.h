/* Impact Effects: puffs, sparks and explosions where SMB1's own events happen
 * (an enemy stomped or defeated, a brick broken, Bowser beaten), drawn with
 * the shared presentation effects (nesrecomp runner/cyc/cyc_fx.h). Events are
 * read from RAM once per emulated frame; nothing is written back. */
#pragma once
#include <stdint.h>

void game_impact_set_enabled(int enabled);
/* strength_percent scales opacity and light; the flags pick the events. */
void game_impact_configure(int strength_percent, int stomp, int fireball, int bricks, int bowser);
void game_impact_frame_end(void);            /* every emulated frame */
void game_impact_reset(void);                /* power on, state load */
void game_impact_post_render(uint32_t *pic, int w, int h);
