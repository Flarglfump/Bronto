/*
 * The GPU presenters' hand-written declarations, checked where they can't be
 * run: Direct3D 12 and Vulkan's Win32 side are compiled against a stand-in
 * windows.h (tests/platform/winstub), and the structures are checked against
 * the sizes the real headers give on 64-bit Windows and Linux (d3d12.h,
 * dxgi1_2.h, vulkan_core.h). A wrong field would move them.
 *
 *   make gpu-check      (compiles it only: nothing here can run without Windows)
 */
#include <windows.h>
#include "bronto/bronto.h"
#include <stdio.h>
#include <string.h>

typedef void (*w32_any_fn)(void);
static w32_any_fn w32_proc(HMODULE m, const char *name) {
    FARPROC p = m ? GetProcAddress(m, name) : NULL;
    w32_any_fn f;
    memcpy(&f, &p, sizeof(f));
    return f;
}
#define W32_PROC(T, m, name) ((T)(void (*)(void))w32_proc((m), (name)))

#include "../../src/platform/gpu_d3d12.c"
#include "../../src/platform/gpu_vulkan.c"
#include "../../src/platform/gpu_gl.c"

#define SIZE(T, n) _Static_assert(sizeof(T) == (n), #T " should be " #n " bytes")
/* d3d12.h / dxgi1_2.h, x64 */
SIZE(D12_COMMAND_QUEUE_DESC, 16);
SIZE(D12_SWAP_CHAIN_DESC1, 48);
SIZE(D12_DESCRIPTOR_HEAP_DESC, 16);
SIZE(D12_HEAP_PROPERTIES, 20);
SIZE(D12_RESOURCE_DESC, 56);
SIZE(D12_SRV_DESC, 40);
SIZE(D12_RESOURCE_BARRIER, 32);
SIZE(D12_TEXTURE_COPY_LOCATION, 48);
SIZE(D12_VIEWPORT, 24);
SIZE(D12_DESCRIPTOR_RANGE, 20);
SIZE(D12_ROOT_PARAMETER, 32);
SIZE(D12_STATIC_SAMPLER_DESC, 52);
SIZE(D12_ROOT_SIGNATURE_DESC, 40);
SIZE(D12_PIPELINE_DESC, 656);
_Static_assert(offsetof(D12_PIPELINE_DESC, BlendState) == 120, "D3D12_GRAPHICS_PIPELINE_STATE_DESC.BlendState");
_Static_assert(offsetof(D12_PIPELINE_DESC, InputLayout) == 552, "D3D12_GRAPHICS_PIPELINE_STATE_DESC.InputLayout");
_Static_assert(offsetof(D12_PIPELINE_DESC, RTVFormats) == 580, "D3D12_GRAPHICS_PIPELINE_STATE_DESC.RTVFormats");
_Static_assert(offsetof(D12_TEXTURE_COPY_LOCATION, u) == 16, "D3D12_TEXTURE_COPY_LOCATION's union");
/* vulkan_core.h, 64-bit */
SIZE(VkApplicationInfo, 48);
SIZE(VkInstanceCreateInfo, 64);
SIZE(VkExtensionProperties, 260);
SIZE(VkQueueFamilyProperties, 24);
SIZE(VkPhysicalDeviceMemoryProperties, 520);
SIZE(VkFormatProperties, 12);
SIZE(VkDeviceQueueCreateInfo, 40);
SIZE(VkDeviceCreateInfo, 72);
SIZE(VkSurfaceCapabilitiesKHR, 52);
SIZE(VkSurfaceFormatKHR, 8);
SIZE(VkSwapchainCreateInfoKHR, 104);
SIZE(VkCommandPoolCreateInfo, 24);
SIZE(VkCommandBufferAllocateInfo, 32);
SIZE(VkCommandBufferBeginInfo, 32);
SIZE(VkBufferCreateInfo, 56);
SIZE(VkMemoryRequirements, 24);
SIZE(VkMemoryAllocateInfo, 32);
SIZE(VkImageCreateInfo, 88);
SIZE(VkImageMemoryBarrier, 72);
SIZE(VkBufferImageCopy, 56);
SIZE(VkImageBlit, 80);
SIZE(VkSubmitInfo, 72);
SIZE(VkPresentInfoKHR, 64);
SIZE(VkWin32SurfaceCreateInfoKHR, 40);
_Static_assert(offsetof(VkPhysicalDeviceProperties, deviceName) == 20, "VkPhysicalDeviceProperties.deviceName");
_Static_assert(sizeof(VkPhysicalDeviceProperties) >= 824, "VkPhysicalDeviceProperties: at least the real size");

/* (used, so they're checked as a caller would see them: compiled only, never run or linked) */
void gpu_decl_check_uses(void);
void gpu_decl_check_uses(void) {
    br_recti r = { 0, 0, 0, 0 };
    d12_start(NULL, 0, 0);
    d12_present(NULL, r, 0, 0);
    d12_stop();
    vkp_start_win32(NULL, NULL, 0, 0);
    vkp_present(NULL, r, 0, 0);
    vkp_stop();
    gl_present(NULL, r, 0, 0);
    gl_end();
}
