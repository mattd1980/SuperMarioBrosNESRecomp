#include "mod_runtime.h"
#include "game_atmosphere.h"

#include <stdio.h>
#include <string.h>

#define PACKAGE "super-mario-bros.enhancement.atmosphere"
#define FEATURE "atmosphere"

static int option_on(const char *id)
{
    char v[16];
    if (!nes_mod_option_value(PACKAGE, FEATURE, id, v, sizeof v)) return 1;   /* manifest default: on */
    return strcmp(v, "false") != 0 && strcmp(v, "0") != 0;
}

static void reset_atmosphere(void) {
    game_atmosphere_set_enabled(0);
}

static void activate_atmosphere(void) {
    game_atmosphere_configure(option_on("dark"), option_on("water"), option_on("starman"),
                              option_on("items"), option_on("dust"));
    game_atmosphere_set_enabled(1);
}

NES_MOD_CONSTRUCTOR(register_atmosphere_plugin) {
    if (!nes_mod_register_reset_callback(reset_atmosphere) ||
        !nes_mod_register_activation_plugin("super-mario-bros.atmosphere", activate_atmosphere))
        fprintf(stderr, "[Mods] Failed to register the Atmosphere plugin\n");
}
