#pragma once

#include "core/application.h"

/**
 * Basic game state in game
 * Called for creation by application
 */
typedef struct game {
    /* Config for the app */
    application_config app_config;

    /* Function pointer to game's initialize function */
    b8 (*initialize)(struct game * game_inst);

    /* Function pointer to game's update function */
    b8 (*update)(struct game * game_inst, f32 delta_time);

    /* Function pointer to game's render function */
    b8 (*render)(struct game * game_inst, f32 delta_time);

    /* Function pointer to resize handler function */
    void (*on_resize)(struct game * game_inst, u32 width, u32 height);

    /* State data created and managed by the game, specific to the game */
    void * state;
} game;