#include "core/event.h"

#include "core/bmemory.h"
#include "containers/darray.h"

typedef struct registered_event {
    void * listener;
    PFN_on_event callback;
} registered_event;

/* Represents single message code for which multiple events may be registered */
typedef struct event_code_entry {
    registered_event * events;
} event_code_entry;

/* Hopefully this is enough codes */
#define MAX_MESSAGE_CODES 16384

/* Event system state structure */
typedef struct event_system_state {
    /* Lookup table for event codes */
    event_code_entry registered[MAX_MESSAGE_CODES];
} event_system_state;

/**
 * Event system internal state
 */
static b8 is_initialized = false;
static event_system_state state;


b8 event_initialize() {
    if (is_initialized == true) {
        return false;
    }

    is_initialized = false;
    b_zero_mem(&state, sizeof(state));

    is_initialized = true;

    return true;
}
void event_shutdown() {
    /* Free events arrays. Objects pointed to should be destroyed on their own */
    for (u16 i = 0; i < MAX_MESSAGE_CODES; i++) {
        if (state.registered[i].events != 0) {
            darray_destroy(state.registered[i].events);
            state.registered[i].events = 0;
        }
    }
}


B_API b8 event_register(u16 code, void * listener, PFN_on_event on_event) {
    if (is_initialized == false) {
        return false;
    }

    if (state.registered[code].events == 0) {
        state.registered[code].events = darray_create(registered_event);
    }

    u64 registered_count = darray_length(state.registered[code].events);
    for (u64 i = 0; i < registered_count; i++) {
        if (state.registered[code].events[i].listener == listener) {
            /* TODO: warn */
            return false;
        }
    }

    /* At this point, no duplicate was found, so clear to register */
    registered_event event;
    event.listener = listener;
    event.callback = on_event;
    darray_push(state.registered[code].events, event);

    return true;
}

B_API b8 event_unregister(u16 code, void * listener, PFN_on_event on_event) {
    if (is_initialized == false) {
        return false;
    }

    /* If nothing is registered for the code, return false */
    if (state.registered[code].events == 0) {
        /* TODO: warn */
        return false;
    }

    u64 registered_count = darray_length(state.registered[code].events);
    for (u64 i = 0; i < registered_count; i++) {
        registered_event e = state.registered[code].events[i];
        if (e.listener == listener && e.callback == on_event) {
            
            registered_event popped_event;
            darray_remove_at(state.registered[code].events, i, &popped_event);
            return true;
        }
    }

    /* Not found */
    return false;
}

B_API b8 event_fire(u16 code, void * sender, event_context context) {
    if (is_initialized == false) {
        return false;
    }

    /* If nothing is registered for the code, return false */
    if (state.registered[code].events == 0) {
        /* TODO: warn */
        return false;
    }

    u64 registered_count = darray_length(state.registered[code].events);
    for (u64 i = 0; i < registered_count; i++) {
        registered_event e = state.registered[code].events[i];
        if (e.callback(code, sender, e.listener, context)) {
            return true;
        }
    }

    /* Not found */
    return false;
}