/*
 * Bronto Engine - showing the framebuffer through Vulkan (platform.h, br_gpu).
 *
 * Shared by the X11 and Win32 backends (macOS has no Vulkan of its own). The
 * Vulkan loader (libvulkan.so.1, vulkan-1.dll) is loaded at run time and the
 * part of the API used here is declared below, so building needs no Vulkan SDK
 * and running without a Vulkan driver just falls back.
 *
 * Each frame: the framebuffer is copied into a host-visible buffer, from there
 * into an image the size of the frame, the swapchain image is cleared black, and
 * the frame is blitted into the viewport with nearest-neighbour filtering - all
 * transfer commands, so no shaders or render passes are needed. One frame is in
 * flight at a time (the CPU waits for the last one before reusing its buffers);
 * the swapchain presents in FIFO order (vsync). A window that changed size gets
 * a new swapchain.
 *
 * Included by platform_x11.c and platform_win32.c, which call
 *   vkp_start_xlib(display, window, w, h) / vkp_start_win32(instance, hwnd, w, h),
 *   vkp_present(frame, viewport, w, h) (false: Vulkan stopped working) and vkp_stop().
 */
#if defined(_WIN32)
#  define BR_VKAPI __stdcall
#else
#  define BR_VKAPI
#endif

/* ---- the declarations (the subset of vulkan_core.h used here) ---------------------------- */
typedef int32_t VkResult;
typedef uint32_t VkFlags;
typedef uint32_t VkBool32;
typedef uint64_t VkDeviceSize;
typedef struct VkInstance_T *VkInstance;
typedef struct VkPhysicalDevice_T *VkPhysicalDevice;
typedef struct VkDevice_T *VkDevice;
typedef struct VkQueue_T *VkQueue;
typedef struct VkCommandBuffer_T *VkCommandBuffer;
typedef uint64_t VkSurfaceKHR, VkSwapchainKHR, VkImage, VkBuffer, VkDeviceMemory, VkSemaphore, VkFence, VkCommandPool;
typedef void (BR_VKAPI *PFN_vkVoidFunction)(void);

#define VKP_SUCCESS 0
#define VKP_SUBOPTIMAL 1000001003
#define VKP_ERROR_OUT_OF_DATE (-1000001004)
#define VKP_ST_APPLICATION_INFO 0
#define VKP_ST_INSTANCE_CREATE_INFO 1
#define VKP_ST_DEVICE_QUEUE_CREATE_INFO 2
#define VKP_ST_DEVICE_CREATE_INFO 3
#define VKP_ST_SUBMIT_INFO 4
#define VKP_ST_MEMORY_ALLOCATE_INFO 5
#define VKP_ST_FENCE_CREATE_INFO 8
#define VKP_ST_SEMAPHORE_CREATE_INFO 9
#define VKP_ST_BUFFER_CREATE_INFO 12
#define VKP_ST_IMAGE_CREATE_INFO 14
#define VKP_ST_COMMAND_POOL_CREATE_INFO 39
#define VKP_ST_COMMAND_BUFFER_ALLOCATE_INFO 40
#define VKP_ST_COMMAND_BUFFER_BEGIN_INFO 42
#define VKP_ST_IMAGE_MEMORY_BARRIER 45
#define VKP_ST_SWAPCHAIN_CREATE_INFO 1000001000
#define VKP_ST_PRESENT_INFO 1000001001
#define VKP_ST_XLIB_SURFACE_CREATE_INFO 1000004000
#define VKP_ST_WIN32_SURFACE_CREATE_INFO 1000009000

#define VKP_FORMAT_R8G8B8A8_UNORM 37
#define VKP_FORMAT_B8G8R8A8_UNORM 44
#define VKP_FORMAT_FEATURE_BLIT_SRC 0x400u
#define VKP_FORMAT_FEATURE_BLIT_DST 0x800u
#define VKP_IMAGE_USAGE_TRANSFER_SRC 0x1u
#define VKP_IMAGE_USAGE_TRANSFER_DST 0x2u
#define VKP_BUFFER_USAGE_TRANSFER_SRC 0x1u
#define VKP_MEMORY_DEVICE_LOCAL 0x1u
#define VKP_MEMORY_HOST_VISIBLE 0x2u
#define VKP_MEMORY_HOST_COHERENT 0x4u
#define VKP_QUEUE_GRAPHICS 0x1u
#define VKP_LAYOUT_UNDEFINED 0
#define VKP_LAYOUT_TRANSFER_SRC 6
#define VKP_LAYOUT_TRANSFER_DST 7
#define VKP_LAYOUT_PRESENT_SRC 1000001002
#define VKP_ACCESS_TRANSFER_READ 0x800u
#define VKP_ACCESS_TRANSFER_WRITE 0x1000u
#define VKP_ACCESS_MEMORY_READ 0x8000u
#define VKP_STAGE_TOP_OF_PIPE 0x1u
#define VKP_STAGE_TRANSFER 0x1000u
#define VKP_STAGE_BOTTOM_OF_PIPE 0x2000u
#define VKP_ASPECT_COLOR 0x1u
#define VKP_FILTER_NEAREST 0
#define VKP_PRESENT_FIFO 2
#define VKP_COMPOSITE_OPAQUE 0x1u
#define VKP_COMPOSITE_INHERIT 0x8u
#define VKP_TRANSFORM_IDENTITY 0x1u
#define VKP_QUEUE_FAMILY_IGNORED (~0u)

