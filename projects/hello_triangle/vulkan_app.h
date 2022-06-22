//
// Created by Admin on 2022/6/22.
//

#ifndef VK_TUTORIAL_VULKAN_APP_H
#define VK_TUTORIAL_VULKAN_APP_H

#include <Eigen/Core>
#include <SDL2/SDL.h>
#include <array>
#include <optional>
#include <vector>
#include <vulkan/vulkan.h>

typedef struct Vertex {
  using Vec2f = Eigen::Vector2f;
  using Vec3f = Eigen::Vector3f;
  Vec2f pos;
  Vec3f color;

  // A vertex binding describes at which rate to load data from memory
  // throughout the vertices.
  static VkVertexInputBindingDescription get_binding_description() {
    VkVertexInputBindingDescription binding_description{
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    return binding_description;
  }

  static std::array<VkVertexInputAttributeDescription, 2>
  get_attribute_descriptions() {
    std::array<VkVertexInputAttributeDescription, 2> attribute_descriptions = {
        VkVertexInputAttributeDescription{
            .location = 0, // location in vertex shader input
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,        // vec2
            .offset = (uint32_t)offsetof(Vertex, pos) // wtf, it exists?
        },
        VkVertexInputAttributeDescription{
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT, // vec3
            .offset = (uint32_t)offsetof(Vertex, color)}};
    return attribute_descriptions;
  }

} Vertex;

typedef struct QueueFamilyIndices {
  std::optional<uint32_t> graphics_family;
  std::optional<uint32_t> transfer_family;
  std::optional<uint32_t> present_family;
  bool is_complete() {
    return graphics_family.has_value() && transfer_family.has_value() &&
           present_family.has_value();
  }
} QueueFamilyIndices;

typedef struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR capabilities;
  std::vector<VkSurfaceFormatKHR> formats;
  std::vector<VkPresentModeKHR> present_modes;
} SwapChainSupportDetails;

class HelloTriangleApplication {
public:
  void run();

  std::vector<Vertex> vertices = {
      {{-0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}},
      {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
      {{0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}},
      {{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
  };

  std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0};

private:
  static const int MAX_FRAMES_IN_FLIGHT = 1;
  SDL_Window *window = nullptr;
  int width = 800;
  int height = 600;
  bool is_running = true;
  bool is_initialized = false;
  uint32_t extension_count = 0;
  uint32_t layer_count = 0;
  uint32_t device_count = 0;
  // do not use std::string_view: not ensure end '\0' format str
  std::vector<const char *> extension_names{};
  std::vector<const char *> validation_layers = {"VK_LAYER_KHRONOS_validation"};
  std::vector<const char *> device_extensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME};

#ifdef NDEBUG
  bool enable_validation_layers = false;
#else
  bool enable_validation_layers = true;
#endif

  VkInstance instance{0};
  VkDebugUtilsMessengerEXT debug_messenger;
  VkPhysicalDevice physical_device{VK_NULL_HANDLE};
  VkDevice device;
  VkQueue graphics_queue;
  VkQueue transfer_queue;
  VkQueue present_queue;
  VkSurfaceKHR surface;
  VkSwapchainKHR swapchain;
  std::vector<VkImage> swapchain_images;
  VkFormat swapchain_image_format;
  VkExtent2D swapchain_extent;
  std::vector<VkImageView> swapchain_image_views; // using an image as a texture
  VkRenderPass render_pass;
  VkPipelineLayout pipeline_layout;
  VkPipeline graphics_pipeline;
  std::vector<VkFramebuffer> swapchain_framebuffers;
  VkCommandPool command_pool;
  VkCommandPool transfer_command_pool;
  std::vector<VkCommandBuffer> command_buffers;
  std::vector<VkSemaphore> image_available_semaphores;
  std::vector<VkSemaphore> render_finished_semaphores;
  std::vector<VkFence> in_flight_fences;
  uint32_t current_frame{0};
  bool framebuffer_resized{false};
  bool window_minimized{true};

  VkBuffer vertex_buffer;
  VkDeviceMemory vertex_buffer_memory; // __DEVICE__
  VkBuffer index_buffer;
  VkDeviceMemory index_buffer_memory;

  void init_window();
  void init_vulkan();
  bool check_device_extension_support(VkPhysicalDevice device);

  bool check_validation_layer_support();
  void init_SDL2_extensions();
  bool check_extensions_support();
  void setup_debug_messenger();

  QueueFamilyIndices find_queue_families(VkPhysicalDevice physical_device);
  bool is_suitable_device(VkPhysicalDevice device);
  void pick_physical_device();
  void create_logical_device();
  // platform related part
  void create_surface();

  uint32_t find_memory_type(uint32_t type_filter,
                            VkMemoryPropertyFlags properties);

  // query swap_chain support
  SwapChainSupportDetails query_swapchain_support(VkPhysicalDevice device);
  VkSurfaceFormatKHR choose_swap_surface_format(
      const std::vector<VkSurfaceFormatKHR> &available_formats);
  VkPresentModeKHR choose_swap_present_mode(
      const std::vector<VkPresentModeKHR> &availabla_present_modes);
  VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR &capabilities);

  void create_swapchain();
  void create_image_views();
  void create_render_pass();
  VkShaderModule create_shader_module(std::vector<char> const &code);
  void create_graphics_pipeline();
  void create_framebuffers();
  void create_command_pool();
  // buffer creation helper
  void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags properties, VkBuffer &buffer,
                     VkDeviceMemory &buffer_memory);
  void copy_buffer(VkBuffer src_buffer, VkBuffer dst_buffer, VkDeviceSize size);
  void create_vertex_buffer();
  void create_index_buffer();
  void create_command_buffer();
  void record_command_buffer(VkCommandBuffer command_buffer,
                             uint32_t image_index);
  void create_sync_objects();
  void cleanup_swapchain();
  void recreate_swapchain();
  void create_instance();

  void main_loop();
  void draw_frame();
  void cleanup();
};

#endif // VK_TUTORIAL_VULKAN_APP_H
