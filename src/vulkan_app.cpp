//
// Created by undersilence on 2022/6/22.
//
#include "vulkan_app.h"
#include "eigen_helper.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "../external/stb_image.h"

#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan_core.h>

#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <vector>

#include <algorithm> // Necessary for std::clamp
#include <cstdint>   // Necessary for uint32_t
#include <limits>    // Necessary for std::numeric_limits

#include <chrono>

void VulkanApplication::run() {
  init_window();
  init_vulkan();
  main_loop();
  cleanup();
}

static VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDebugUtilsMessengerEXT *pDebugMessenger) {
  auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      instance, "vkCreateDebugUtilsMessengerEXT");
  if (func != nullptr) {
    return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
  } else {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
}

static void
DestroyDebugUtilsMessengerEXT(VkInstance instance,
                              VkDebugUtilsMessengerEXT debugMessenger,
                              const VkAllocationCallbacks *pAllocator) {
  auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
      instance, "vkDestroyDebugUtilsMessengerEXT");
  if (func != nullptr) {
    func(instance, debugMessenger, pAllocator);
  }
}

// debug callbacks
static VKAPI_ATTR VkBool32 VKAPI_CALL
DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              VkDebugUtilsMessageTypeFlagsEXT messageType,
              const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
              void *pUserData) {

  std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

  return VK_FALSE;
}

static void PopulateDebugMessengerCreateInfo(
    VkDebugUtilsMessengerCreateInfoEXT &createInfo) {
  createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  createInfo.pfnUserCallback = DebugCallback;
}

// shader code read helper
static std::vector<char> read_file(std::string const &filename) {
  std::ifstream file(filename, std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    throw std::runtime_error("failed to open file! " + filename);
  }
  auto file_size = static_cast<size_t>(file.tellg());
  std::vector<char> buffer(file_size);

  file.seekg(0);
  file.read(buffer.data(), file_size);
  file.close();
  return buffer;
}

bool VulkanApplication::check_device_extension_support(
    VkPhysicalDevice device) {
  uint32_t extension_count;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count,
                                       nullptr);

  std::vector<VkExtensionProperties> available_extensions(extension_count);
  vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count,
                                       available_extensions.data());

  std::set<std::string> required_extensions(device_extensions.begin(),
                                            device_extensions.end());
  for (const auto &extension : available_extensions) {
    required_extensions.erase(extension.extensionName);
  }
  return required_extensions.empty();
}

bool VulkanApplication::check_validation_layer_support() {
  vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
  std::vector<VkLayerProperties> available_layers(layer_count);
  vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

  std::cout << "available validation layers:\n";

  for (const auto &layer_props : available_layers) {
    std::cout << '\t' << layer_props.layerName << '\n';
  }

  for (const auto &layer_name : validation_layers) {
    bool is_support = false;

    for (const auto &layer_props : available_layers) {
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

void VulkanApplication::init_SDL2_extensions() {
  SDL_Vulkan_GetInstanceExtensions(window, &extension_count, nullptr);
  if (extension_count) {
    std::vector<const char *> ext_bufs(extension_count);
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

bool VulkanApplication::check_extensions_support() {
  vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
  std::vector<VkExtensionProperties> extensions(extension_count);
  vkEnumerateInstanceExtensionProperties(nullptr, &extension_count,
                                         extensions.data());

  std::cout << "available extensions:\n";

  for (const auto &extension : extensions) {
    std::cout << '\t' << extension.extensionName << '\n';
  }

  init_SDL2_extensions();

  for (const auto &require_extension : extension_names) {
    bool is_support{false};
    for (const auto &extension : extensions) {
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

void VulkanApplication::init_window() {
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  auto window_flags =
      SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN;
  window =
      SDL_CreateWindow("SDL_Vulkan_DEMO", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, width, height, window_flags);
  SDL_SetWindowData(window, "App", this);

  if (!window) {
    throw std::runtime_error(SDL_GetError());
  }
}

void VulkanApplication::setup_debug_messenger() {
  if (!enable_validation_layers) {
    return;
  }
  VkDebugUtilsMessengerCreateInfoEXT create_info{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = DebugCallback,
      .pUserData = nullptr};

  if (CreateDebugUtilsMessengerEXT(instance, &create_info, nullptr,
                                   &debug_messenger) != VK_SUCCESS) {
    throw std::runtime_error("failed to setup debug messenger!");
  }
}

QueueFamilyIndices
VulkanApplication::find_queue_families(VkPhysicalDevice physical_device) {
  uint32_t queue_family_count = 0;
  QueueFamilyIndices result;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count,
                                           nullptr);

  std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count,
                                           queue_families.data());

  for (uint32_t i = 0; i < queue_family_count; ++i) {
    if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      result.graphics_family = i;
      // continue;
    }

    if (queue_families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
      result.transfer_family = i;
    }

    VkBool32 present_support = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, i, surface,
                                         &present_support);
    if (present_support) {
      result.present_family = i;
    }

    if (result.is_complete()) {
      break;
    }
  }
  return result;
}

bool VulkanApplication::is_suitable_device(VkPhysicalDevice device) {
  QueueFamilyIndices indices = find_queue_families(device);
  bool extensions_supported = check_device_extension_support(device);
  bool swapchain_adequate = false;

  VkPhysicalDeviceFeatures features{};
  vkGetPhysicalDeviceFeatures(physical_device, &features);

  if (extensions_supported) {
    auto swapchain_support = query_swapchain_support(device);
    swapchain_adequate = !swapchain_support.formats.empty() &&
                         !swapchain_support.present_modes.empty();
  }

  return indices.is_complete() && extensions_supported && swapchain_adequate &&
         features.samplerAnisotropy;
}

void VulkanApplication::pick_physical_device() {
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

void VulkanApplication::create_logical_device() {
  QueueFamilyIndices indices = find_queue_families(physical_device);

  std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
  // merge same queue_family indices
  std::set<uint32_t> queue_families_set = {indices.graphics_family.value(),
                                           indices.transfer_family.value(),
                                           indices.present_family.value()};

  //  std::cerr << "unique queue family size " << queue_families_set.size()
  //            << std::endl;

  float queue_priority = 1.0f;
  for (auto queue_family : queue_families_set) {
    VkDeviceQueueCreateInfo queue_create_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueFamilyIndex = queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority};
    queue_create_infos.push_back(queue_create_info);
  }

  VkPhysicalDeviceFeatures device_features{.samplerAnisotropy = VK_TRUE};
  VkDeviceCreateInfo create_info{
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size()),
      .pQueueCreateInfos = queue_create_infos.data(), // array here
      // enable devices' extensions
      .enabledExtensionCount = (uint32_t)device_extensions.size(),
      .ppEnabledExtensionNames = device_extensions.data(),
      .pEnabledFeatures = &device_features};

  if (enable_validation_layers) {
    create_info.enabledLayerCount = layer_count;
    create_info.ppEnabledLayerNames = validation_layers.data();
  } else {
    create_info.enabledLayerCount = 0;
  }

  if (vkCreateDevice(physical_device, &create_info, nullptr, &device) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create logical device!");
  }

  // retrieving queue handles, zero means first queue(element 0)
  vkGetDeviceQueue(device, indices.graphics_family.value(), 0, &graphics_queue);
  vkGetDeviceQueue(device, indices.transfer_family.value(), 0, &transfer_queue);
  vkGetDeviceQueue(device, indices.present_family.value(), 0, &present_queue);
}

// platform related part
void VulkanApplication::create_surface() {
  if (!SDL_Vulkan_CreateSurface(window, instance, &surface)) {
    throw std::runtime_error(
        "failed to create Vulkan compatible surface using SDL\n");
  }
}

SwapChainSupportDetails
VulkanApplication::query_swapchain_support(VkPhysicalDevice device) {
  SwapChainSupportDetails details;

  // query surface extent (width, height) from surface
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface,
                                            &details.capabilities);

  uint32_t format_count;
  vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, nullptr);
  if (format_count != 0) {
    details.formats.resize(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count,
                                         details.formats.data());
  }

  uint32_t present_mode_count;
  vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface,
                                            &present_mode_count, nullptr);
  if (present_mode_count != 0) {
    details.present_modes.resize(present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        device, surface, &present_mode_count, details.present_modes.data());
  }
  return details;
}

