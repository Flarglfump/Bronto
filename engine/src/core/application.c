#include "application.h"
#include "game_types.h"

#include "logger.h"
#include "platform/platform.h"

typedef struct application_state {
    game * game_inst;
    b8 is_running;
    b8 is_suspended;
    platform_state platform;
    i16 width;
    i16 height;
    f64 last_time;
} application_state;

static b8 initialized = false;
static application_state app_state;

b8 application_create(game * game_inst) {
    if (initialized) {
        B_ERROR("application_create called more than once.");
        return false;
    }

    app_state.game_inst = game_inst;

    // Initialize subsystems
    initialize_logging();

    // TODO: Remove this
    B_FATAL("A test message: %f", 3.14f);
    B_ERROR("A test message: %f", 3.14f);
    B_WARN("A test message: %f", 3.14f);
    B_INFO("A test message: %f", 3.14f);
    B_DEBUG("A test message: %f", 3.14f);
    B_TRACE("A test message: %f", 3.14f);

    app_state.is_running = true;
    app_state.is_suspended = false;

    /* Perform platform-sepcific initialization  */
    if (!platform_startup(&app_state.platform,
            game_inst->app_config.name,
            game_inst->app_config.start_pos_x,
            game_inst->app_config.start_pos_y,
            game_inst->app_config.start_width, 
            game_inst->app_config.start_height)) {
        return false;
    }
    
    /* Initialize game */
    if (!app_state.game_inst->initialize(app_state.game_inst)) {
        B_FATAL("Game initialization failed");
        return false;
    }

    /* Handle resize event upon initialization */
    app_state.game_inst->on_resize(app_state.game_inst, app_state.width, app_state.height);

    /* Set private initialized flag to tru once game has been initialized */
    initialized = true;
    return true;
}

b8 application_run() {   

    /* Game loop! */
    while (app_state.is_running) {
        if (!platform_pump_messages(&app_state.platform)) {
            app_state.is_running = false;
        }

        /* Run this only if game is not suspended */
        if (!app_state.is_suspended) {
            /* Call game's update function */
            if (!app_state.game_inst->update(app_state.game_inst, (f32) 0)) {
                B_FATAL("Game update failed, shutting down!");
                app_state.is_running = false;
                break;
            }

            /* Call game's render function */
            if (!app_state.game_inst->render(app_state.game_inst, (f32) 0)) {
                B_FATAL("Game render failed, shutting down!");
                app_state.is_running = false;
                break;
            }
        }
    }

    app_state.is_running = false; // Just to be safe

    platform_shutdown(&app_state.platform);

    return true;
}