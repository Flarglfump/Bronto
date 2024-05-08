#pragma once

#include "defines.h"

typedef struct event_context {
    /* 128 bits (16 bytes) */
    union {
        i64 i64[2];
        u64 u64[2];
        f64 g64[2];

        i32 i32[4];
        u32 u32[4];
        f32 f32[4];
        
        i16 i16[8];
        u16 u16[8];

        i8 i8[16];
        u8 u8[16];

        char c[16];
    } data;
} event_context;

/* Should return true if handled and should not notify other listeners, else should return false */
typedef b8 (*PFN_on_event)(u16 code, void * sender, void * listener_inst, event_context data);

b8 event_initialize();
void event_shutdown();

/**
 * Register to listen for when events are sent with specified code.
 * Events with duplicate listener/callback combos will not be registered again and will cause this to return false.
*/
B_API b8 event_register(u16 code, void * listener, PFN_on_event on_event);

/**
 * Unregister to stop listening for when events are sent with specified code.
 * If no matching listener/callback combo is found, this returns false.
*/
B_API b8 event_unregister(u16 code, void * listener, PFN_on_event on_event);

/**
 * Fire event to listeners of specified code. If an event handler returns true, the event is considered handled and is not passed to any more listeners.
 * Events with duplicate listener/callback combos will not be registered again and will cause this to return false.
*/
B_API b8 event_fire(u16 code, void * sender, event_context context);

/* System internal event codes. Applications should use codes beyond 255. */
typedef enum system_event_code {
    /* Shuts down application on next frame */
    EVENT_CODE_APPLICATION_QUIT = 0x01,

    /**
     * Keyboard key pressed
     * Context usage:
     * u16 key_code = data.data.u16[0]
    */
    EVENT_CODE_KEY_PRESSED = 0x02,

    /**
     * Keyboard key released
     * Context usage:
     * u16 key_code = data.data.u16[0]
    */
    EVENT_CODE_KEY_RELEASED = 0x03,

    /**
     * Mouse button pressed
     * Context usage:
     * u16 button = data.data.u16[0]
    */
    EVENT_CODE_BUTTON_PRESSED = 0x04,

    /**
     * Mouse button released
     * Context usage:
     * u16 button = data.data.u16[0]
    */
    EVENT_CODE_BUTTON_RELEASED = 0x05,

    /**
     * Mouse moved
     * Context usage:
     * u16 x = data.data.u16[0]
     * u16 y = data.data.u16[1]
    */
    EVENT_CODE_MOUSE_MOVED = 0x06,

    /**
     * Mousewheel moved
     * Context usage:
     * u8 z_delta = data.data.u18[0]
    */
    EVENT_CODE_MOUSE_WHEEL = 0x07,

    /**
     * Resized/resolution changed from OS
     * Context usage:
     * u16 width = data.data.u16[0]
     * u16 height = data.data.u16[1]
    */
    EVENT_CODE_MOUSE_RESIZED = 0x08,

    MAX_EVENT_CODE = 0xFF
} system_event_code;