VkSurfaceFormatKHR VulkanApplication::choose_swap_surface_format(
    const std::vector<VkSurfaceFormatKHR> &available_formats) {
  for (auto const &format : available_formats) {
    if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
        format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return format;
    } // encoding&color space format
  }
  return available_formats.front();
}

VkPresentModeKHR VulkanApplication::choose_swap_present_mode(
    const std::vector<VkPresentModeKHR> &available_present_modes) {
  for (auto const &mode : available_present_modes) {
    if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
      return mode;
    }
  }
  // return VK_PRESENT_MODE_IMMEDIATE_KHR;
  std::cerr << "fallback to FIFO mode!\n";
  return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanApplication::choose_swap_extent(
    const VkSurfaceCapabilitiesKHR &capabilities) {
  if (capabilities.currentExtent.width !=
      std::numeric_limits<uint32_t>::max()) {
    return capabilities.currentExtent;
  } else {
    SDL_Vulkan_GetDrawableSize(window, &width, &height);

    VkExtent2D actual_extent{static_cast<uint32_t>(width),
                             static_cast<uint32_t>(height)};

    actual_extent.width =
        std::clamp(actual_extent.width, capabilities.minImageExtent.width,
                   capabilities.maxImageExtent.width);
    actual_extent.height =
        std::clamp(actual_extent.height, capabilities.minImageExtent.height,
                   capabilities.maxImageExtent.height);

    return actual_extent;
  }
}

void VulkanApplication::create_swapchain() {
  SwapChainSupportDetails swapchain_support =
      query_swapchain_support(physical_device);

  VkSurfaceFormatKHR surface_format =
      choose_swap_surface_format(swapchain_support.formats);
  VkPresentModeKHR present_mode =
      choose_swap_present_mode(swapchain_support.present_modes);
  VkExtent2D extent = choose_swap_extent(swapchain_support.capabilities);

  while (extent.width == 0 || extent.height == 0) {
    return;
  }

  // how many images in swap chain
  uint32_t image_count = swapchain_support.capabilities.minImageCount + 1;
  if (swapchain_support.capabilities.maxImageCount > 0 &&
      image_count > swapchain_support.capabilities.maxImageCount) {
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
      .imageUsage =
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, // directly present, post-process
                                               // :
                                               // VK_IMAGE_USAGE_TRANSFER_DST_BIT
      .preTransform = swapchain_support.capabilities.currentTransform,
      .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode = present_mode,
      .clipped = VK_TRUE // clip screen (window) obscured
  };

  auto indices = find_queue_families(physical_device);
  uint32_t queue_family_indices[] = {indices.graphics_family.value(),
                                     indices.transfer_family.value(),
                                     indices.present_family.value()};

  if (indices.graphics_family != indices.present_family) {
    create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    create_info.queueFamilyIndexCount = 2;
    create_info.pQueueFamilyIndices = queue_family_indices;
  } else {
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    //    create_info.queueFamilyIndexCount = 0;
    //    create_info.pQueueFamilyIndices = nullptr;
  }

  if (vkCreateSwapchainKHR(device, &create_info, nullptr, &swapchain) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create swap chain!");
  }

  vkGetSwapchainImagesKHR(device, swapchain, &image_count, nullptr);
  if (image_count != 0) {
    swapchain_images.resize(image_count);
    vkGetSwapchainImagesKHR(device, swapchain, &image_count,
                            swapchain_images.data());
  }

  swapchain_image_format = surface_format.format;
  swapchain_extent = extent;
}

void VulkanApplication::create_image_views() {
  swapchain_image_views.resize(swapchain_images.size());
  for (size_t i = 0; i < swapchain_images.size(); ++i) {
    swapchain_image_views[i] =
        create_image_view(swapchain_images[i], swapchain_image_format);
  };
}

