
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <iostream>
#include <vector>
#include <string_view>
#include <optional>
#include <set>

#include <cstdint> // Necessary for uint32_t
#include <limits> // Necessary for std::numeric_limits
#include <algorithm> // Necessary for std::clamp

// debug callbacks
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
  VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
  VkDebugUtilsMessageTypeFlagsEXT messageType,
  const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
  void* pUserData) {

  std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

  return VK_FALSE;
}

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
  auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
  if (func != nullptr) {
    return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
  }
  else {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
  auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
  if (func != nullptr) {
    func(instance, debugMessenger, pAllocator);
  }
}

void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
  createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  createInfo.pfnUserCallback = DebugCallback;
}

class HelloTriangleApplication {
public:
  void run() {
    init_window();
    init_vulkan();
    main_loop();
    clean_up();
  }

private:
  SDL_Window* window{ nullptr };
  uint32_t width{ 1280 }, height{ 720 };
  bool is_running{ true };
  bool is_initialized{ false };
  uint32_t extension_count{ 0 }, layer_count{ 0 }, device_count{ 0 };
  // do not use std::string_view: not ensure end '\0' format str
  std::vector<const char*> extension_names{};
  std::vector<const char*> validation_layers = {
    "VK_LAYER_KHRONOS_validation"
  };
  std::vector<const char*> device_extensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
  };

#ifdef NDEBUG
  bool enable_validation_layers{ false };
#else
  bool enable_validation_layers{ true };