typedef struct { uint32_t width, height; } VkExtent2D;
typedef struct { uint32_t width, height, depth; } VkExtent3D;
typedef struct { int32_t x, y, z; } VkOffset3D;
typedef struct {
    int32_t sType; const void *pNext; const char *pApplicationName; uint32_t applicationVersion;
    const char *pEngineName; uint32_t engineVersion; uint32_t apiVersion;
} VkApplicationInfo;
typedef struct {
    int32_t sType; const void *pNext; VkFlags flags; const VkApplicationInfo *pApplicationInfo;
    uint32_t enabledLayerCount; const char *const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount; const char *const *ppEnabledExtensionNames;
} VkInstanceCreateInfo;
typedef struct { char extensionName[256]; uint32_t specVersion; } VkExtensionProperties;
typedef struct {
    uint32_t apiVersion, driverVersion, vendorID, deviceID; int32_t deviceType; char deviceName[256];
    uint8_t pipelineCacheUUID[16];
    uint8_t rest[2048];                  /* the limits and sparse properties (not read here; more than they take) */
} VkPhysicalDeviceProperties;
typedef struct { VkFlags queueFlags; uint32_t queueCount; uint32_t timestampValidBits; VkExtent3D minImageTransferGranularity; } VkQueueFamilyProperties;
typedef struct { VkFlags propertyFlags; uint32_t heapIndex; } VkMemoryType;
typedef struct { VkDeviceSize size; VkFlags flags; } VkMemoryHeap;
typedef struct { uint32_t memoryTypeCount; VkMemoryType memoryTypes[32]; uint32_t memoryHeapCount; VkMemoryHeap memoryHeaps[16]; } VkPhysicalDeviceMemoryProperties;
typedef struct { VkFlags linearTilingFeatures, optimalTilingFeatures, bufferFeatures; } VkFormatProperties;
typedef struct {
    int32_t sType; const void *pNext; VkFlags flags; uint32_t queueFamilyIndex; uint32_t queueCount; const float *pQueuePriorities;
} VkDeviceQueueCreateInfo;
typedef struct {
    int32_t sType; const void *pNext; VkFlags flags; uint32_t queueCreateInfoCount; const VkDeviceQueueCreateInfo *pQueueCreateInfos;
    uint32_t enabledLayerCount; const char *const *ppEnabledLayerNames; uint32_t enabledExtensionCount;
    const char *const *ppEnabledExtensionNames; const void *pEnabledFeatures;
} VkDeviceCreateInfo;
typedef struct {
    uint32_t minImageCount, maxImageCount; VkExtent2D currentExtent, minImageExtent, maxImageExtent; uint32_t maxImageArrayLayers;
    VkFlags supportedTransforms; uint32_t currentTransform; VkFlags supportedCompositeAlpha; VkFlags supportedUsageFlags;
} VkSurfaceCapabilitiesKHR;
typedef struct { int32_t format; int32_t colorSpace; } VkSurfaceFormatKHR;
typedef struct {
    int32_t sType; const void *pNext; VkFlags flags; VkSurfaceKHR surface; uint32_t minImageCount; int32_t imageFormat;
    int32_t imageColorSpace; VkExtent2D imageExtent; uint32_t imageArrayLayers; VkFlags imageUsage; int32_t imageSharingMode;
    uint32_t queueFamilyIndexCount; const uint32_t *pQueueFamilyIndices; uint32_t preTransform; uint32_t compositeAlpha;
    int32_t presentMode; VkBool32 clipped; VkSwapchainKHR oldSwapchain;
} VkSwapchainCreateInfoKHR;
typedef struct { int32_t sType; const void *pNext; VkFlags flags; uint32_t queueFamilyIndex; } VkCommandPoolCreateInfo;
typedef struct { int32_t sType; const void *pNext; VkCommandPool commandPool; int32_t level; uint32_t commandBufferCount; } VkCommandBufferAllocateInfo;
typedef struct { int32_t sType; const void *pNext; VkFlags flags; const void *pInheritanceInfo; } VkCommandBufferBeginInfo;
typedef struct {
    int32_t sType; const void *pNext; VkFlags flags; VkDeviceSize size; VkFlags usage; int32_t sharingMode;
    uint32_t queueFamilyIndexCount; const uint32_t *pQueueFamilyIndices;
} VkBufferCreateInfo;
typedef struct { VkDeviceSize size, alignment; uint32_t memoryTypeBits; } VkMemoryRequirements;
typedef struct { int32_t sType; const void *pNext; VkDeviceSize allocationSize; uint32_t memoryTypeIndex; } VkMemoryAllocateInfo;
typedef struct {
    int32_t sType; const void *pNext; VkFlags flags; int32_t imageType; int32_t format; VkExtent3D extent; uint32_t mipLevels;
    uint32_t arrayLayers; uint32_t samples; int32_t tiling; VkFlags usage; int32_t sharingMode; uint32_t queueFamilyIndexCount;
    const uint32_t *pQueueFamilyIndices; int32_t initialLayout;
} VkImageCreateInfo;
typedef struct { int32_t sType; const void *pNext; VkFlags flags; } VkSemaphoreCreateInfo, VkFenceCreateInfo;
typedef struct { VkFlags aspectMask; uint32_t baseMipLevel, levelCount, baseArrayLayer, layerCount; } VkImageSubresourceRange;
typedef struct {
    int32_t sType; const void *pNext; VkFlags srcAccessMask, dstAccessMask; int32_t oldLayout, newLayout;
    uint32_t srcQueueFamilyIndex, dstQueueFamilyIndex; VkImage image; VkImageSubresourceRange subresourceRange;
} VkImageMemoryBarrier;
typedef struct { VkFlags aspectMask; uint32_t mipLevel, baseArrayLayer, layerCount; } VkImageSubresourceLayers;
typedef struct {
    VkDeviceSize bufferOffset; uint32_t bufferRowLength, bufferImageHeight; VkImageSubresourceLayers imageSubresource;
    VkOffset3D imageOffset; VkExtent3D imageExtent;
} VkBufferImageCopy;
typedef struct { VkImageSubresourceLayers srcSubresource; VkOffset3D srcOffsets[2]; VkImageSubresourceLayers dstSubresource; VkOffset3D dstOffsets[2]; } VkImageBlit;
typedef union { float float32[4]; int32_t int32[4]; uint32_t uint32[4]; } VkClearColorValue;
typedef struct {
    int32_t sType; const void *pNext; uint32_t waitSemaphoreCount; const VkSemaphore *pWaitSemaphores; const VkFlags *pWaitDstStageMask;
    uint32_t commandBufferCount; const VkCommandBuffer *pCommandBuffers; uint32_t signalSemaphoreCount; const VkSemaphore *pSignalSemaphores;
} VkSubmitInfo;
typedef struct {
    int32_t sType; const void *pNext; uint32_t waitSemaphoreCount; const VkSemaphore *pWaitSemaphores; uint32_t swapchainCount;
    const VkSwapchainKHR *pSwapchains; const uint32_t *pImageIndices; VkResult *pResults;
} VkPresentInfoKHR;
#if defined(_WIN32)
typedef struct { int32_t sType; const void *pNext; VkFlags flags; HINSTANCE hinstance; HWND hwnd; } VkWin32SurfaceCreateInfoKHR;
#else
typedef struct { int32_t sType; const void *pNext; VkFlags flags; Display *dpy; Window window; } VkXlibSurfaceCreateInfoKHR;
#endif

