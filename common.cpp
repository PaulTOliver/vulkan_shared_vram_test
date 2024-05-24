#include <iostream>
#include <unistd.h>
#include <vector>
#include <vulkan/vulkan.h>

// ----------------------------------------------------------------------------
// CONSTANTS
// ----------------------------------------------------------------------------

#define SHARED_BUFFER_SIZE 1024
#define SOCKET_PATH        "/tmp/vulkan_socket"

// ----------------------------------------------------------------------------
// VARIABLES
// ----------------------------------------------------------------------------

// Vulkan validation layers (these enable debug messages)
std::vector<const char *> validationLayers = {
    "VK_LAYER_KHRONOS_validation",
};

// Vulkan extensions. We will push to this vector during initialization.
std::vector<const char *> instanceExtensions = {
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
};

std::vector<const char *> deviceExtensions = {
    VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
    VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
};

// Variables
VkInstance               instance;
VkDebugUtilsMessengerEXT debugMessenger;
VkPhysicalDevice         physicalDevice;
VkDevice                 device;
VkQueue                  queue;
VkBuffer                 sharedBuffer;
VkDeviceMemory           sharedMemory;
int                      socketFD;
int                      connFD;
int                      sharedBufferFD;
std::string              sharedData;

// ----------------------------------------------------------------------------
// SHARED DECLARATIONS
// ----------------------------------------------------------------------------
void createSharedMemoryObjectsAndFDs();

// ----------------------------------------------------------------------------
// DEBUGGING
// ----------------------------------------------------------------------------
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void                                       *pUserData
) {
    std::cerr << "Validation layer: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

void fillDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo) {
    createInfo = {
        .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback,
    };
}

// ----------------------------------------------------------------------------
// MEMORY TYPE FINDER
// ----------------------------------------------------------------------------
uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            std::cout << "Found suitable memory type " << i << " with properties " << memProperties.memoryTypes[i].propertyFlags << std::endl;
            return i;
        }

    }

    throw std::runtime_error("Failed to find suitable memory type!");
}

// ----------------------------------------------------------------------------
// VULKAN INITIALIZATION
// ----------------------------------------------------------------------------
void createInstance() {
    std::cout << "Creating new Vulkan instance" << std::endl;

    // Fill up application info
    VkApplicationInfo appInfo = {
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName   = appName,
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName        = "No Engine",
        .engineVersion      = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion         = VK_API_VERSION_1_1,
    };

    // Fill up debug messenger info
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo;
    fillDebugMessengerCreateInfo(debugCreateInfo);

    // Fill up creation info
    VkInstanceCreateInfo createInfo = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext                   = (VkDebugUtilsMessengerCreateInfoEXT *)&debugCreateInfo,
        .pApplicationInfo        = &appInfo,
        .enabledLayerCount       = static_cast<uint32_t>(validationLayers.size()),
        .ppEnabledLayerNames     = validationLayers.data(),
        .enabledExtensionCount   = static_cast<uint32_t>(instanceExtensions.size()),
        .ppEnabledExtensionNames = instanceExtensions.data(),
    };

    if  (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create instance!");
    }
}

void setupDebugMessenger() {
    std::cout << "Setting up debug messenger" << std::endl;

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo;
    fillDebugMessengerCreateInfo(debugCreateInfo);

    if (((PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"))(instance, &debugCreateInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
        throw std::runtime_error("Failed to set up debug messenger!");
    }
}

void pickPhysicalDevice() {
    std::cout << "Selecting a physical device" << std::endl;

    uint32_t deviceCount;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    physicalDevice = VK_NULL_HANDLE;

    for (const auto &device : devices) {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);

        // Enforce selection of NVidia card.
        if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            std::cout << "Selected device: " << deviceProperties.deviceName << std::endl;
            physicalDevice = device;
            break;
        }
    }

    if (physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to find a suitable GPU!");
    }
}

void createLogicalDeviceAndQueue() {
    std::cout << "Creating a logical device" << std::endl;

    float queuePriority = 1.f;

    // We don't care about queue capabilities just yet. We need to change this to a valid queue
    // family configuration later on, when we do actual rendering.
    VkDeviceQueueCreateInfo queueCreateInfo = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0,
        .queueCount       = 1,
        .pQueuePriorities = &queuePriority,
    };

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &queueCreateInfo,
        .enabledLayerCount       = static_cast<uint32_t>(validationLayers.size()),
        .ppEnabledLayerNames     = validationLayers.data(),
        .enabledExtensionCount   = static_cast<uint32_t>(deviceExtensions.size()),
        .ppEnabledExtensionNames = deviceExtensions.data(),
        .pEnabledFeatures        = &deviceFeatures,
    };

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create logical device!");
    }

    vkGetDeviceQueue(device, 0, 0, &queue);
}

void initVulkan() {
    std::cout << "Initializing Vulkan" << std::endl;

    createInstance();
    setupDebugMessenger();
    pickPhysicalDevice();
    createLogicalDeviceAndQueue();
    createSharedMemoryObjectsAndFDs();
}

// ----------------------------------------------------------------------------
// CLEANUP
// ----------------------------------------------------------------------------
void cleanup() {
    std::cout << "Running cleanup" << std::endl;

    vkFreeMemory(device, sharedMemory, nullptr);
    vkDestroyBuffer(device, sharedBuffer, nullptr);
    vkDestroyDevice(device, nullptr);
    ((PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"))(instance, debugMessenger, nullptr);
    vkDestroyInstance(instance, nullptr);
}
