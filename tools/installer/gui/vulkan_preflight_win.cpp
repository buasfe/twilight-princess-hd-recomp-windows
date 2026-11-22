#include "vulkan_preflight_win.h"

#include <windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#include "gfx/vulkan/device_requirements.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

VulkanPreflight check_vulkan() {
    std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(&FreeLibrary)> loader(
        LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32), FreeLibrary);
    if (!loader)
        return {false, "The Windows Vulkan loader is missing. Install the graphics driver and check again."};

    const auto proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader.get(), "vkGetInstanceProcAddr"));
    if (!proc)
        return {false, "The Vulkan loader does not export vkGetInstanceProcAddr."};
    const auto version = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(proc(nullptr, "vkEnumerateInstanceVersion"));
    uint32_t api = 0;
    if (!version || version(&api) != VK_SUCCESS || api < VK_API_VERSION_1_1)
        return {false, "The Vulkan loader needs Vulkan 1.1 or newer. Update the graphics driver and check again."};
    const auto create = reinterpret_cast<PFN_vkCreateInstance>(proc(nullptr, "vkCreateInstance"));
    if (!create)
        return {false, "The Vulkan loader does not export vkCreateInstance."};
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Twilight Princess HD setup";
    app.apiVersion = std::min(api, VK_API_VERSION_1_3);
    const char* extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = 2;
    ci.ppEnabledExtensionNames = extensions;
    VkInstance instance = nullptr;
    const VkResult result = create(&ci, nullptr, &instance);
    if (result == VK_ERROR_EXTENSION_NOT_PRESENT)
        return {false, "The Vulkan driver needs VK_KHR_surface and VK_KHR_win32_surface."};
    if (result != VK_SUCCESS)
        return {false, "Vulkan initialization failed with code " + std::to_string(result) + ". Check the graphics driver."};
    const auto destroy = [proc](VkInstance handle) {
        auto fn = reinterpret_cast<PFN_vkDestroyInstance>(proc(handle, "vkDestroyInstance"));
        if (fn) fn(handle, nullptr);
    };
    std::unique_ptr<std::remove_pointer_t<VkInstance>, decltype(destroy)> owner(instance, destroy);
    const auto devices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(proc(instance, "vkEnumeratePhysicalDevices"));
    const auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(proc(instance, "vkGetPhysicalDeviceProperties"));
    const auto features = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(proc(instance, "vkGetPhysicalDeviceFeatures2"));
    const auto device_extensions = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(proc(instance, "vkEnumerateDeviceExtensionProperties"));
    const auto queues = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(proc(instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    const auto present = reinterpret_cast<PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR>(
        proc(instance, "vkGetPhysicalDeviceWin32PresentationSupportKHR"));
    if (!devices || !properties || !features || !device_extensions || !queues || !present)
        return {false, "The Vulkan driver is missing device queries or Win32 presentation support."};

    uint32_t count = 0;
    if (devices(instance, &count, nullptr) != VK_SUCCESS || !count)
        return {false, "No Vulkan graphics devices found. Check the installed graphics drivers."};
    std::vector<VkPhysicalDevice> physical_devices(count);
    if (devices(instance, &count, physical_devices.data()) != VK_SUCCESS)
        return {false, "The Vulkan driver could not list the available graphics devices. Check again."};
    std::string details, preferred;
    int best_rank = -1;
    for (uint32_t i = 0; i < count; ++i) {
        const VkPhysicalDevice device = physical_devices[i];
        VkPhysicalDeviceProperties prop{};
        properties(device, &prop);
        details += std::string(prop.deviceName) + " (Vulkan " +
            std::to_string(VK_API_VERSION_MAJOR(prop.apiVersion)) + "." +
            std::to_string(VK_API_VERSION_MINOR(prop.apiVersion)) + "): ";

        uint32_t n = 0;
        if (device_extensions(device, nullptr, &n, nullptr) != VK_SUCCESS) {
            details += "could not query device extensions.\n";
            continue;
        }
        std::vector<VkExtensionProperties> exts(n);
        if (device_extensions(device, nullptr, &n, exts.data()) != VK_SUCCESS) {
            details += "could not list device extensions.\n";
            continue;
        }
        const auto support = gfxvk::check_device(device, prop.apiVersion, [&](const char* name) {
            return std::any_of(exts.begin(), exts.begin() + n, [&](const auto& ext) {
                return std::strcmp(ext.extensionName, name) == 0;
            });
        }, features);
        if (support.missing) {
            details += std::string("missing ") + support.missing + ".\n";
            continue;
        }
        queues(device, &n, nullptr);
        std::vector<VkQueueFamilyProperties> families(n);
        queues(device, &n, families.data());
        bool can_draw = false;
        for (uint32_t q = 0; q < n; ++q) {
            if ((families[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present(device, q)) {
                can_draw = true;
                break;
            }
        }
        if (!can_draw) {
            details += "no graphics queue with Win32 presentation.\n";
            continue;
        }
        details += support.rendering == gfxvk::DynamicRendering::Core
            ? "compatible (core dynamic rendering).\n" : "compatible (VK_KHR_dynamic_rendering).\n";
        const int rank = gfxvk::device_rank(support.rendering, prop.deviceType);
        if (rank > best_rank) {
            best_rank = rank;
            preferred = prop.deviceName;
        }
    }
    if (preferred.empty())
        return {false, details + "Update the GPU driver or select a compatible dedicated GPU in Windows Graphics settings."};
    return {true, details + "Preferred GPU: " + preferred + "."};
}