void VulkanApplication::create_render_pass() {
  VkAttachmentDescription color_attachment{
      .format = swapchain_image_format,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, // what to do before rendering
      .storeOp =
          VK_ATTACHMENT_STORE_OP_STORE, // after rendering, stored in mem.
      .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, // not use
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,    // pixel format
      .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR // directly present using
                                                     // the swapchain
  }; // define single color buffer attachment (one of the images from swapchain)

  // Single render pass can consist of multiple subpasses, every subpass ref to
  // one or more attachments
  VkAttachmentReference color_attachment_ref{
      .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

  VkSubpassDescription subpass{.pipelineBindPoint =
                                   VK_PIPELINE_BIND_POINT_GRAPHICS,
                               .colorAttachmentCount = 1,
                               .pColorAttachments = &color_attachment_ref};

  VkSubpassDependency dependency{
      .srcSubpass = VK_SUBPASS_EXTERNAL,
      .dstSubpass = 0,
      .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .srcAccessMask = 0,
      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
  };

  VkRenderPassCreateInfo render_pass_info{
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &color_attachment,
      .subpassCount = 1,
      .pSubpasses = &subpass,
      .dependencyCount = 1,
      .pDependencies = &dependency};

  if (vkCreateRenderPass(device, &render_pass_info, nullptr, &render_pass) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create render pass!");
  }
}

VkShaderModule
VulkanApplication::create_shader_module(std::vector<char> const &code) {
  VkShaderModuleCreateInfo create_info{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = code.size(),
      .pCode = reinterpret_cast<const uint32_t *>(
          code.data()) // need reinterpret (convert raw data)
  };
  VkShaderModule shader_module;
  if (vkCreateShaderModule(device, &create_info, nullptr, &shader_module) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create shader module!");
  }
  return shader_module;
}

void VulkanApplication::create_graphics_pipeline() {
  auto vert_shader_code = read_file("shaders/vert.spv");
  auto frag_shader_code = read_file("shaders/frag.spv");

  auto vert_shader_module = create_shader_module(vert_shader_code);
  auto frag_shader_module = create_shader_module(frag_shader_code);

  VkPipelineShaderStageCreateInfo vert_shader_stage_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = vert_shader_module,
      .pName = "main" // entry point
      // .pSpecializationInfo = nullptr // define shader constants
  };

  VkPipelineShaderStageCreateInfo frag_shader_stage_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = frag_shader_module,
      .pName = "main" // entry point
      // .pSpecializationInfo = nullptr // define shader constants
  };

  VkPipelineShaderStageCreateInfo shader_stages[] = {vert_shader_stage_info,
                                                     frag_shader_stage_info};

  // FIXED function states for Pipeline
  // 1. Vertex Input: describes the format of the vertex data that passed to the
  // vertex shader describes in two ways: a. bindings: data spacing and
  // per-vertex or per-instance level b. attribute descriptions: type, how to
  // load and offset (layout)

  auto binding_description = Vertex::get_binding_description();
  auto attribute_descriptions = Vertex::get_attribute_descriptions();

  VkPipelineVertexInputStateCreateInfo vertex_input_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &binding_description, // details
      .vertexAttributeDescriptionCount =
          (uint32_t)attribute_descriptions.size(),
      .pVertexAttributeDescriptions = attribute_descriptions.data()};

  // 2. Input Assembly: assemble geometry to be drawn from vertices, and
  // primitive restart on/off
  VkPipelineInputAssemblyStateCreateInfo input_assembly{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .primitiveRestartEnable =
          VK_FALSE // set special index 0xffff to break up primitives
  };

  // 3. Viewports and Scissors: defines framebuffer region
  VkViewport viewport{.x = 0.0f,
                      .y = 0.0f,
                      .width = (float)swapchain_extent.width,
                      .height = (float)swapchain_extent.height,
                      .minDepth = 0.0f,
                      .maxDepth = 1.0};

  VkRect2D scissor{.offset{0, 0}, .extent = swapchain_extent};

  VkPipelineViewportStateCreateInfo viewport_state{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1, // multiple viewports need GPU support
      .pViewports = &viewport,
      .scissorCount = 1,
      .pScissors = &scissor};

  // 4. Rasterizer: break primitives into pixels (fragments) to be colored by
  // fragment shader
  //                also performing depth testing, face culling, scissor test
  //                and set draw mode (fill, wireframe, point)
  VkPipelineRasterizationStateCreateInfo rasterizer{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable = VK_FALSE, // VK_TRUE if need clamp depth into [near_z,
                                    // far_z] instead of discard violates
      .rasterizerDiscardEnable = VK_FALSE,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_BACK_BIT,            // culling back face
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, // specifies the vertex
                                                    // order for front faces
      .depthBiasEnable = VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp = 0.0f,
      .depthBiasSlopeFactor = 0.0f,
      .lineWidth = 1.0f,
  };

  // 5. Multisampling: anti-aliasing support (need GPU support)
  VkPipelineMultisampleStateCreateInfo multisampling{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
      .sampleShadingEnable = VK_FALSE,
      .minSampleShading = 1.0f,
      .pSampleMask = nullptr,
      .alphaToCoverageEnable = VK_FALSE,
      .alphaToOneEnable = VK_FALSE};

  // 6. Depth and Stencil testing
  // VkPipelineDepthStencilStateCreateInfo depth_stencil_state;

  // 7. Color blending: Configure how to combine with the color already in the
  // framebuffer,
  //                    two schemes: a. Mix the old and new color,
  //                                 b. Combine the old and new value using a
  //                                 bitwise op

  // VkPipelineColorBlendStateCreateInfo: global color blending settings
  // VkPipelineColorBlendAttachmentState: per attached framebuffer settings
  VkPipelineColorBlendAttachmentState color_blend_attachment{
      .blendEnable = VK_TRUE,
      .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA, // alpha blending
      .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
      .colorBlendOp = VK_BLEND_OP_ADD,
      .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
      .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
      .alphaBlendOp = VK_BLEND_OP_ADD,
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
  };

  VkPipelineColorBlendStateCreateInfo color_bending{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable = VK_FALSE,
      .logicOp = VK_LOGIC_OP_COPY,
      .attachmentCount = 1,
      .pAttachments = &color_blend_attachment,
      .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f}};

  // 8. Dynamic State: set dynamic state in pipeline (change without recreating
  // the pipeline)
  std::vector<VkDynamicState> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT,
                                                VK_DYNAMIC_STATE_LINE_WIDTH};
  VkPipelineDynamicStateCreateInfo dynamic_state_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
      .pDynamicStates = dynamic_states.data()};

  // 9. Pipeline Layout:  dynamic state variables settings (uniform)
  VkPipelineLayoutCreateInfo pipeline_layout_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &descriptor_set_layout,
      .pushConstantRangeCount = 0,
      .pPushConstantRanges = nullptr};

  if (vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr,
                             &pipeline_layout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }

  VkGraphicsPipelineCreateInfo pipeline_info{
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = 2,
      .pStages = shader_stages,
      .pVertexInputState = &vertex_input_info,
      .pInputAssemblyState = &input_assembly,
      .pViewportState = &viewport_state,
      .pRasterizationState = &rasterizer,
      .pMultisampleState = &multisampling,
      .pDepthStencilState = nullptr,
      .pColorBlendState = &color_bending,
      // .pDynamicState = &dynamic_state_info,
      .pDynamicState = nullptr,
      .layout = pipeline_layout,
      .renderPass = render_pass,
      .subpass = 0,
      .basePipelineHandle = VK_NULL_HANDLE, // for pipeline deriving
      .basePipelineIndex = -1};

  // can create multiple pipelines once
  // VkPipelineCache used to store and reuse data to pipeline creation across
  // multiple calls (speedups)
  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info,
                                nullptr, &graphics_pipeline) != VK_SUCCESS) {
    throw std::runtime_error("failed to create graphics pipeline!");
  }

  vkDestroyShaderModule(device, frag_shader_module, nullptr);
  vkDestroyShaderModule(device, vert_shader_module, nullptr);
}

