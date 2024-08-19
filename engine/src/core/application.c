#include "application.h"
#include "game_types.h"

#include "logger.h"

#include "platform/platform.h"
#include "core/bmemory.h"
#include "core/event.h"
#include "core/input.h"
#include "core/clock.h"

#include "renderer/renderer_frontend.h"

typedef struct application_state {
    game * game_inst;
    b8 is_running;
    b8 is_suspended;
    platform_state platform;
    i16 width;
    i16 height;
    clock clock;
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
    input_initialize();
    

    // TODO: Remove this
    // B_FATAL("A test message: %f", 3.14f);
    // B_ERROR("A test message: %f", 3.14f);
    // B_WARN("A test message: %f", 3.14f);
    // B_INFO("A test message: %f", 3.14f);
    // B_DEBUG("A test message: %f", 3.14f);
    // B_TRACE("A test message: %f", 3.14f);

    app_state.is_running = true;
    app_state.is_suspended = false;

    /* Perform event system initialization */
    B_INFO("Initializing event subsytem...");
    if (!event_initialize()) {
        B_ERROR("Event system failed to initialize. Application cannot continue!");
        return false;
    }
    B_INFO("Event subsytem initialized...");


    /* Perform platform-specific initialization  */
    B_INFO("Initializing platform layer...");
    if (!platform_startup(&app_state.platform,
            game_inst->app_config.name,
            game_inst->app_config.start_pos_x,
            game_inst->app_config.start_pos_y,
            game_inst->app_config.start_width, 
            game_inst->app_config.start_height)) {
        return false;
    }
    B_INFO("Platform layer initialized...");

    /* Initialize renderer */
    B_INFO("Initializing renderer subsystem...");
    if (!renderer_initialize(game_inst->app_config.name, &(app_state.platform))) {
        B_FATAL("Renderer initialization failed")
        return false;
    }
    B_INFO("Renderer subsystem initialized...");

    /* Initialize game */
    B_INFO("Initializing game...");
    if (!app_state.game_inst->initialize(app_state.game_inst)) {
        B_FATAL("Game initialization failed");
        return false;
    }

    B_INFO("Game initialized...");

    /* Handle resize event upon initialization */
    app_state.game_inst->on_resize(app_state.game_inst, app_state.width, app_state.height);

    /* Set private initialized flag to tru once game has been initialized */
    initialized = true;
    return true;
}

b8 application_run() {
    B_INFO("Running game...");

    clock_start(&app_state.clock);
    clock_update(&app_state.clock);
    app_state.last_time = app_state.clock.elapsed;
    f64 running_time = 0;
    u8 frame_count = 0;
    f64 target_frame_seconds = 1.0f / 60; // Target framerate = 60 frames per second

    B_INFO(get_memory_usage_str());


    /* Game loop! */
    while (app_state.is_running) {
        if (!platform_pump_messages(&app_state.platform)) {
            app_state.is_running = false;
        }

        /* Run this only if game is not suspended */
        if (!app_state.is_suspended) {
            /* Update clock and get delta time */
            f64 current_time = app_state.clock.elapsed;
            f64 delta = (current_time - app_state.last_time);
            f64 frame_start_time = platform_get_absolute_time();

            /* Call game's update function */
            if (!app_state.game_inst->update(app_state.game_inst, (f32) delta)) {
                B_FATAL("Game update failed, shutting down!");
                app_state.is_running = false;
                break;
            }

            /* Call game's render function */
            if (!app_state.game_inst->render(app_state.game_inst, (f32) delta)) {
                B_FATAL("Game render failed, shutting down!");
                app_state.is_running = false;
                break;
            }

            // TODO: refactor packet creation
            render_packet packet;
            packet.delta_time = delta;
            renderer_draw_frame(&packet);

            /* Determine elapsed time during frame */
            f64 frame_end_time = platform_get_absolute_time();
            f64 frame_elapsed_time = frame_end_time - frame_start_time;
            running_time += frame_elapsed_time;
            f64 remaining_seconds;

            /* If frame did not use all allotted time, give it back to OS */
            if (remaining_seconds > 0) {
                u64 remaining_ms = (remaining_ms * 1000);

                b8 limit_frames = false; // NOTE: may want to handle this later
                if (remaining_ms > 0 && limit_frames) {
                    platform_sleep(remaining_ms - 1);
                }

                frame_count++;
            }

            /* Send input updates */
            // NOTE: Input updating should always be handled after any input recording should be performed - so update input as last part of game loop.
            input_update((f32) delta);

            /* Update last time */
            app_state.last_time = current_time;
        }
    }

    app_state.is_running = false; // Just to be safe

    clock_stop(&app_state.clock);

    /* Unregister any registered events */

    /* Shut down subsystems */
    event_shutdown();
    input_shutdown();
    renderer_shutdown();

    platform_shutdown(&app_state.platform);

    return true;
}