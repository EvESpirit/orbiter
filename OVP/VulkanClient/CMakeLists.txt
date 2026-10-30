# ==============================================================
# OVP/VulkanClient/CMakeLists.txt
# Part of the ORBITER VISUALISATION PROJECT (OVP)
# Licensed under the MIT License
# ==============================================================

include(FetchContent)

# Fetch matching header/single-TU Vulkan 1.3 dependencies.
# SOURCE_SUBDIR points to a dummy directory so CMake populates
# <pkg>_SOURCE_DIR without executing third-party install() targets.
FetchContent_Declare(
  vulkan_headers
  GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
  GIT_TAG        vulkan-sdk-1.3.296.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  _noop_header_only
)

FetchContent_Declare(
  volk
  GIT_REPOSITORY https://github.com/zeux/volk.git
  GIT_TAG        vulkan-sdk-1.3.296.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  _noop_header_only
)

FetchContent_Declare(
  vk_bootstrap
  GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
  GIT_TAG        v1.3.296
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  _noop_header_only
)

FetchContent_Declare(
  vma
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
  GIT_TAG        v3.1.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  _noop_header_only
)

FetchContent_MakeAvailable(imgui vulkan_headers volk vk_bootstrap vma)

set(VKCLIENT_SOURCES
  VkAPI.cpp
  VkLog.cpp
  VkConfig.cpp
  VideoTab.cpp
  VulkanClient.cpp
  ${vk_bootstrap_SOURCE_DIR}/src/VkBootstrap.cpp
)

set(VKCLIENT_HEADERS
  Resource.h
  VkAPI.h
  VkLog.h
  VkConfig.h
  VideoTab.h
  VulkanClient.h
)

set(VKCLIENT_API_HEADERS
  ${ORBITER_BINARY_SDK_DIR}/include/gcCoreAPI.h
  ${ORBITER_BINARY_SDK_DIR}/include/gcGUI.h
)

source_group("Include Files" FILES ${VKCLIENT_HEADERS})
source_group("Source Files" FILES ${VKCLIENT_SOURCES})
source_group("API Headers" FILES ${VKCLIENT_API_HEADERS})

add_library(VulkanClient MODULE
  ${VKCLIENT_SOURCES}
  ${VKCLIENT_HEADERS}
  ${VKCLIENT_API_HEADERS}
  VulkanClient.rc
)

target_compile_features(VulkanClient PRIVATE cxx_std_17)

target_compile_definitions(VulkanClient PRIVATE
  VULKANCLIENT_EXPORTS
  VK_NO_PROTOTYPES
  VK_USE_PLATFORM_WIN32_KHR
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  _CRT_SECURE_NO_WARNINGS
)

target_include_directories(VulkanClient PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${ORBITER_SOURCE_SDK_INCLUDE_DIR}
  ${ORBITER_BINARY_SDK_DIR}/include
  ${vulkan_headers_SOURCE_DIR}/include
  ${volk_SOURCE_DIR}
  ${vk_bootstrap_SOURCE_DIR}/src
  ${vma_SOURCE_DIR}/include
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)

target_link_directories(VulkanClient PRIVATE
  ${ORBITER_BINARY_SDK_DIR}/lib
)

target_link_libraries(VulkanClient PRIVATE
  orbiter.lib
  orbitersdk.lib
  kernel32.lib
  user32.lib
  gdi32.lib
  comctl32.lib
  comdlg32.lib
  advapi32.lib
  shell32.lib
  shlwapi.lib
  ole32.lib
  oleaut32.lib
  uuid.lib
  imm32.lib
  version.lib
  msimg32.lib
)

set_target_properties(VulkanClient PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_BINARY_DIR}/Modules/Plugin
  LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/Modules/Plugin
  FOLDER OVP
)

add_dependencies(VulkanClient
  ${OrbiterTgt}
  Orbitersdk
  OVP_Interface
)

install(TARGETS VulkanClient
  LIBRARY
  DESTINATION ${ORBITER_INSTALL_PLUGIN_DIR}
)