void VulkanApplication::create_framebuffers() {
  swapchain_framebuffers.resize(swapchain_image_views.size());
  for (size_t i = 0; i < swapchain_image_views.size(); ++i) {
    VkImageView attachments[] = {swapchain_image_views[i]};

    VkFramebufferCreateInfo framebuffer_info{
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = render_pass,
        .attachmentCount = 1,
        .pAttachments = attachments,
        .width = swapchain_extent.width,
        .height = swapchain_extent.height,
        .layers = 1};

    if (vkCreateFramebuffer(device, &framebuffer_info, nullptr,
                            &swapchain_framebuffers[i]) != VK_SUCCESS) {
      throw std::runtime_error("failed to create framebuffer!");
    }
  }
}

void VulkanApplication::create_command_pool() {
  QueueFamilyIndices queue_family_indices =
      find_queue_families(physical_device);
  VkCommandPoolCreateInfo pool_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = queue_family_indices.graphics_family.value()
      // each cmd pool can alloc cmd buffers that submit to a single type of
      // queue
  };

  if (vkCreateCommandPool(device, &pool_info, nullptr, &command_pool) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create command pool!");
  }

  VkCommandPoolCreateInfo transfer_pool_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = queue_family_indices.transfer_family.value()};

  if (vkCreateCommandPool(device, &transfer_pool_info, nullptr,
                          &transfer_command_pool) != VK_SUCCESS) {
    throw std::runtime_error("failed to create transfer command pool!");
  }
}

void VulkanApplication::create_command_buffer() {
  command_buffers.resize(MAX_FRAMES_IN_FLIGHT);
  VkCommandBufferAllocateInfo alloc_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = command_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, // for reuse common op
      .commandBufferCount = (uint32_t)command_buffers.size()};

  if (vkAllocateCommandBuffers(device, &alloc_info, command_buffers.data()) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to allocate command buffers!");
  }
}

void VulkanApplication::record_command_buffer(VkCommandBuffer command_buffer,
                                              uint32_t image_index) {
  VkCommandBufferBeginInfo begin_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = 0,
      .pInheritanceInfo = nullptr};

  if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
    throw std::runtime_error("failed to begin recording command buffer!");
  }

  VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 1.0f}}};

  VkRenderPassBeginInfo render_pass_info{
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = render_pass,
      .framebuffer =
          swapchain_framebuffers[image_index], // picking right framebuffer for
                                               // the current swapchain image
      .renderArea{.offset = {0, 0}, .extent = swapchain_extent},
      .clearValueCount = 1,
      .pClearValues = &clear_color};

  vkCmdBeginRenderPass(command_buffer, &render_pass_info,
                       VK_SUBPASS_CONTENTS_INLINE);

  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    graphics_pipeline);
  VkBuffer vertex_buffers[] = {vertex_buffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);
  vkCmdBindIndexBuffer(command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT16);

  vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline_layout, 0, 1,
                          &descriptor_sets[current_frame], 0, nullptr);
  vkCmdDrawIndexed(command_buffer, (uint32_t)indices.size(), 1, 0, 0, 0);
  // vkCmdDraw(command_buffer, (uint32_t)vertices.size(), 1, 0, 0);

  vkCmdEndRenderPass(command_buffer);
  if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
    throw std::runtime_error("failed to record command buffer!");
  }
}

void VulkanApplication::create_sync_objects() {
  image_available_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
  render_finished_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
  in_flight_fences.resize(MAX_FRAMES_IN_FLIGHT);

  VkSemaphoreCreateInfo semaphore_info{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkFenceCreateInfo fence_info{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                               .flags = VK_FENCE_CREATE_SIGNALED_BIT};

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    if (vkCreateSemaphore(device, &semaphore_info, nullptr,
                          &image_available_semaphores[i]) != VK_SUCCESS ||
        vkCreateSemaphore(device, &semaphore_info, nullptr,
                          &render_finished_semaphores[i]) != VK_SUCCESS ||
        vkCreateFence(device, &fence_info, nullptr, &in_flight_fences[i]) !=
            VK_SUCCESS) {
      throw std::runtime_error("failed to create semaphores or fence!");
    }
  }
}

