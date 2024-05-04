#include <core/logger.h>

int main(void) {
    B_FATAL("A test message: %f", 3.14f);
    B_ERROR("A test message: %f", 3.14f);
    B_WARN("A test message: %f", 3.14f);
    B_INFO("A test message: %f", 3.14f);
    B_DEBUG("A test message: %f", 3.14f);
    B_TRACE("A test message: %f", 3.14f);

    return 0;
}