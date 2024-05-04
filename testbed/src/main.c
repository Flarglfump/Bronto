#include <core/logger.h>
#include <core/asserts.h>

// TODO: Test
#include <core/application.h>

int main(void) {
    
    // Configure app settings
    application_config config;
    config.start_pos_x = 100;
    config.start_pos_y = 100;
    config.start_width = 1280;
    config.start_height = 720;
    config.name = "Bronto Testbed";
    
    // Create app
    application_create(&config);
    
    
    // Run app
    application_run();
    

    return 0;
}