void VulkanApplication::init_vulkan() {
  create_instance();
  setup_debug_messenger();
  create_surface();
  pick_physical_device();
  create_logical_device();
  create_swapchain();
  create_image_views();
  create_render_pass();
  create_descriptor_set_layout(); // set memory layout first
  create_graphics_pipeline();
  create_framebuffers();
  create_command_pool();
  create_texture_image();
  create_texture_image_view();
  create_texture_sampler();
  create_vertex_buffer();
  create_index_buffer();
  create_uniform_buffers();
  create_descriptor_pool();
  create_descriptor_sets();
  create_command_buffer();
  create_sync_objects();
}

void VulkanApplication::cleanup_swapchain() {
  for (auto framebuffer : swapchain_framebuffers) {
    vkDestroyFramebuffer(device, framebuffer, nullptr);
  }
  vkDestroyPipeline(device, graphics_pipeline, nullptr);
  vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
  vkDestroyRenderPass(device, render_pass, nullptr);
  for (auto image_view : swapchain_image_views) {
    vkDestroyImageView(device, image_view, nullptr);
  }
  vkDestroySwapchainKHR(device, swapchain, nullptr);
}

void VulkanApplication::recreate_swapchain() {
  /*SDL_Vulkan_GetDrawableSize(window, &width, &height);
  while (width == 0 || height == 0) {
    SDL_Vulkan_GetDrawableSize(window, &width, &height);
    SDL_WaitEvent(nullptr);
  }*/

  vkDeviceWaitIdle(device);
  create_swapchain();
  create_image_views();
  create_render_pass();
  create_graphics_pipeline();
  create_framebuffers();
}

void VulkanApplication::create_instance() {

  VkApplicationInfo app_info{.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                             .pNext = nullptr,
                             .pApplicationName = "Hello Triangle",
                             .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
                             .pEngineName = "No Engine",
                             .engineVersion = VK_MAKE_VERSION(1, 0, 0),
                             .apiVersion = VK_API_VERSION_1_0};

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
      .ppEnabledExtensionNames = (const char **)extension_names.data()};

  VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
  if (enable_validation_layers) {
    create_info.enabledLayerCount = layer_count;
    create_info.ppEnabledLayerNames = validation_layers.data();

    PopulateDebugMessengerCreateInfo(debugCreateInfo);
    create_info.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debugCreateInfo;
  } else {
    create_info.enabledLayerCount = 0;
    create_info.pNext = nullptr;
  }

  if (vkCreateInstance(&create_info, nullptr, &instance) != VK_SUCCESS) {
    throw std::runtime_error("failed to create instance!");
  }

  // instance initialized success!
  is_initialized = true;
}

void VulkanApplication::main_loop() {
  SDL_Event e;
  uint32_t duration = 0, start_time = 0, total_frames = 0;
  uint32_t milliseconds_per_frame = 16;

  while (is_running) {
    while (SDL_PollEvent(&e) != 0) {

      // start_time = SDL_GetTicks();
      // do rendering loop
      if (e.type == SDL_QUIT)
        is_running = false;
      if (e.type == SDL_WINDOWEVENT) {
        if (e.window.event == SDL_WINDOWEVENT_RESIZED) {
          // auto app =
          // reinterpret_cast<VulkanApplication*>(SDL_GetWindowData(window,
          // "App")); app->framebuffer_resized = true;
          framebuffer_resized = true; // NO NEED FOR SDL2 surface
        }
      }

      //      uint32_t duration = SDL_GetTicks() - start_time;
      //      if (duration > milliseconds_per_frame) {
    }

    start_time = SDL_GetTicks();

    draw_frame();

    duration += SDL_GetTicks() - start_time;
    float fps = ++total_frames / (float)(duration == 0 ? 1 : duration) * 1000;
    std::string title = "SDL_Vulkan_DEMO fps:" + std::to_string(fps);
    SDL_SetWindowTitle(window, title.c_str());
  }
  vkDeviceWaitIdle(device);
}

void VulkanApplication::draw_frame() {
  // frame steps outline
  // 1. wait for the prev frame to finish
  // 2. acquire an image from the swap chain
  // 3. record a cmd buffer which draws the scene onto that image
  // 4. submit the recorded cmd buffer
  // 5. present the swap chain image

  // Semaphores is used to add order between queue operations
  // Fence: use it if the host needs to know when the GPU has finished something
  // (e.g. screenshot)
  vkWaitForFences(device, 1, &in_flight_fences[current_frame], VK_TRUE,
                  UINT64_MAX);

  uint32_t image_index;
  auto result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                      image_available_semaphores[current_frame],
                                      VK_NULL_HANDLE, &image_index);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    framebuffer_resized = false;
    recreate_swapchain();
    return;
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    throw std::runtime_error("failed to acquire swap chain image!");
  }
  vkResetFences(device, 1,
                &in_flight_fences[current_frame]); // clear immediately

  update_uniform_buffer(current_frame);

  vkResetCommandBuffer(command_buffers[current_frame], 0);
  record_command_buffer(command_buffers[current_frame], image_index);

  VkSemaphore wait_semaphores[] = {
      image_available_semaphores[current_frame]}; // GPU waits for these
                                                  // semaphores
  VkSemaphore signal_semaphores[] = {
      render_finished_semaphores[current_frame]}; // GPU sets these semaphores
  VkPipelineStageFlags wait_stages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

  VkSubmitInfo submit_info{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                           .waitSemaphoreCount = 1,
                           .pWaitSemaphores = wait_semaphores,
                           .pWaitDstStageMask = wait_stages,
                           .commandBufferCount = 1,
                           .pCommandBuffers = &command_buffers[current_frame],
                           .signalSemaphoreCount = 1,
                           .pSignalSemaphores = signal_semaphores};

  // signal in_flight_fence when cmd buffer exec finished
  if (vkQueueSubmit(graphics_queue, 1, &submit_info,
                    in_flight_fences[current_frame]) != VK_SUCCESS) {
    throw std::runtime_error("failed to submit draw command buffer!");
  }
  // Presentation to screen

  VkSwapchainKHR swapchains[] = {swapchain};
  VkPresentInfoKHR present_info{
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = signal_semaphores, // wait GPU render finished
      .swapchainCount = 1,
      .pSwapchains = swapchains,
      .pImageIndices = &image_index,
      .pResults = nullptr};

  // send image to the swapchain
  result = vkQueuePresentKHR(present_queue, &present_info);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
      framebuffer_resized) {
    std::cout << "framebuffer need resized!\n";
    framebuffer_resized = false;
    recreate_swapchain();
  } else if (result != VK_SUCCESS) {
    throw std::runtime_error("failed to present swap chain image!");
  }

  current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanApplication::cleanup() {
  if (is_initialized) {
    cleanup_swapchain();

    vkDestroySampler(device, texture_sampler, nullptr);
    vkDestroyImageView(device, texture_image_view, nullptr);
    vkDestroyImage(device, texture_image, nullptr);
    vkFreeMemory(device, texture_image_memory, nullptr);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
      vkDestroyBuffer(device, uniform_buffers[i], nullptr);
      vkFreeMemory(device, uniform_buffers_memory[i], nullptr);
    }
    vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
    vkDestroyBuffer(device, index_buffer, nullptr);
    vkFreeMemory(device, index_buffer_memory, nullptr);
    vkDestroyBuffer(device, vertex_buffer, nullptr);
    vkFreeMemory(device, vertex_buffer_memory, nullptr);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
      vkDestroySemaphore(device, image_available_semaphores[i], nullptr);
      vkDestroySemaphore(device, render_finished_semaphores[i], nullptr);
      vkDestroyFence(device, in_flight_fences[i], nullptr);
    }

    vkDestroyCommandPool(device, command_pool, nullptr);
    vkDestroyCommandPool(device, transfer_command_pool, nullptr);
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

