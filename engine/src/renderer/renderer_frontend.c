#include "renderer_frontend.h"

#include "renderer_backend.h"

#include "core/logger.h"
#include "core/bmemory.h"

// Backend render context
static renderer_backend * backend = 0;

b8 renderer_initialize(const char * application_name, struct platform_state * plat_state) {
  B_INFO("Allocating renderer backend...");
  backend = b_alloc(sizeof(renderer_backend), MEMORY_TAG_RENDERER);
  B_INFO("Allocated renderer backend.");

  // TODO: make this configurable
  B_INFO("Creating renderer backend...");
  renderer_backend_create(RENDERER_BACKEND_TYPE_VULKAN, plat_state, backend);
  backend->frame_number = 0;
  B_INFO("Created renderer backend.");

  B_INFO("Initializing renderer backend...");
  if (!backend->initialize(backend, application_name, plat_state)) {
    B_FATAL("Could not initialize renderer backend! Shutting down...");
    return false;
  }
  B_INFO("Initialized renderer backend.");

  /* Success! */
  return true;
}
void renderer_shutdown() {
  backend->shutdown(backend);
  b_free(backend, sizeof(renderer_backend), MEMORY_TAG_RENDERER);
}

b8 renderer_begin_frame(f32 delta_time) {
  return backend->begin_frame(backend, delta_time);
}

b8 renderer_end_frame(f32 delta_time) {
  b8 result = backend->end_frame(backend, delta_time);
  (backend->frame_number)++;
  return result;
}

b8 renderer_draw_frame(render_packet * packet) {
  /* If begin frame successful, then can continue */
  if (renderer_begin_frame(packet->delta_time)) {

    /* End frame. If fails, probably can't recover. */
    b8 result = renderer_end_frame(packet->delta_time);

    if (!result) {
      B_ERROR("Call to \"renderer_end_frame\" returned false. Shutting down...")
      return false;
    }
  }

  return true;
}

void renderer_on_resized(u16 width, u16 height) {

}