/* The functions: instance-level, then device-level (found through the instance/device). */
#define VKP_INSTANCE_FUNCS(F) \
    F(void, vkDestroyInstance, (VkInstance, const void *)) \
    F(VkResult, vkEnumeratePhysicalDevices, (VkInstance, uint32_t *, VkPhysicalDevice *)) \
    F(void, vkGetPhysicalDeviceProperties, (VkPhysicalDevice, VkPhysicalDeviceProperties *)) \
    F(void, vkGetPhysicalDeviceQueueFamilyProperties, (VkPhysicalDevice, uint32_t *, VkQueueFamilyProperties *)) \
    F(void, vkGetPhysicalDeviceMemoryProperties, (VkPhysicalDevice, VkPhysicalDeviceMemoryProperties *)) \
    F(void, vkGetPhysicalDeviceFormatProperties, (VkPhysicalDevice, int32_t, VkFormatProperties *)) \
    F(VkResult, vkEnumerateDeviceExtensionProperties, (VkPhysicalDevice, const char *, uint32_t *, VkExtensionProperties *)) \
    F(VkResult, vkGetPhysicalDeviceSurfaceSupportKHR, (VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32 *)) \
    F(VkResult, vkGetPhysicalDeviceSurfaceCapabilitiesKHR, (VkPhysicalDevice, VkSurfaceKHR, VkSurfaceCapabilitiesKHR *)) \
    F(VkResult, vkGetPhysicalDeviceSurfaceFormatsKHR, (VkPhysicalDevice, VkSurfaceKHR, uint32_t *, VkSurfaceFormatKHR *)) \
    F(void, vkDestroySurfaceKHR, (VkInstance, VkSurfaceKHR, const void *)) \
    F(VkResult, vkCreateDevice, (VkPhysicalDevice, const VkDeviceCreateInfo *, const void *, VkDevice *)) \
    F(PFN_vkVoidFunction, vkGetDeviceProcAddr, (VkDevice, const char *))
#define VKP_DEVICE_FUNCS(F) \
    F(void, vkDestroyDevice, (VkDevice, const void *)) \
    F(void, vkGetDeviceQueue, (VkDevice, uint32_t, uint32_t, VkQueue *)) \
    F(VkResult, vkDeviceWaitIdle, (VkDevice)) \
    F(VkResult, vkCreateSwapchainKHR, (VkDevice, const VkSwapchainCreateInfoKHR *, const void *, VkSwapchainKHR *)) \
    F(void, vkDestroySwapchainKHR, (VkDevice, VkSwapchainKHR, const void *)) \
    F(VkResult, vkGetSwapchainImagesKHR, (VkDevice, VkSwapchainKHR, uint32_t *, VkImage *)) \
    F(VkResult, vkAcquireNextImageKHR, (VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t *)) \
    F(VkResult, vkQueuePresentKHR, (VkQueue, const VkPresentInfoKHR *)) \
    F(VkResult, vkQueueSubmit, (VkQueue, uint32_t, const VkSubmitInfo *, VkFence)) \
    F(VkResult, vkCreateCommandPool, (VkDevice, const VkCommandPoolCreateInfo *, const void *, VkCommandPool *)) \
    F(void, vkDestroyCommandPool, (VkDevice, VkCommandPool, const void *)) \
    F(VkResult, vkAllocateCommandBuffers, (VkDevice, const VkCommandBufferAllocateInfo *, VkCommandBuffer *)) \
    F(VkResult, vkBeginCommandBuffer, (VkCommandBuffer, const VkCommandBufferBeginInfo *)) \
    F(VkResult, vkEndCommandBuffer, (VkCommandBuffer)) \
    F(VkResult, vkResetCommandBuffer, (VkCommandBuffer, VkFlags)) \
    F(void, vkCmdPipelineBarrier, (VkCommandBuffer, VkFlags, VkFlags, VkFlags, uint32_t, const void *, uint32_t, const void *, uint32_t, const VkImageMemoryBarrier *)) \
    F(void, vkCmdCopyBufferToImage, (VkCommandBuffer, VkBuffer, VkImage, int32_t, uint32_t, const VkBufferImageCopy *)) \
    F(void, vkCmdBlitImage, (VkCommandBuffer, VkImage, int32_t, VkImage, int32_t, uint32_t, const VkImageBlit *, int32_t)) \
    F(void, vkCmdClearColorImage, (VkCommandBuffer, VkImage, int32_t, const VkClearColorValue *, uint32_t, const VkImageSubresourceRange *)) \
    F(VkResult, vkCreateBuffer, (VkDevice, const VkBufferCreateInfo *, const void *, VkBuffer *)) \
    F(void, vkDestroyBuffer, (VkDevice, VkBuffer, const void *)) \
    F(void, vkGetBufferMemoryRequirements, (VkDevice, VkBuffer, VkMemoryRequirements *)) \
    F(VkResult, vkBindBufferMemory, (VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize)) \
    F(VkResult, vkCreateImage, (VkDevice, const VkImageCreateInfo *, const void *, VkImage *)) \
    F(void, vkDestroyImage, (VkDevice, VkImage, const void *)) \
    F(void, vkGetImageMemoryRequirements, (VkDevice, VkImage, VkMemoryRequirements *)) \
    F(VkResult, vkBindImageMemory, (VkDevice, VkImage, VkDeviceMemory, VkDeviceSize)) \
    F(VkResult, vkAllocateMemory, (VkDevice, const VkMemoryAllocateInfo *, const void *, VkDeviceMemory *)) \
    F(void, vkFreeMemory, (VkDevice, VkDeviceMemory, const void *)) \
    F(VkResult, vkMapMemory, (VkDevice, VkDeviceMemory, VkDeviceSize, VkDeviceSize, VkFlags, void **)) \
    F(void, vkUnmapMemory, (VkDevice, VkDeviceMemory)) \
    F(VkResult, vkCreateSemaphore, (VkDevice, const VkSemaphoreCreateInfo *, const void *, VkSemaphore *)) \
    F(void, vkDestroySemaphore, (VkDevice, VkSemaphore, const void *)) \
    F(VkResult, vkCreateFence, (VkDevice, const VkFenceCreateInfo *, const void *, VkFence *)) \
    F(void, vkDestroyFence, (VkDevice, VkFence, const void *)) \
    F(VkResult, vkWaitForFences, (VkDevice, uint32_t, const VkFence *, VkBool32, uint64_t)) \
    F(VkResult, vkResetFences, (VkDevice, uint32_t, const VkFence *))

#define VKP_DECLARE(ret, name, args) typedef ret (BR_VKAPI *name##_fn) args;
VKP_INSTANCE_FUNCS(VKP_DECLARE)
VKP_DEVICE_FUNCS(VKP_DECLARE)
#undef VKP_DECLARE
typedef PFN_vkVoidFunction (BR_VKAPI *vkGetInstanceProcAddr_fn)(VkInstance, const char *);
typedef VkResult (BR_VKAPI *vkCreateInstance_fn)(const VkInstanceCreateInfo *, const void *, VkInstance *);
typedef VkResult (BR_VKAPI *vkEnumerateInstanceExtensionProperties_fn)(const char *, uint32_t *, VkExtensionProperties *);
#if defined(_WIN32)
typedef VkResult (BR_VKAPI *vkCreateWin32SurfaceKHR_fn)(VkInstance, const VkWin32SurfaceCreateInfoKHR *, const void *, VkSurfaceKHR *);
#else
typedef VkResult (BR_VKAPI *vkCreateXlibSurfaceKHR_fn)(VkInstance, const VkXlibSurfaceCreateInfoKHR *, const void *, VkSurfaceKHR *);
#endif

