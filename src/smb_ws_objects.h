#pragma once
#include <stdint.h>

/* Firebars, Bowser and his flame, and the power-up drawn across the wide view
 * (cycle backend); see smb_ws_objects.c. */
void smb_ws_objects_configure(int enabled);
void smb_ws_objects_begin_frame(void);
int smb_ws_objects_suppress_sprite(int oam_slot);
int smb_ws_objects_sprite(int n, int *world_x, int *y, int *tile);
void smb_ws_objects_draw(uint32_t *out, int width, int play_x0, int render_camera, const uint8_t *opaque);
