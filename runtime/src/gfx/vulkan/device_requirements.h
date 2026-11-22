#pragma once
#include <initializer_list>
#include <vulkan/vulkan.h>

namespace gfxvk {
enum class DynamicRendering { None, Core, KHR };

struct DeviceSupport {
  DynamicRendering rendering;
  const char *missing = nullptr;
};

// Both setup and the runtime use the same minimum driver requirements. The
// runtime additionally checks that its actual surfaces can use the queue.
template <typename HasExtension>
DeviceSupport check_device(VkPhysicalDevice device, uint32_t api,
                           HasExtension has_extension,
                           PFN_vkGetPhysicalDeviceFeatures2 get_features) {
  if (api < VK_API_VERSION_1_1)
    return {DynamicRendering::None, "Vulkan 1.1 or newer"};
  if (!has_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
    return {DynamicRendering::None, VK_KHR_SWAPCHAIN_EXTENSION_NAME};

  VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  if (api >= VK_API_VERSION_1_3) {
    VkPhysicalDeviceVulkan13Features core{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features.pNext = &core;
    get_features(device, &features);
    if (core.dynamicRendering)
      return {DynamicRendering::Core};
  }
  if (!has_extension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) {
    return {DynamicRendering::None, api >= VK_API_VERSION_1_3 ? "dynamicRendering feature"
                                                            : VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME};
  }
  if (api < VK_API_VERSION_1_2) {
    for (const char *name : {VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME,
                              VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME}) {
      if (!has_extension(name))
        return {DynamicRendering::None, name};
    }
  }
  VkPhysicalDeviceDynamicRenderingFeaturesKHR ext{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
  features.pNext = &ext;
  get_features(device, &features);
  if (!ext.dynamicRendering)
    return {DynamicRendering::None, "dynamicRendering feature"};
  return {DynamicRendering::KHR};
}

inline int device_rank(DynamicRendering rendering, VkPhysicalDeviceType type) {
  const int core = rendering == DynamicRendering::Core ? 1 : 0;
#ifdef _WIN32
  if (type == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
    return 2 + core;
#else
  (void)type;
#endif
  return core;
}
} // namespace gfxvk