uint32_t VulkanApplication::find_memory_type(uint32_t type_filter,
                                             VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties mem_properties;
  vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);
  for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
    if (((type_filter >> i) & 1) &&
        (mem_properties.memoryTypes[i].propertyFlags & properties) ==
            properties) {
      return i;
    }
  }
  throw std::runtime_error("failed to find suitable memory type!");
}

void VulkanApplication::create_buffer(VkDeviceSize size,
                                      VkBufferUsageFlags usage,
                                      VkMemoryPropertyFlags properties,
                                      VkBuffer &buffer,
                                      VkDeviceMemory &buffer_memory) {
  auto queue_indices = find_queue_families(physical_device);
  std::vector<uint32_t> indices;
  if (queue_indices.graphics_family.value() ==
      queue_indices.transfer_family.value()) {
    indices.push_back(queue_indices.graphics_family.value());
  } else {
    indices.push_back(queue_indices.graphics_family.value());
    indices.push_back(queue_indices.transfer_family.value());
  }

  VkBufferCreateInfo buffer_info{
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = size,
      .usage = usage,
      .sharingMode = (uint32_t)indices.size() == 1 ? VK_SHARING_MODE_EXCLUSIVE
                                                   : VK_SHARING_MODE_CONCURRENT,
      // create buffer which queues included
      .queueFamilyIndexCount = (uint32_t)indices.size(),
      .pQueueFamilyIndices = indices.data()};

  if (vkCreateBuffer(device, &buffer_info, nullptr, &buffer) != VK_SUCCESS) {
    throw std::runtime_error("failed to create vertex buffer!");
  }

  VkMemoryRequirements mem_requirements;
  vkGetBufferMemoryRequirements(device, buffer, &mem_requirements);

  // GPU memory alloc
  VkMemoryAllocateInfo alloc_info{
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mem_requirements.size,
      // enable write from CPU && CPU/GPU memory strictly coherent
      .memoryTypeIndex =
          find_memory_type(mem_requirements.memoryTypeBits, properties)};

  if (vkAllocateMemory(device, &alloc_info, nullptr, &buffer_memory) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to allocate vertex buffer on GPU memory!");
  }

  // bind CPU & GPU memories
  vkBindBufferMemory(device, buffer, buffer_memory, 0);
}

void VulkanApplication::copy_buffer(VkBuffer src_buffer, VkBuffer dst_buffer,
                                    VkDeviceSize size) {

  auto cmd_buf = begin_single_commands(transfer_command_pool);

  VkBufferCopy copy_region{.srcOffset = 0, .dstOffset = 0, .size = size};
  vkCmdCopyBuffer(cmd_buf, src_buffer, dst_buffer, 1, &copy_region);

  end_single_commands(cmd_buf, transfer_command_pool);
}