#define VKP_MAX_IMAGES 8

static struct {
    void *lib;
    vkGetInstanceProcAddr_fn vkGetInstanceProcAddr;
#define VKP_FIELD(ret, name, args) name##_fn name;
    VKP_INSTANCE_FUNCS(VKP_FIELD)
    VKP_DEVICE_FUNCS(VKP_FIELD)
#undef VKP_FIELD
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice gpu;
    VkDevice device;
    VkQueue queue;
    uint32_t family;
    VkPhysicalDeviceMemoryProperties memory;
    VkSwapchainKHR swapchain;
    VkImage images[VKP_MAX_IMAGES];
    VkSemaphore done[VKP_MAX_IMAGES];        /* per swapchain image: its commands finished (present waits) */
    uint32_t image_count;
    int32_t format;
    VkExtent2D extent;
    bool rebuild;                            /* the swapchain is out of date: make it again */
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkSemaphore acquired;
    VkFence fence;
    VkBuffer staging;                        /* the frame's pixels, written by the CPU */
    VkDeviceMemory staging_memory;
    void *staging_map;
    VkDeviceSize staging_size;
    VkImage frame;                           /* ... on the GPU, the frame's size */
    VkDeviceMemory frame_memory;
    int frame_w, frame_h;
    int want_w, want_h;                      /* the window size the swapchain was made for */
} VKP;

static void *vkp_lib_open(void) {
#if defined(_WIN32)
    return (void *)LoadLibraryA("vulkan-1.dll");
#else
    void *h = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    return h ? h : dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
#endif
}

static void *vkp_lib_sym(void *lib, const char *name) {
#if defined(_WIN32)
    FARPROC f = GetProcAddress((HMODULE)lib, name);
    void *p;
    memcpy(&p, &f, sizeof(p));
    return p;
#else
    return dlsym(lib, name);
#endif
}

static void *vkp_instance_proc(const char *name) {
    PFN_vkVoidFunction f = VKP.vkGetInstanceProcAddr(VKP.instance, name);
    void *p;
    memcpy(&p, &f, sizeof(p));
    return p;
}

static void *vkp_device_proc(const char *name) {
    PFN_vkVoidFunction f = VKP.vkGetDeviceProcAddr(VKP.device, name);
    void *p;
    memcpy(&p, &f, sizeof(p));
    return p;
}

static bool vkp_has_ext(const VkExtensionProperties *list, uint32_t n, const char *name) {
    uint32_t i;
    for (i = 0; i < n; ++i) if (strcmp(list[i].extensionName, name) == 0) return true;
    return false;
}

static int vkp_memory_type(uint32_t bits, VkFlags want, VkFlags fallback) {
    uint32_t i;
    for (i = 0; i < VKP.memory.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (VKP.memory.memoryTypes[i].propertyFlags & want) == want) return (int)i;
    for (i = 0; i < VKP.memory.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (VKP.memory.memoryTypes[i].propertyFlags & fallback) == fallback) return (int)i;
    return -1;
}

/* ---- the instance and the surface ---------------------------------------------------------- */

