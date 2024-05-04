#pragma once
#include "defines.h"

// Application configuration
typedef struct application_config {
    // Window starting position x axis
    i16 start_pos_x;

    // Window starting position y axis
    i16 start_pos_y;

    // Window starting width
    i16 start_width;

    // Window starting height
    i16 start_height;

    // Application name used in windowing
    char * name;
} application_config;

B_API b8 application_create(application_config * config);
B_API b8 application_run();