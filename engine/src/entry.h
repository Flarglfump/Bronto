#pragma once

#include "core/application.h"
#include "core/logger.h"
#include "game_types.h"

/* Extern function, defined in user code to create a game */
extern b8 create_game(game * out_game);

/**
 * The main entry point of the application
*/
int main(void) {

    /* Request game instance from application */
    game game_inst;
    if (!create_game(&game_inst)) {
        B_FATAL("Could not create game!");
        return -1;
    }

    /* Make sure function pointers actually exist */
    if (!game_inst.render || !game_inst.update || !game_inst.initialize || !game_inst.on_resize) {
        B_FATAL("The game's function pointers are not assigned!");
        return -2;
    }


    
    /* Create app */
    if (!application_create(&game_inst)) {
        B_INFO("Application creation failed!");
        return 1;
    }
    
    /* Run app - start game loop */
    if (!application_run()) {
        B_INFO("Application terminated ungracefully!");
        return 2;
    }

    

    return 0;
}