#endif

  VkInstance instance{ 0 };
  VkDebugUtilsMessengerEXT debug_messenger{ 0 };
  VkPhysicalDevice physical_device{ VK_NULL_HANDLE };
  VkDevice device;
  VkQueue graphics_queue;
  VkQueue present_queue;
  VkSurfaceKHR surface;
  VkSwapchainKHR swapchain;
  std::vector<VkImage> swapchain_images;
  VkFormat swapchain_image_format;
  VkExtent2D swapchain_extent;
  std::vector<VkImageView> swapchain_image_views; // using an image as a texture

  bool check_device_extension_support(VkPhysicalDevice device) {
    uint32_t extension_count;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);

    std::vector<VkExtensionProperties> available_extensions(extension_count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, available_extensions.data());

    std::set<std::string> required_extensions(device_extensions.begin(), device_extensions.end());
    for (const auto& extension : available_extensions) {
      required_extensions.erase(extension.extensionName);
    }
    return required_extensions.empty();
  }

  bool check_validation_layer_support() {
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    std::cout << "available validation layers:\n";

    for (const auto& layer_props : available_layers) {
      std::cout << '\t' << layer_props.layerName << '\n';
    }

    for (const auto& layer_name : validation_layers) {
      bool is_support = false;

      for (const auto& layer_props : available_layers) {
        if (strcmp(layer_name, layer_props.layerName) == 0) {
          is_support = true;
          break;
        }
      }

      if (!is_support) {
        return false;
      }
    }
    layer_count = validation_layers.size();
    return true;
  }

  void init_SDL2_extensions() {
    SDL_Vulkan_GetInstanceExtensions(window, &extension_count, nullptr);
    if (extension_count) {
      std::vector<const char*> ext_bufs(extension_count);
      SDL_Vulkan_GetInstanceExtensions(window, &extension_count, ext_bufs.data());
      printf("require %u vulkan instance extensions\n", extension_count);
      for (auto i = 0; i < extension_count; ++i) {
        std::cout << i << ": " << ext_bufs[i] << "\n";
        extension_names.push_back(ext_bufs[i]);
      }
      if (enable_validation_layers) {
        // extension_names.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
        extension_names.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
      }
    }
  }

  bool check_extensions_support() {
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
    std::vector<VkExtensionProperties> extensions(extension_count);
    vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions.data());

    std::cout << "available extensions:\n";

    for (const auto& extension : extensions) {
      std::cout << '\t' << extension.extensionName << '\n';
    }

    init_SDL2_extensions();

    for (const auto& require_extension : extension_names) {
      bool is_support{ false };
      for (const auto& extension : extensions) {
        if (strcmp(extension.extensionName, require_extension) == 0) {
          is_support = true;
          break;
        }
      }
      if (!is_support) {
        return false;
      }
    }
    extension_count = extension_names.size();
    return true;
  }


  void init_window() {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    auto window_flags =
      SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN;
    window =
      SDL_CreateWindow("SDL_Vulkan_DEMO", 0, 0, width, height, window_flags);

    if (window) {
    }
    else {
      throw std::runtime_error(SDL_GetError());
    }
  }

  void setup_debug_messenger() {
    if (!enable_validation_layers) {
      return;
    }
    VkDebugUtilsMessengerCreateInfoEXT create_info{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = DebugCallback,
      .pUserData = nullptr
    };

    if (CreateDebugUtilsMessengerEXT(instance, &create_info, nullptr, &debug_messenger) != VK_SUCCESS) {
      throw std::runtime_error("failed to setup debug messenger!");
    }
  }


  struct QueueFamilyIndices {
    std::optional<uint32_t> graphics_family;
    std::optional<uint32_t> present_family;
    bool is_complete() {
      return graphics_family.has_value() && present_family.has_value();
    }
  };

  QueueFamilyIndices find_queue_families(VkPhysicalDevice device) {
    uint32_t queue_family_count = 0;
    QueueFamilyIndices result;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);

    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families.data());

    for (uint32_t i = 0; i < queue_family_count; ++i) {
      if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        result.graphics_family = i;
      }

      VkBool32 present_support = false;
      vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface, &present_support);
      if (present_support) {
        result.present_family = i;
      }

      if (result.is_complete()) {
        break;
      }
    }
    return result;
  }

  bool is_suitable_device(VkPhysicalDevice device) {
    QueueFamilyIndices indices = find_queue_families(device);
    bool extensions_supported = check_device_extension_support(device);
    bool swapchain_adequate = false;

    if (extensions_supported) {
      auto swapchain_support = query_swapchain_support(device);
      swapchain_adequate = !swapchain_support.formats.empty() && !swapchain_support.present_modes.empty();
    }

    return indices.is_complete() && extensions_supported && swapchain_adequate;
  }

  void pick_physical_device() {
    vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    if (device_count == 0) {
      throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance, &device_count, devices.data());
    physical_device = devices[0]; // select gpu:0 default

    if (physical_device == VK_NULL_HANDLE) {
      throw std::runtime_error("failed to find a suitable GPU!");
    }
  }

  void create_logical_device() {
    QueueFamilyIndices indices = find_queue_families(physical_device);

    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    std::set<uint32_t> queue_families_set = {
      indices.graphics_family.value(), indices.present_family.value()
    };

    float queue_priority = 1.0f;
    for (auto queue_family : queue_families_set) {
      VkDeviceQueueCreateInfo queue_create_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueFamilyIndex = queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority
      };
      queue_create_infos.push_back(queue_create_info);
    }

    VkPhysicalDeviceFeatures device_features{};
    VkDeviceCreateInfo create_info{
       .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
       .queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size()),
       .pQueueCreateInfos = queue_create_infos.data(), // array here
       .pEnabledFeatures = &device_features
    };

    // enable devices' extensions
    create_info.enabledExtensionCount = static_cast<uint32_t>(device_extensions.size());
    create_info.ppEnabledExtensionNames = device_extensions.data();

    if (enable_validation_layers) {
      create_info.enabledLayerCount = layer_count;
      create_info.ppEnabledLayerNames = validation_layers.data();
    }
    else {
      create_info.enabledLayerCount = 0;
    }

    if (vkCreateDevice(physical_device, &create_info, nullptr, &device) != VK_SUCCESS) {
      throw std::runtime_error("failed to create logical device!");
    }

    // retrieving queue handles, zero means first queue(element 0)
    vkGetDeviceQueue(device, indices.graphics_family.value(), 0, &graphics_queue);
    vkGetDeviceQueue(device, indices.present_family.value(), 0, &present_queue);
  }

  // platform related part
  void create_surface() {
    if (!SDL_Vulkan_CreateSurface(window, instance, &surface)) {
      throw std::runtime_error("failed to create Vulkan compatible surface using SDL\n");
    }
  }

  // query swap_chain support
  struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;
  };

  SwapChainSupportDetails query_swapchain_support(VkPhysicalDevice device) {
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, nullptr);
    if (format_count != 0) {
      details.formats.resize(format_count);
      vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, details.formats.data());
    }

    uint32_t present_mode_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, nullptr);
    if (present_mode_count != 0) {
      details.present_modes.resize(present_mode_count);
      vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, details.present_modes.data());
    }
    return details;
  }

  VkSurfaceFormatKHR choose_swap_surface_format(const std::vector<VkSurfaceFormatKHR>& available_formats) {
    for (auto const& format : available_formats) {
      if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        return format;
      } // encoding&color space format
    }
    return available_formats.front();
  }

  VkPresentModeKHR choose_swap_present_mode(const std::vector<VkPresentModeKHR>& availabla_present_modes) {
    for (auto const& mode : availabla_present_modes) {
      if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
        return mode;
      }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
  }

  VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
      return capabilities.currentExtent;
    }
    else {
      int width, height;
      SDL_Vulkan_GetDrawableSize(window, &width, &height);

      VkExtent2D actual_extent{
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
      };

      actual_extent.width = std::clamp(actual_extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
      actual_extent.height = std::clamp(actual_extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
      return actual_extent;
    }
  }

  void create_swapchain() {
    SwapChainSupportDetails swapchain_support = query_swapchain_support(physical_device);

    VkSurfaceFormatKHR surface_format = choose_swap_surface_format(swapchain_support.formats);
    VkPresentModeKHR present_mode = choose_swap_present_mode(swapchain_support.present_modes);
    VkExtent2D extent = choose_swap_extent(swapchain_support.capabilities);

    // how many images in swap chain
    uint32_t image_count = swapchain_support.capabilities.minImageCount + 1;
    if (swapchain_support.capabilities.maxImageCount > 0 && image_count > swapchain_support.capabilities.maxImageCount) {
      image_count = swapchain_support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info{
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = surface,
      .minImageCount = image_count,
      .imageFormat = surface_format.format,
      .imageColorSpace = surface_format.colorSpace,
      .imageExtent = extent,
      .imageArrayLayers = 1, // set 1 unless stereoscopic 3D App
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, // directly present, post-process : VK_IMAGE_USAGE_TRANSFER_DST_BIT
      .presentMode = present_mode,
      .clipped = VK_TRUE, // clip screen (window) obscured
      .oldSwapchain = VK_NULL_HANDLE
    };

    auto indices = find_queue_families(physical_device);
    uint32_t queue_family_indices[] = {
      indices.graphics_family.value(), indices.present_family.value()
    };

    if (indices.graphics_family != indices.present_family) {
      create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
      create_info.queueFamilyIndexCount = 2;
      create_info.pQueueFamilyIndices = queue_family_indices;
    }
    else {
      create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
      create_info.queueFamilyIndexCount = 0;
      create_info.pQueueFamilyIndices = nullptr;
    }

    create_info.preTransform = swapchain_support.capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    if (vkCreateSwapchainKHR(device, &create_info, nullptr, &swapchain) != VK_SUCCESS) {
      throw std::runtime_error("failed to create swap chain!");
    }

    vkGetSwapchainImagesKHR(device, swapchain, &image_count, nullptr);
    if (image_count != 0) {
      swapchain_images.resize(image_count);
      vkGetSwapchainImagesKHR(device, swapchain, &image_count, swapchain_images.data());
    }

    swapchain_image_format = surface_format.format;
    swapchain_extent = extent;
  }

  void create_image_views() {
    swapchain_image_views.resize(swapchain_images.size());
    for (size_t i = 0; i < swapchain_images.size(); ++i) {
      VkImageViewCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = swapchain_images[i],
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = swapchain_image_format,
        .components {
          .r = VK_COMPONENT_SWIZZLE_IDENTITY,
          .g = VK_COMPONENT_SWIZZLE_IDENTITY,
          .b = VK_COMPONENT_SWIZZLE_IDENTITY,
          .a = VK_COMPONENT_SWIZZLE_IDENTITY,
        },
        .subresourceRange { // image purpose and mipmap level
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .baseMipLevel = 0,
          .levelCount = 1,
          .baseArrayLayer = 0,
          .layerCount = 1
        }
      };

      if (vkCreateImageView(device, &create_info, nullptr, &swapchain_image_views[i]) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image views!");
      }
    };
  }

  void init_vulkan() {
    create_instance();
    setup_debug_messenger();
    create_surface();
    pick_physical_device();
    create_logical_device();
    create_swapchain();
    create_image_views(); 
  }

  void create_instance() {

    VkApplicationInfo app_info{
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pNext = nullptr,
      .pApplicationName = "Hello Triangle",
      .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
      .pEngineName = "No Engine",
      .engineVersion = VK_MAKE_VERSION(1, 0, 0),
      .apiVersion = VK_API_VERSION_1_0
    };

    if (enable_validation_layers && !check_validation_layer_support()) {
      throw std::runtime_error("validation layers requested, but not available!");
    }

    // for SDL2
    if (!check_extensions_support()) {
      throw std::runtime_error("some extensions requested, but not available!");
    }

    VkInstanceCreateInfo create_info{
       .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
       .pApplicationInfo = &app_info,
       .enabledExtensionCount = extension_count,
       .ppEnabledExtensionNames = (const char**)extension_names.data()
    };

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enable_validation_layers) {
      create_info.enabledLayerCount = layer_count;
      create_info.ppEnabledLayerNames = validation_layers.data();

      PopulateDebugMessengerCreateInfo(debugCreateInfo);
      create_info.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
    }
    else {
      create_info.enabledLayerCount = 0;
      create_info.pNext = nullptr;
    }

    if (vkCreateInstance(&create_info, nullptr, &instance) != VK_SUCCESS) {
      throw std::runtime_error("failed to create instance!");
    }

    // instance initialized success!
    is_initialized = true;
  }

  void main_loop() {
    SDL_Event e;
    while (is_running) {
      while (SDL_PollEvent(&e) != 0) {
        // do rendering loop
        if (e.type == SDL_QUIT)
          is_running = false;
      }
    }
  }

  void clean_up() {
    if (is_initialized) {
      for (auto image_view : swapchain_image_views) {
        vkDestroyImageView(device, image_view, nullptr);
      }

      vkDestroySwapchainKHR(device, swapchain, nullptr);
      vkDestroyDevice(device, nullptr);

      if (enable_validation_layers) {
        DestroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
      }
      vkDestroySurfaceKHR(instance, surface, nullptr);
      vkDestroyInstance(instance, nullptr);
      SDL_DestroyWindow(window);
      SDL_Quit();
    }
  }
};




int main(int argc, char** argv) {

  HelloTriangleApplication app;
  try {
    app.run();
  }
  catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