static bool vkp_instance(const char *surface_ext) {
    vkCreateInstance_fn create;
    vkEnumerateInstanceExtensionProperties_fn enum_ext;
    VkExtensionProperties *exts = NULL;
    uint32_t n = 0;
    const char *wanted[2];
    VkApplicationInfo app;
    VkInstanceCreateInfo ci;
    VkResult r;
    if (!VKP.lib && !(VKP.lib = vkp_lib_open())) { BR_INFO("Vulkan: no Vulkan loader on this computer"); return false; }
    {
        void *p = vkp_lib_sym(VKP.lib, "vkGetInstanceProcAddr");
        memcpy(&VKP.vkGetInstanceProcAddr, &p, sizeof(p));
    }
    if (!VKP.vkGetInstanceProcAddr) { BR_INFO("Vulkan: the loader has no vkGetInstanceProcAddr"); return false; }
    VKP.instance = NULL;
    { void *p = vkp_instance_proc("vkCreateInstance"); memcpy(&create, &p, sizeof(p)); }
    { void *p = vkp_instance_proc("vkEnumerateInstanceExtensionProperties"); memcpy(&enum_ext, &p, sizeof(p)); }
    if (!create || !enum_ext) return false;
    if (enum_ext(NULL, &n, NULL) != VKP_SUCCESS || n == 0 || !(exts = (VkExtensionProperties *)br_alloc(sizeof(*exts) * n))) return false;
    enum_ext(NULL, &n, exts);
    if (!vkp_has_ext(exts, n, "VK_KHR_surface") || !vkp_has_ext(exts, n, surface_ext)) {
        BR_INFO("Vulkan: no %s", vkp_has_ext(exts, n, "VK_KHR_surface") ? surface_ext : "VK_KHR_surface");
        br_free(exts);
        return false;
    }
    br_free(exts);
    wanted[0] = "VK_KHR_surface";
    wanted[1] = surface_ext;
    memset(&app, 0, sizeof(app));
    app.sType = VKP_ST_APPLICATION_INFO;
    app.pApplicationName = "Bronto";
    app.pEngineName = "Bronto";
    app.apiVersion = 1u << 22;                               /* Vulkan 1.0 is all this needs */
    memset(&ci, 0, sizeof(ci));
    ci.sType = VKP_ST_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = 2;
    ci.ppEnabledExtensionNames = wanted;
    if ((r = create(&ci, NULL, &VKP.instance)) != VKP_SUCCESS) { BR_INFO("Vulkan: no instance (error %d)", (int)r); VKP.instance = NULL; return false; }
#define VKP_LOAD(ret, name, args) \
    { void *p_ = vkp_instance_proc(#name); memcpy(&VKP.name, &p_, sizeof(p_)); if (!VKP.name) { BR_INFO("Vulkan: no %s", #name); return false; } }
    VKP_INSTANCE_FUNCS(VKP_LOAD)
#undef VKP_LOAD
    return true;
}

/* ---- the device ----------------------------------------------------------------------------- */

static bool vkp_device(char *info, size_t info_size) {
    VkPhysicalDevice gpus[16];
    uint32_t n = 16, i, best = ~0u, best_family = 0;
    int best_score = -1;
    VkPhysicalDeviceProperties props;
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qi;
    VkDeviceCreateInfo di;
    const char *ext = "VK_KHR_swapchain";
    VkResult r;
    if (VKP.vkEnumeratePhysicalDevices(VKP.instance, &n, gpus) < 0 || n == 0) { BR_INFO("Vulkan: no GPU"); return false; }
    for (i = 0; i < n; ++i) {                                 /* a GPU with a queue that draws and presents here */
        VkQueueFamilyProperties fam[32];
        VkExtensionProperties *exts;
        VkFormatProperties fp;
        uint32_t nf = 32, ne = 0, f;
        int score;
        VKP.vkGetPhysicalDeviceProperties(gpus[i], &props);
        VKP.vkGetPhysicalDeviceFormatProperties(gpus[i], VKP_FORMAT_R8G8B8A8_UNORM, &fp);
        if (!(fp.optimalTilingFeatures & VKP_FORMAT_FEATURE_BLIT_SRC)) continue;
        if (VKP.vkEnumerateDeviceExtensionProperties(gpus[i], NULL, &ne, NULL) != VKP_SUCCESS || !ne) continue;
        if (!(exts = (VkExtensionProperties *)br_alloc(sizeof(*exts) * ne))) continue;
        VKP.vkEnumerateDeviceExtensionProperties(gpus[i], NULL, &ne, exts);
        f = vkp_has_ext(exts, ne, ext);
        br_free(exts);
        if (!f) continue;
        VKP.vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &nf, NULL);
        if (nf > 32) nf = 32;
        VKP.vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &nf, fam);
        for (f = 0; f < nf; ++f) {
            VkBool32 present = 0;
            if (!(fam[f].queueFlags & VKP_QUEUE_GRAPHICS)) continue;
            if (VKP.vkGetPhysicalDeviceSurfaceSupportKHR(gpus[i], f, VKP.surface, &present) != VKP_SUCCESS || !present) continue;
            score = props.deviceType == 2 ? 3 : props.deviceType == 1 ? 2 : props.deviceType == 3 ? 1 : 0;   /* discrete, integrated, virtual */
            if (score > best_score) { best_score = score; best = i; best_family = f; }
            break;
        }
    }
    if (best == ~0u) { BR_INFO("Vulkan: no GPU here can show frames in this window"); return false; }
    VKP.gpu = gpus[best];
    VKP.family = best_family;
    VKP.vkGetPhysicalDeviceProperties(VKP.gpu, &props);
    VKP.vkGetPhysicalDeviceMemoryProperties(VKP.gpu, &VKP.memory);
    memset(&qi, 0, sizeof(qi));
    qi.sType = VKP_ST_DEVICE_QUEUE_CREATE_INFO;
    qi.queueFamilyIndex = VKP.family;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    memset(&di, 0, sizeof(di));
    di.sType = VKP_ST_DEVICE_CREATE_INFO;
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = 1;
    di.ppEnabledExtensionNames = &ext;
    if ((r = VKP.vkCreateDevice(VKP.gpu, &di, NULL, &VKP.device)) != VKP_SUCCESS) { BR_INFO("Vulkan: no device (error %d)", (int)r); VKP.device = NULL; return false; }
#define VKP_LOAD(ret, name, args) \
    { void *p_ = vkp_device_proc(#name); memcpy(&VKP.name, &p_, sizeof(p_)); if (!VKP.name) { BR_INFO("Vulkan: no %s", #name); return false; } }
    VKP_DEVICE_FUNCS(VKP_LOAD)
#undef VKP_LOAD
    VKP.vkGetDeviceQueue(VKP.device, VKP.family, 0, &VKP.queue);
    snprintf(info, info_size, "Vulkan %u.%u, %.120s", props.apiVersion >> 22, (props.apiVersion >> 12) & 0x3ffu, props.deviceName);
    return true;
}

/* ---- the swapchain ---------------------------------------------------------------------------- */

static void vkp_drop_swapchain(void) {
    uint32_t i;
    for (i = 0; i < VKP.image_count; ++i) if (VKP.done[i]) { VKP.vkDestroySemaphore(VKP.device, VKP.done[i], NULL); VKP.done[i] = 0; }
    if (VKP.swapchain) VKP.vkDestroySwapchainKHR(VKP.device, VKP.swapchain, NULL);
    VKP.swapchain = 0;
    VKP.image_count = 0;
}

static bool vkp_swapchain(int win_w, int win_h) {
    VkSurfaceCapabilitiesKHR caps;
    VkSurfaceFormatKHR formats[64];
    uint32_t nf = 64, i;
    VkSwapchainCreateInfoKHR ci;
    VkSemaphoreCreateInfo si;
    VkSwapchainKHR old = VKP.swapchain;
    VkFormatProperties fp;
    VkResult r;
    if (VKP.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VKP.gpu, VKP.surface, &caps) != VKP_SUCCESS) return false;
    if (!(caps.supportedUsageFlags & VKP_IMAGE_USAGE_TRANSFER_DST)) { BR_INFO("Vulkan: the window's images can't be drawn into by copying"); return false; }
    if (VKP.vkGetPhysicalDeviceSurfaceFormatsKHR(VKP.gpu, VKP.surface, &nf, formats) < 0 || nf == 0) return false;
    VKP.format = -1;
    for (i = 0; i < nf && VKP.format < 0; ++i) {             /* plain (not sRGB) 8-bit, as the frame is */
        if (formats[i].format != VKP_FORMAT_B8G8R8A8_UNORM && formats[i].format != VKP_FORMAT_R8G8B8A8_UNORM) continue;
        VKP.vkGetPhysicalDeviceFormatProperties(VKP.gpu, formats[i].format, &fp);
        if (fp.optimalTilingFeatures & VKP_FORMAT_FEATURE_BLIT_DST) VKP.format = formats[i].format;
    }
    if (VKP.format < 0) { BR_INFO("Vulkan: no 8-bit window format to blit into"); return false; }
    memset(&ci, 0, sizeof(ci));
    for (i = 0; i < nf; ++i) if (formats[i].format == VKP.format) { ci.imageColorSpace = formats[i].colorSpace; break; }
    if (caps.currentExtent.width != 0xFFFFFFFFu) VKP.extent = caps.currentExtent;
    else {
        VKP.extent.width = (uint32_t)(win_w < 1 ? 1 : win_w);
        VKP.extent.height = (uint32_t)(win_h < 1 ? 1 : win_h);
        if (VKP.extent.width < caps.minImageExtent.width) VKP.extent.width = caps.minImageExtent.width;
        if (VKP.extent.height < caps.minImageExtent.height) VKP.extent.height = caps.minImageExtent.height;
        if (VKP.extent.width > caps.maxImageExtent.width) VKP.extent.width = caps.maxImageExtent.width;
        if (VKP.extent.height > caps.maxImageExtent.height) VKP.extent.height = caps.maxImageExtent.height;
    }
    VKP.want_w = win_w;
    VKP.want_h = win_h;
    if (VKP.extent.width == 0 || VKP.extent.height == 0) return false;   /* (minimised: try again later) */
    ci.sType = VKP_ST_SWAPCHAIN_CREATE_INFO;
    ci.surface = VKP.surface;
    ci.minImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount && ci.minImageCount > caps.maxImageCount) ci.minImageCount = caps.maxImageCount;
    if (ci.minImageCount > VKP_MAX_IMAGES) ci.minImageCount = VKP_MAX_IMAGES;
    ci.imageFormat = VKP.format;
    ci.imageExtent = VKP.extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VKP_IMAGE_USAGE_TRANSFER_DST;
    ci.imageSharingMode = 0;
    ci.preTransform = (caps.supportedTransforms & VKP_TRANSFORM_IDENTITY) ? VKP_TRANSFORM_IDENTITY : caps.currentTransform;
    ci.compositeAlpha = (caps.supportedCompositeAlpha & VKP_COMPOSITE_OPAQUE) ? VKP_COMPOSITE_OPAQUE :
                        (caps.supportedCompositeAlpha & VKP_COMPOSITE_INHERIT) ? VKP_COMPOSITE_INHERIT : caps.supportedCompositeAlpha & (~caps.supportedCompositeAlpha + 1);
    ci.presentMode = VKP_PRESENT_FIFO;                       /* (always there) */
    ci.clipped = 1;
    ci.oldSwapchain = old;
    if ((r = VKP.vkCreateSwapchainKHR(VKP.device, &ci, NULL, &VKP.swapchain)) != VKP_SUCCESS) {
        BR_INFO("Vulkan: no swapchain (error %d)", (int)r);
        VKP.swapchain = old;
        return false;
    }
    if (old) {                                               /* (the old one's images are done with: the device was idle) */
        VkSwapchainKHR made = VKP.swapchain;
        VKP.swapchain = old;
        vkp_drop_swapchain();
        VKP.swapchain = made;
    }
    VKP.image_count = VKP_MAX_IMAGES;
    if (VKP.vkGetSwapchainImagesKHR(VKP.device, VKP.swapchain, &VKP.image_count, VKP.images) < 0) return false;
    memset(&si, 0, sizeof(si));
    si.sType = VKP_ST_SEMAPHORE_CREATE_INFO;
    for (i = 0; i < VKP.image_count; ++i) if (VKP.vkCreateSemaphore(VKP.device, &si, NULL, &VKP.done[i]) != VKP_SUCCESS) return false;
    VKP.rebuild = false;
    return true;
}

