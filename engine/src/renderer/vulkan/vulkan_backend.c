#include "vulkan_backend.h"

#include "vulkan_types.inl"
#include "core/logger.h"

/* Private vulkan context for use in vulkan_backend.c */
static vulkan_context context;

b8 vulkan_renderer_backend_initialize(renderer_backend * backend, const char * application_name, struct platform_state * plat_state) {
  // TODO: Custom allocator
  context.allocator = 0; 

  VkApplicationInfo app_info = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app_info.apiVersion = VK_API_VERSION_1_2;
  app_info.pApplicationName = application_name;
  app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.pEngineName = "Bronto Engine";
  app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);

  VkInstanceCreateInfo create_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  create_info.pApplicationInfo = &app_info;
  create_info.enabledExtensionCount = 0;
  create_info.ppEnabledExtensionNames = 0;
  create_info.enabledLayerCount = 0;
  create_info.ppEnabledLayerNames = 0;

  #if B_PLATFORM_APPLE == 1
    create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  #endif

  VkResult result = vkCreateInstance(&create_info, context.allocator, &(context.instance));
  if (result != VK_SUCCESS) {
    B_ERROR("vkCreateInstance failed with result: %u", result);
    return false;
  }

  B_INFO("Vulkan renderer initialized successfully");
  return true;
}

void vulkan_renderer_backend_shutdown(renderer_backend * backend) {

}

void vulkan_renderer_backend_on_resized(renderer_backend * backend, u16 width, u16 height) {

}

b8 vulkan_renderer_backend_begin_frame(renderer_backend * backend, f32 delta_time) {
  return true;
}
b8 vulkan_renderer_backend_end_frame(renderer_backend * backend, f32 delta_time) {
  return true;
}