
#include <iostream>
#include "vulkan_app.h"


int main(int argc, char** argv) {

  VulkanApplication app;
  try {
    app.run();
  }
  catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