/* ---- the frame's buffer and image ---------------------------------------------------------------- */

static void vkp_drop_frame(void) {
    if (VKP.staging_map) VKP.vkUnmapMemory(VKP.device, VKP.staging_memory);
    if (VKP.staging) VKP.vkDestroyBuffer(VKP.device, VKP.staging, NULL);
    if (VKP.staging_memory) VKP.vkFreeMemory(VKP.device, VKP.staging_memory, NULL);
    if (VKP.frame) VKP.vkDestroyImage(VKP.device, VKP.frame, NULL);
    if (VKP.frame_memory) VKP.vkFreeMemory(VKP.device, VKP.frame_memory, NULL);
    VKP.staging_map = NULL;
    VKP.staging = VKP.frame = 0;
    VKP.staging_memory = VKP.frame_memory = 0;
    VKP.frame_w = VKP.frame_h = 0;
}

static bool vkp_frame(int w, int h) {
    VkBufferCreateInfo bi;
    VkImageCreateInfo ii;
    VkMemoryRequirements req;
    VkMemoryAllocateInfo ai;
    int type;
    vkp_drop_frame();
    memset(&bi, 0, sizeof(bi));
    bi.sType = VKP_ST_BUFFER_CREATE_INFO;
    bi.size = (VkDeviceSize)w * (VkDeviceSize)h * 4u;
    bi.usage = VKP_BUFFER_USAGE_TRANSFER_SRC;
    if (VKP.vkCreateBuffer(VKP.device, &bi, NULL, &VKP.staging) != VKP_SUCCESS) return false;
    VKP.vkGetBufferMemoryRequirements(VKP.device, VKP.staging, &req);
    type = vkp_memory_type(req.memoryTypeBits, VKP_MEMORY_HOST_VISIBLE | VKP_MEMORY_HOST_COHERENT, VKP_MEMORY_HOST_VISIBLE | VKP_MEMORY_HOST_COHERENT);
    if (type < 0) return false;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VKP_ST_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = (uint32_t)type;
    if (VKP.vkAllocateMemory(VKP.device, &ai, NULL, &VKP.staging_memory) != VKP_SUCCESS) return false;
    if (VKP.vkBindBufferMemory(VKP.device, VKP.staging, VKP.staging_memory, 0) != VKP_SUCCESS) return false;
    if (VKP.vkMapMemory(VKP.device, VKP.staging_memory, 0, bi.size, 0, &VKP.staging_map) != VKP_SUCCESS) { VKP.staging_map = NULL; return false; }
    VKP.staging_size = bi.size;
    memset(&ii, 0, sizeof(ii));
    ii.sType = VKP_ST_IMAGE_CREATE_INFO;
    ii.imageType = 1;                                         /* 2D */
    ii.format = VKP_FORMAT_R8G8B8A8_UNORM;
    ii.extent.width = (uint32_t)w;
    ii.extent.height = (uint32_t)h;
    ii.extent.depth = 1;
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = 1;
    ii.tiling = 0;                                            /* optimal */
    ii.usage = VKP_IMAGE_USAGE_TRANSFER_SRC | VKP_IMAGE_USAGE_TRANSFER_DST;
    ii.initialLayout = VKP_LAYOUT_UNDEFINED;
    if (VKP.vkCreateImage(VKP.device, &ii, NULL, &VKP.frame) != VKP_SUCCESS) return false;
    VKP.vkGetImageMemoryRequirements(VKP.device, VKP.frame, &req);
    type = vkp_memory_type(req.memoryTypeBits, VKP_MEMORY_DEVICE_LOCAL, 0);
    if (type < 0) return false;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = (uint32_t)type;
    if (VKP.vkAllocateMemory(VKP.device, &ai, NULL, &VKP.frame_memory) != VKP_SUCCESS) return false;
    if (VKP.vkBindImageMemory(VKP.device, VKP.frame, VKP.frame_memory, 0) != VKP_SUCCESS) return false;
    VKP.frame_w = w;
    VKP.frame_h = h;
    return true;
}

/* ---- start, present, stop -------------------------------------------------------------------------- */

