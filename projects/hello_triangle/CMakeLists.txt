
project(Triangle)

add_executable(${PROJECT_NAME} main.cpp)

find_package(Eigen3 CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Eigen3::Eigen)

find_package(SDL2 CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2 SDL2::SDL2main)

find_package(Vulkan REQUIRED)
message(${Vulkan_LIBRARIES} ${Vulkan_INCLUDE_DIRS})
target_include_directories(${PROJECT_NAME} PRIVATE ${Vulkan_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${Vulkan_LIBRARIES})


# find_path(VULKAN_HEADERS_INCLUDE_DIRS "vk_video/vulkan_video_codec_h264std.h")
# target_include_directories(${PROJECT_NAME} PRIVATE ${VULKAN_HEADERS_INCLUDE_DIRS})