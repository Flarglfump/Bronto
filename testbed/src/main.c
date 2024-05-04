#include <core/logger.h>
#include <core/asserts.h>

// TODO: Test
#include <platform/platform.h>

int main(void) {
    B_FATAL("A test message: %f", 3.14f);
    B_ERROR("A test message: %f", 3.14f);
    B_WARN("A test message: %f", 3.14f);
    B_INFO("A test message: %f", 3.14f);
    B_DEBUG("A test message: %f", 3.14f);
    B_TRACE("A test message: %f", 3.14f);

    platform_state state;
    if (platform_startup(&state, "Bronto Testbed", 100, 100, 1280, 720)) {
        while (true) {
            platform_pump_messages(&state);
        }
    }

    platform_shutdown(&state);

    return 0;
}