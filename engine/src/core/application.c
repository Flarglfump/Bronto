#include "application.h"
#include "logger.h"
#include "platform/platform.h"

typedef struct application_state {
    b8 is_running;
    b8 is_suspended;
    platform_state platform;
    i16 width;
    i16 height;
    f64 last_time;
} application_state;

static b8 initialized = false;
static application_state app_state;

b8 application_create(application_config * config) {
    if (initialized) {
        B_ERROR("application_create called more than once.");
        return false;
    }

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


    if (!platform_startup(&app_state.platform, config->name, config->start_pos_x, config->start_pos_y, config->start_width, config->start_height)) {
        return false;
    }
    
    initialized = true;

    return true;
}

b8 application_run() {   
    // Game loop!
    while (app_state.is_running) {
        if (!platform_pump_messages(&app_state.platform)) {
            app_state.is_running = false;
        }
    }

    app_state.is_running = false; // Just to be safe

    platform_shutdown(&app_state.platform);

    return true;
}