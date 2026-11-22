#pragma once
#include <string>

struct VulkanPreflight {
    bool compatible;
    std::string details;
};

VulkanPreflight check_vulkan();