void VulkanApplication::create_vertex_buffer() {

  VkDeviceSize buffer_size = sizeof(vertices[0]) * vertices.size();
  VkBuffer staging_buffer;
  VkDeviceMemory staging_buffer_memory;

  create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                staging_buffer, staging_buffer_memory);
  // copy buffer memory from cpu to gpu
  void *data;
  vkMapMemory(device, staging_buffer_memory, 0, buffer_size, 0, &data);
  memcpy(data, vertices.data(), (size_t)buffer_size);
  vkUnmapMemory(device, staging_buffer_memory);

  create_buffer(
      buffer_size,
      // final vertex buffer: transfer_dst & vertex_buffer
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertex_buffer, vertex_buffer_memory);
  copy_buffer(staging_buffer, vertex_buffer, buffer_size);

  // destroy buffers explicitly
  vkDestroyBuffer(device, staging_buffer, nullptr);
  vkFreeMemory(device, staging_buffer_memory, nullptr);
}
void VulkanApplication::create_index_buffer() {

  VkDeviceSize buffer_size = sizeof(indices[0]) * indices.size();
  VkBuffer staging_buffer;
  VkDeviceMemory staging_buffer_memory;

  create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                staging_buffer, staging_buffer_memory);
  // copy buffer memory from cpu to gpu
  void *data;
  vkMapMemory(device, staging_buffer_memory, 0, buffer_size, 0, &data);
  memcpy(data, indices.data(), (size_t)buffer_size);
  vkUnmapMemory(device, staging_buffer_memory);

  create_buffer(
      buffer_size,
      // final vertex buffer: transfer_dst & index_buffer
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, index_buffer, index_buffer_memory);
  copy_buffer(staging_buffer, index_buffer, buffer_size);

  // destroy buffers explicitly
  vkDestroyBuffer(device, staging_buffer, nullptr);
  vkFreeMemory(device, staging_buffer_memory, nullptr);
}
void VulkanApplication::create_descriptor_set_layout() {
  VkDescriptorSetLayoutBinding ubo_layout_binding{
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      .descriptorCount = 1, // could be uniform array
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
      .pImmutableSamplers = nullptr // relevant for sampling related descriptor
  };

  VkDescriptorSetLayoutBinding sampler_layout_binding{
      .binding = 1, // binding position
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
      .pImmutableSamplers = nullptr};

  std::array<VkDescriptorSetLayoutBinding, 2> bindings = {
      ubo_layout_binding, sampler_layout_binding};

  VkDescriptorSetLayoutCreateInfo layout_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = (uint32_t)bindings.size(),
      .pBindings = bindings.data()};

  if (vkCreateDescriptorSetLayout(device, &layout_info, nullptr,
                                  &descriptor_set_layout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor set layout!");
  }
}
void VulkanApplication::create_uniform_buffers() {
  VkDeviceSize buffer_size = sizeof(UniformBufferObject);

  uniform_buffers.resize(MAX_FRAMES_IN_FLIGHT);
  uniform_buffers_memory.resize(MAX_FRAMES_IN_FLIGHT);

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    create_buffer(buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  uniform_buffers[i], uniform_buffers_memory[i]);
  }
}
void VulkanApplication::update_uniform_buffer(uint32_t current_image) {
  static auto start_time = std::chrono::high_resolution_clock::now();
  auto current_time = std::chrono::high_resolution_clock::now();
  float time = std::chrono::duration<float, std::chrono::seconds::period>(
                   current_time - start_time)
                   .count();

  using namespace Eigen;
  using namespace EigenHelper;

  Eigen::Matrix4f model = EigenHelper::rotate(time * 90.0f / 180.0f * 3.1415926,
                                              Eigen::Vector3f::UnitZ());
  Eigen::Matrix4f view = EigenHelper::lookAt(Eigen::Vector3f(2.0f, 2.0f, 2.0f),
                                             Eigen::Vector3f(0.0f, 0.0f, 0.0f),
                                             Eigen::Vector3f(0.0f, 0.0f, 1.0f));
  Eigen::Matrix4f project = EigenHelper::perspective(
      45.0f / 180.0f * 3.1415926,
      swapchain_extent.width / (float)swapchain_extent.height, 0.1, 10.f);

  UniformBufferObject ubo{.model = model, .view = view, .project = project};
  ubo.project(1, 1) *= -1;

  void *data;
  vkMapMemory(device, uniform_buffers_memory[current_image], 0, sizeof(ubo), 0,
              &data);
  memcpy(data, &ubo, sizeof(ubo));
  vkUnmapMemory(device, uniform_buffers_memory[current_image]);
}

void VulkanApplication::create_descriptor_pool() {
  std::array<VkDescriptorPoolSize, 2> pool_sizes{
      VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                           .descriptorCount = (uint32_t)MAX_FRAMES_IN_FLIGHT},
      VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           .descriptorCount = (uint32_t)MAX_FRAMES_IN_FLIGHT}};

  VkDescriptorPoolCreateInfo pool_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = (uint32_t)MAX_FRAMES_IN_FLIGHT, // descriptor set max size
      .poolSizeCount = 2,
      .pPoolSizes = pool_sizes.data(),
  };

  if (vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor pool!");
  }
}

