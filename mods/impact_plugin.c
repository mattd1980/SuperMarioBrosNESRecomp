#include "mod_runtime.h"
#include "game_impact.h"

#include <stdio.h>
#include <string.h>

#define PACKAGE "super-mario-bros.enhancement.impact-effects"
#define FEATURE "impact-effects"

static int option_on(const char *id)
{
    char v[16];
    if (!nes_mod_option_value(PACKAGE, FEATURE, id, v, sizeof v)) return 1;   /* manifest default: on */
    return strcmp(v, "false") != 0 && strcmp(v, "0") != 0;
}

static void reset_impact(void) {
    game_impact_set_enabled(0);
}

static void activate_impact(void) {
    char strength[32];
    int percent = 70;
    if (nes_mod_option_value(PACKAGE, FEATURE, "strength", strength, sizeof strength))
        percent = !strcmp(strength, "normal") ? 100 : !strcmp(strength, "strong") ? 140 : 70;
    game_impact_configure(percent, option_on("stomp"), option_on("fireball"), option_on("bricks"), option_on("bowser"));
    game_impact_set_enabled(1);
}

NES_MOD_CONSTRUCTOR(register_impact_plugin) {
    if (!nes_mod_register_reset_callback(reset_impact) ||
        !nes_mod_register_activation_plugin("super-mario-bros.impact-effects", activate_impact))
        fprintf(stderr, "[Mods] Failed to register the Impact Effects plugin\n");
}