static void vkp_stop(void) {
    if (VKP.device) {
        VKP.vkDeviceWaitIdle(VKP.device);
        vkp_drop_frame();
        vkp_drop_swapchain();
        if (VKP.acquired) VKP.vkDestroySemaphore(VKP.device, VKP.acquired, NULL);
        if (VKP.fence) VKP.vkDestroyFence(VKP.device, VKP.fence, NULL);
        if (VKP.pool) VKP.vkDestroyCommandPool(VKP.device, VKP.pool, NULL);
        VKP.vkDestroyDevice(VKP.device, NULL);
    }
    if (VKP.instance) {
        if (VKP.surface && VKP.vkDestroySurfaceKHR) VKP.vkDestroySurfaceKHR(VKP.instance, VKP.surface, NULL);
        if (VKP.vkDestroyInstance) VKP.vkDestroyInstance(VKP.instance, NULL);
    }
    {
        void *lib = VKP.lib;                                  /* (the loader stays: unloading it isn't worth the risk) */
        memset(&VKP, 0, sizeof(VKP));
        VKP.lib = lib;
    }
}

/* After the surface is made: the device, the swapchain and what each frame needs. */
static bool vkp_finish_start(int win_w, int win_h) {
    char info[200];
    VkCommandPoolCreateInfo pi;
    VkCommandBufferAllocateInfo ai;
    VkSemaphoreCreateInfo si;
    VkFenceCreateInfo fi;
    if (!vkp_device(info, sizeof(info))) return false;
    if (!vkp_swapchain(win_w, win_h) && !VKP.swapchain) return false;
    memset(&pi, 0, sizeof(pi));
    pi.sType = VKP_ST_COMMAND_POOL_CREATE_INFO;
    pi.flags = 0x2;                                           /* its buffers can be reset one by one */
    pi.queueFamilyIndex = VKP.family;
    if (VKP.vkCreateCommandPool(VKP.device, &pi, NULL, &VKP.pool) != VKP_SUCCESS) return false;
    memset(&ai, 0, sizeof(ai));
    ai.sType = VKP_ST_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = VKP.pool;
    ai.commandBufferCount = 1;
    if (VKP.vkAllocateCommandBuffers(VKP.device, &ai, &VKP.cmd) != VKP_SUCCESS) return false;
    memset(&si, 0, sizeof(si));
    si.sType = VKP_ST_SEMAPHORE_CREATE_INFO;
    if (VKP.vkCreateSemaphore(VKP.device, &si, NULL, &VKP.acquired) != VKP_SUCCESS) return false;
    memset(&fi, 0, sizeof(fi));
    fi.sType = VKP_ST_FENCE_CREATE_INFO;
    fi.flags = 0x1;                                           /* signalled: the first frame doesn't wait */
    if (VKP.vkCreateFence(VKP.device, &fi, NULL, &VKP.fence) != VKP_SUCCESS) return false;
    br_gpu_set_active(BR_GPU_VULKAN, info);
    return true;
}

#if defined(_WIN32)
static bool vkp_start_win32(HINSTANCE instance, HWND hwnd, int win_w, int win_h) {
    VkWin32SurfaceCreateInfoKHR ci;
    vkCreateWin32SurfaceKHR_fn create;
    if (!vkp_instance("VK_KHR_win32_surface")) { vkp_stop(); return false; }
    { void *p = vkp_instance_proc("vkCreateWin32SurfaceKHR"); memcpy(&create, &p, sizeof(p)); }
    memset(&ci, 0, sizeof(ci));
    ci.sType = VKP_ST_WIN32_SURFACE_CREATE_INFO;
    ci.hinstance = instance;
    ci.hwnd = hwnd;
    if (!create || create(VKP.instance, &ci, NULL, &VKP.surface) != VKP_SUCCESS) { BR_INFO("Vulkan: no surface for the window"); vkp_stop(); return false; }
    if (!vkp_finish_start(win_w, win_h)) { vkp_stop(); return false; }
    return true;
}
#else
static bool vkp_start_xlib(Display *dpy, Window win, int win_w, int win_h) {
    VkXlibSurfaceCreateInfoKHR ci;
    vkCreateXlibSurfaceKHR_fn create;
    if (!vkp_instance("VK_KHR_xlib_surface")) { vkp_stop(); return false; }
    { void *p = vkp_instance_proc("vkCreateXlibSurfaceKHR"); memcpy(&create, &p, sizeof(p)); }
    memset(&ci, 0, sizeof(ci));
    ci.sType = VKP_ST_XLIB_SURFACE_CREATE_INFO;
    ci.dpy = dpy;
    ci.window = win;
    if (!create || create(VKP.instance, &ci, NULL, &VKP.surface) != VKP_SUCCESS) { BR_INFO("Vulkan: no surface for the window"); vkp_stop(); return false; }
    if (!vkp_finish_start(win_w, win_h)) { vkp_stop(); return false; }
    return true;
}
#endif

static void vkp_barrier(VkImage image, int32_t from, int32_t to, VkFlags src_access, VkFlags dst_access, VkFlags src_stage, VkFlags dst_stage) {
    VkImageMemoryBarrier b;
    memset(&b, 0, sizeof(b));
    b.sType = VKP_ST_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = src_access;
    b.dstAccessMask = dst_access;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VKP_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = VKP_ASPECT_COLOR;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    VKP.vkCmdPipelineBarrier(VKP.cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &b);
}

