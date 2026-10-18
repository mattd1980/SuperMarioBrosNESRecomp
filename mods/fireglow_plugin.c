#include "mod_runtime.h"
#include "game_fireglow.h"

#include <stdio.h>
#include <string.h>

#define PACKAGE "super-mario-bros.enhancement.fireball-glow"
#define FEATURE "fireball-glow"

static void reset_fireglow(void) {
    game_fireglow_set_enabled(0);
}

static void activate_fireglow(void) {
    char strength[32], flames[16];
    int percent = 100;
    if (nes_mod_option_value(PACKAGE, FEATURE, "strength", strength, sizeof strength))
        percent = !strcmp(strength, "subtle") ? 60 : !strcmp(strength, "strong") ? 150 : 100;
    int trail = 1;
    if (nes_mod_option_value(PACKAGE, FEATURE, "flames", flames, sizeof flames))
        trail = strcmp(flames, "false") != 0 && strcmp(flames, "0") != 0;
    game_fireglow_configure(percent, trail);
    game_fireglow_set_enabled(1);
}

NES_MOD_CONSTRUCTOR(register_fireglow_plugin) {
    if (!nes_mod_register_reset_callback(reset_fireglow) ||
        !nes_mod_register_activation_plugin("super-mario-bros.fireball-glow", activate_fireglow))
        fprintf(stderr, "[Mods] Failed to register the Fireball Glow plugin\n");
}
