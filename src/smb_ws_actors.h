#pragma once
#include <stdint.h>

typedef enum { SMB_ENEMIES_NATIVE, SMB_ENEMIES_CLASSIC, SMB_ENEMIES_VIEWPORT } SmbEnemyMode;
void smb_ws_actors_configure(int enabled, SmbEnemyMode mode);
void smb_ws_actors_reset(void);
void smb_ws_actors_begin_frame(void);
void smb_ws_actors_update(void);
void smb_ws_actors_draw(uint32_t *out, int width, int native_x0, int render_camera, const uint8_t *opaque);
int smb_ws_actors_debug(const char *cmd, int id);
int smb_ws_actors_suppress_sprite(int oam_slot);

/* Enemies a mod adds to the current area (not in the ROM's enemy data). The
 * residents own them like authored enemies: loaded at the wide view's edge,
 * moved by the original routines, handed to native slots near Mario. */
typedef struct { uint16_t x; uint8_t y, id; } SmbWsExtraEnemy;
typedef int (*SmbWsExtraEnemies)(SmbWsExtraEnemy *out, int cap);
void smb_ws_actors_set_extra_enemies(SmbWsExtraEnemies source);
/* True while residents load enemies (widescreen enemies on, in gameplay). */
int smb_ws_actors_managing(void);
