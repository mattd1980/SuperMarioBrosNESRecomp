#pragma once

#include <stdint.h>

/* Default-off custom renderer. Rendering never changes guest screen edges. */
void game_widescreen_set_mod_enabled(int enabled);
void game_widescreen_configure(const char *aspect, const char *hud, const char *enemies);
void game_widescreen_set_camera(const char *camera);
int game_widescreen_view_left(int native_camera, int width);
/* For passes drawn over the presented picture: where the native 256-pixel
 * view (sprite x 0) sits in it, and the level camera it shows (the displayed
 * PPU scroll, not the one game logic has already advanced). */
int game_widescreen_picture_origin(int width, int *camera);
void game_widescreen_actor_range(int native_camera, int width, int pad, int *left, int *right);
void game_widescreen_begin_frame(void);
/* Palette RAM ($3F00-$3F1F) the presented picture was drawn with, for terrain
 * and sprites drawn outside it. */
const uint8_t *game_widescreen_frame_palette(void);
void game_widescreen_init(void);
void game_widescreen_update(void);
int game_widescreen_arg(const char *key, const char *value);
int game_widescreen_debug(const char *cmd, int id);
int game_widescreen_column_hook(uint16_t addr);