void VulkanApplication::create_descriptor_sets() {
  std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT,
                                             descriptor_set_layout);
  VkDescriptorSetAllocateInfo alloc_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = descriptor_pool,
      .descriptorSetCount = (uint32_t)MAX_FRAMES_IN_FLIGHT,
      .pSetLayouts = layouts.data()};

  descriptor_sets.resize(MAX_FRAMES_IN_FLIGHT);
  if (vkAllocateDescriptorSets(device, &alloc_info, descriptor_sets.data()) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to allocate descriptor sets!");
  }

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    VkDescriptorBufferInfo buffer_info{.buffer = uniform_buffers[i],
                                       .offset = 0,
                                       .range = sizeof(UniformBufferObject)};
    VkDescriptorImageInfo image_info{
        .sampler = texture_sampler,
        .imageView = texture_image_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    std::array<VkWriteDescriptorSet, 2> descriptor_writes{
        VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_sets[i],
            .dstBinding = 0,      // binding location
            .dstArrayElement = 0, // first index
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pImageInfo = nullptr,       // refer to image data
            .pBufferInfo = &buffer_info, // refer to buffer data
            .pTexelBufferView = nullptr  // refer to buffer view
        },
        VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_sets[i],
            .dstBinding = 1,      // binding location
            .dstArrayElement = 0, // first index
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &image_info,  // refer to image data
            .pBufferInfo = nullptr,     // refer to buffer data
            .pTexelBufferView = nullptr // refer to buffer view
        }};
    vkUpdateDescriptorSets(device, (uint32_t)descriptor_writes.size(),
                           descriptor_writes.data(), 0, nullptr);
  }
}
void VulkanApplication::create_texture_image() {
  int tex_width, tex_height, tex_channels;
  stbi_uc *pixels = stbi_load("textures/texture.jpg", &tex_width, &tex_height,
                              &tex_channels, STBI_rgb_alpha);
  VkDeviceSize image_size = tex_width * tex_height * 4; // RGBA
  if (!pixels) {
    throw std::runtime_error("failed to load texture image!");
  }

  VkBuffer staging_buffer;
  VkDeviceMemory staging_buffer_memory;
  // create staging buffer for copy_buffer2image first
  create_buffer(image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                staging_buffer, staging_buffer_memory);

  void *data;
  vkMapMemory(device, staging_buffer_memory, 0, image_size, 0, &data);
  memcpy(data, pixels, (size_t)image_size);
  vkUnmapMemory(device, staging_buffer_memory);
  stbi_image_free(pixels);

  create_image(
      tex_width, tex_height, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, texture_image, texture_image_memory);

  // transfer image to target layout first
  transition_image_layout(texture_image, VK_FORMAT_R8G8B8A8_SRGB,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copy_buffer2image(staging_buffer, texture_image, (uint32_t)tex_width,
                    (uint32_t)tex_height);

  transition_image_layout(texture_image, VK_FORMAT_R8G8B8A8_SRGB,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  vkDestroyBuffer(device, staging_buffer, nullptr);
  vkFreeMemory(device, staging_buffer_memory, nullptr);
}

void VulkanApplication::create_image(uint32_t width, uint32_t height,
                                     VkFormat format, VkImageTiling tiling,
                                     VkImageUsageFlags usage,
                                     VkMemoryPropertyFlags properties,
                                     VkImage &image,
                                     VkDeviceMemory &image_memory) {
  VkImageCreateInfo image_info{
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = format,
      .extent{.width = (uint32_t)width, .height = (uint32_t)height, .depth = 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT, // for multi-sampling
      .tiling = tiling,                 // for optimal access
      .usage = usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };

  if (vkCreateImage(device, &image_info, nullptr, &image) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture image!");
  }

  VkMemoryRequirements mem_requirements;
  // not vkGetBufferMemoryRequirements
  vkGetImageMemoryRequirements(device, image, &mem_requirements);
  VkMemoryAllocateInfo alloc_info{
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mem_requirements.size,
      .memoryTypeIndex =
          find_memory_type(mem_requirements.memoryTypeBits, properties)};

  if (vkAllocateMemory(device, &alloc_info, nullptr, &image_memory) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to allocate texture image memory!");
  }
  vkBindImageMemory(device, image, image_memory, 0);
}

VkCommandBuffer
VulkanApplication::begin_single_commands(VkCommandPool command_pool) {
  VkCommandBufferAllocateInfo alloc_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = command_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1};

  VkCommandBuffer transfer_command_buffer;
  vkAllocateCommandBuffers(device, &alloc_info, &transfer_command_buffer);
  // create transient cmd buffer
  VkCommandBufferBeginInfo begin_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT // once submit, reset
  };

  if (vkBeginCommandBuffer(transfer_command_buffer, &begin_info) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to begin transfer command buffer!");
  }
  return transfer_command_buffer;
}

void VulkanApplication::end_single_commands(VkCommandBuffer command_buffer,
                                            VkCommandPool command_pool) {
  vkEndCommandBuffer(command_buffer); // stop recording

  VkSubmitInfo submit_info{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                           .commandBufferCount = 1,
                           .pCommandBuffers = &command_buffer};

  vkQueueSubmit(transfer_queue, 1, &submit_info, nullptr);
  vkQueueWaitIdle(transfer_queue);

  // don't forget to clean cmd buffer
  vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
}
void VulkanApplication::transition_image_layout(VkImage image, VkFormat format,
                                                VkImageLayout old_layout,
                                                VkImageLayout new_layout) {
  auto command_buffer = begin_single_commands(command_pool);

  VkPipelineStageFlags source_stage, destination_stage;

  VkImageMemoryBarrier barrier{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      // describe operations before or after the barrier
      .oldLayout = old_layout,
      .newLayout = new_layout,
      // set if u don't want queue families ownership transfer
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image,
      .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1}};

  // before copy buffer to image, set image layout from UNDEFINED to
  // TRANSFER_DST
  if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
      new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;   // earliest possible
    destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT; // pseudo transfer stage
  } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask =
        VK_ACCESS_TRANSFER_WRITE_BIT; // from copy operations
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  }

  vkCmdPipelineBarrier(command_buffer, source_stage, destination_stage, 0, 0,
                       nullptr, 0, nullptr, 1, &barrier);

  end_single_commands(command_buffer, command_pool);
}
void VulkanApplication::copy_buffer2image(VkBuffer buffer, VkImage image,
                                          uint32_t width, uint32_t height) {
  VkCommandBuffer command_buffer = begin_single_commands(transfer_command_pool);

  VkBufferImageCopy region{
      .bufferOffset = 0,
      .bufferRowLength = 0,
      .bufferImageHeight = 0,
      .imageSubresource{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .mipLevel = 0,
                        .baseArrayLayer = 0,
                        .layerCount = 1},
      .imageOffset{0, 0, 0},
      .imageExtent{width, height, 1}};

  vkCmdCopyBufferToImage(command_buffer, buffer, image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  end_single_commands(command_buffer, transfer_command_pool);
}
void VulkanApplication::create_texture_image_view() {
  texture_image_view =
      create_image_view(texture_image, VK_FORMAT_R8G8B8A8_SRGB);
}

VkImageView VulkanApplication::create_image_view(VkImage image,
                                                 VkFormat format) {
  VkImageViewCreateInfo create_info{
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = format,
      .subresourceRange{// image purpose and mipmap level
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1}};
  VkImageView image_view;
  if (vkCreateImageView(device, &create_info, nullptr, &image_view) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create image views!");
  }
  return image_view;
}

void VulkanApplication::create_texture_sampler() {
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(physical_device, &properties);

  VkSamplerCreateInfo sampler_info{
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = VK_FILTER_LINEAR, // over sampling
      .minFilter = VK_FILTER_LINEAR, // under sampling
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT, //
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .mipLodBias = 0.0f,
      .anisotropyEnable = VK_TRUE,
      .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
      .compareEnable = VK_FALSE, // mainly for PCF
      .compareOp = VK_COMPARE_OP_ALWAYS,
      .minLod = 0.0f,
      .maxLod = 0.0f,
      .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
      .unnormalizedCoordinates = VK_FALSE};

  if (vkCreateSampler(device, &sampler_info, nullptr, &texture_sampler) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create texture sampler!");
  }
}