/* False only when Vulkan has stopped working (the caller falls back to software). */
static bool vkp_present(const br_image *fb, br_recti vp, int win_w, int win_h) {
    uint32_t index = 0;
    VkCommandBufferBeginInfo bi;
    VkBufferImageCopy copy;
    VkImageBlit blit;
    VkClearColorValue black;
    VkImageSubresourceRange all;
    VkSubmitInfo si;
    VkPresentInfoKHR pi;
    VkFlags wait_stage = VKP_STAGE_TRANSFER;
    VkResult r;
    int32_t x0, y0, x1, y1;
    if (win_w <= 0 || win_h <= 0 || !fb || fb->width <= 0 || fb->height <= 0) return true;
    if (VKP.vkWaitForFences(VKP.device, 1, &VKP.fence, 1, ~0ull) != VKP_SUCCESS) return false;
    if (VKP.rebuild || !VKP.swapchain || VKP.want_w != win_w || VKP.want_h != win_h) {
        VKP.vkDeviceWaitIdle(VKP.device);
        if (!vkp_swapchain(win_w, win_h)) return VKP.swapchain != 0;   /* (none yet: minimised, say) */
    }
    if (fb->width != VKP.frame_w || fb->height != VKP.frame_h) {
        VKP.vkDeviceWaitIdle(VKP.device);
        if (!vkp_frame(fb->width, fb->height)) return false;
    }
    r = VKP.vkAcquireNextImageKHR(VKP.device, VKP.swapchain, ~0ull, VKP.acquired, 0, &index);
    if (r == VKP_ERROR_OUT_OF_DATE) { VKP.rebuild = true; return true; }
    if (r != VKP_SUCCESS && r != VKP_SUBOPTIMAL) return false;
    if (index >= VKP.image_count) return false;
    memcpy(VKP.staging_map, fb->pixels, (size_t)fb->width * (size_t)fb->height * 4u);
    VKP.vkResetFences(VKP.device, 1, &VKP.fence);
    VKP.vkResetCommandBuffer(VKP.cmd, 0);
    memset(&bi, 0, sizeof(bi));
    bi.sType = VKP_ST_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = 0x1;                                           /* one time */
    VKP.vkBeginCommandBuffer(VKP.cmd, &bi);
    /* the pixels into the frame image */
    vkp_barrier(VKP.frame, VKP_LAYOUT_UNDEFINED, VKP_LAYOUT_TRANSFER_DST, 0, VKP_ACCESS_TRANSFER_WRITE, VKP_STAGE_TOP_OF_PIPE, VKP_STAGE_TRANSFER);
    memset(&copy, 0, sizeof(copy));
    copy.imageSubresource.aspectMask = VKP_ASPECT_COLOR;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = (uint32_t)fb->width;
    copy.imageExtent.height = (uint32_t)fb->height;
    copy.imageExtent.depth = 1;
    VKP.vkCmdCopyBufferToImage(VKP.cmd, VKP.staging, VKP.frame, VKP_LAYOUT_TRANSFER_DST, 1, &copy);
    vkp_barrier(VKP.frame, VKP_LAYOUT_TRANSFER_DST, VKP_LAYOUT_TRANSFER_SRC, VKP_ACCESS_TRANSFER_WRITE, VKP_ACCESS_TRANSFER_READ, VKP_STAGE_TRANSFER, VKP_STAGE_TRANSFER);
    /* the window: black, then the frame scaled into the viewport */
    vkp_barrier(VKP.images[index], VKP_LAYOUT_UNDEFINED, VKP_LAYOUT_TRANSFER_DST, 0, VKP_ACCESS_TRANSFER_WRITE, VKP_STAGE_TOP_OF_PIPE, VKP_STAGE_TRANSFER);
    memset(&black, 0, sizeof(black));
    black.float32[3] = 1.0f;
    memset(&all, 0, sizeof(all));
    all.aspectMask = VKP_ASPECT_COLOR;
    all.levelCount = 1;
    all.layerCount = 1;
    VKP.vkCmdClearColorImage(VKP.cmd, VKP.images[index], VKP_LAYOUT_TRANSFER_DST, &black, 1, &all);
    x0 = vp.x < 0 ? 0 : vp.x;
    y0 = vp.y < 0 ? 0 : vp.y;
    x1 = vp.x + vp.w > (int)VKP.extent.width ? (int)VKP.extent.width : vp.x + vp.w;
    y1 = vp.y + vp.h > (int)VKP.extent.height ? (int)VKP.extent.height : vp.y + vp.h;
    if (x1 > x0 && y1 > y0 && vp.w > 0 && vp.h > 0) {
        /* (the part of the frame that lands inside the window, when the viewport hangs over its edge) */
        float sx = (float)fb->width / (float)vp.w, sy = (float)fb->height / (float)vp.h;
        VkImageMemoryBarrier b;
        memset(&b, 0, sizeof(b));                            /* the clear before the blit */
        b.sType = VKP_ST_IMAGE_MEMORY_BARRIER;
        b.srcAccessMask = VKP_ACCESS_TRANSFER_WRITE;
        b.dstAccessMask = VKP_ACCESS_TRANSFER_WRITE;
        b.oldLayout = b.newLayout = VKP_LAYOUT_TRANSFER_DST;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VKP_QUEUE_FAMILY_IGNORED;
        b.image = VKP.images[index];
        b.subresourceRange = all;
        VKP.vkCmdPipelineBarrier(VKP.cmd, VKP_STAGE_TRANSFER, VKP_STAGE_TRANSFER, 0, 0, NULL, 0, NULL, 1, &b);
        memset(&blit, 0, sizeof(blit));
        blit.srcSubresource.aspectMask = blit.dstSubresource.aspectMask = VKP_ASPECT_COLOR;
        blit.srcSubresource.layerCount = blit.dstSubresource.layerCount = 1;
        blit.srcOffsets[0].x = (int32_t)((float)(x0 - vp.x) * sx);
        blit.srcOffsets[0].y = (int32_t)((float)(y0 - vp.y) * sy);
        blit.srcOffsets[1].x = (int32_t)((float)(x1 - vp.x) * sx + 0.5f);
        blit.srcOffsets[1].y = (int32_t)((float)(y1 - vp.y) * sy + 0.5f);
        if (blit.srcOffsets[1].x > fb->width) blit.srcOffsets[1].x = fb->width;
        if (blit.srcOffsets[1].y > fb->height) blit.srcOffsets[1].y = fb->height;
        blit.srcOffsets[1].z = 1;
        blit.dstOffsets[0].x = x0;
        blit.dstOffsets[0].y = y0;
        blit.dstOffsets[1].x = x1;
        blit.dstOffsets[1].y = y1;
        blit.dstOffsets[1].z = 1;
        VKP.vkCmdBlitImage(VKP.cmd, VKP.frame, VKP_LAYOUT_TRANSFER_SRC, VKP.images[index], VKP_LAYOUT_TRANSFER_DST, 1, &blit, VKP_FILTER_NEAREST);
    }
    vkp_barrier(VKP.images[index], VKP_LAYOUT_TRANSFER_DST, VKP_LAYOUT_PRESENT_SRC, VKP_ACCESS_TRANSFER_WRITE, VKP_ACCESS_MEMORY_READ, VKP_STAGE_TRANSFER, VKP_STAGE_BOTTOM_OF_PIPE);
    if (VKP.vkEndCommandBuffer(VKP.cmd) != VKP_SUCCESS) return false;
    memset(&si, 0, sizeof(si));
    si.sType = VKP_ST_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &VKP.acquired;
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &VKP.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &VKP.done[index];
    if (VKP.vkQueueSubmit(VKP.queue, 1, &si, VKP.fence) != VKP_SUCCESS) return false;
    memset(&pi, 0, sizeof(pi));
    pi.sType = VKP_ST_PRESENT_INFO;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &VKP.done[index];
    pi.swapchainCount = 1;
    pi.pSwapchains = &VKP.swapchain;
    pi.pImageIndices = &index;
    r = VKP.vkQueuePresentKHR(VKP.queue, &pi);
    if (r == VKP_ERROR_OUT_OF_DATE || r == VKP_SUBOPTIMAL) VKP.rebuild = true;
    else if (r != VKP_SUCCESS) return false;
    return true;
}
