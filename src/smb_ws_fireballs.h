#pragma once
#include <stdint.h>

/* Mario's fireballs across the wide view (cycle backend); see smb_ws_fireballs.c. */
void smb_ws_fireballs_configure(int enabled);
/* A displayed fireball/explosion OAM sprite drawn outside the native screen:
 * its X relative to ScreenLeft (the native pass must not draw it). */
int smb_ws_fireballs_sprite_rel(int oam_slot, int *rel);
void smb_ws_fireballs_draw(uint32_t *out, int width, int height, int play_x0, const uint8_t *opaque);
