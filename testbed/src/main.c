#include <core/logger.h>
#include <core/asserts.h>

int main(void) {
    B_FATAL("A test message: %f", 3.14f);
    B_ERROR("A test message: %f", 3.14f);
    B_WARN("A test message: %f", 3.14f);
    B_INFO("A test message: %f", 3.14f);
    B_DEBUG("A test message: %f", 3.14f);
    B_TRACE("A test message: %f", 3.14f);

    B_ASSERT(1 == 0); // Will always be false

    return 0;
}