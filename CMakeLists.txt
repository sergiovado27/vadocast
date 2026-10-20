cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(obs-studio VERSION ${OBS_VERSION_CANONICAL})

if(CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows/architecture.cmake")
  if(NOT OBS_PARENT_ARCHITECTURE STREQUAL CMAKE_VS_PLATFORM_NAME)
    return()
  endif()
endif()

include(compilerconfig)
include(defaults)
include(helpers)

option(ENABLE_FRONTEND "Enable building with UI (requires Qt)" ON)
# vadocast: Lua/Python scripting is removed.
option(ENABLE_SCRIPTING "Enable scripting support" OFF)
option(ENABLE_HEVC "Enable HEVC encoders" ON)

# vadocast: 720p/1080p are always available. 1440p (2K) is prepared but off until enabled here.
option(VADOCAST_ENABLE_1440P "Allow 1440p (2K) horizontal and vertical canvases" OFF)
if(VADOCAST_ENABLE_1440P)
  add_compile_definitions(VADOCAST_ENABLE_1440P)
endif()
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/shared/vadocast")

# vadocast's own version, shown next to the OBS Studio version it is based on.
set(VADOCAST_VERSION "0.1.0")
add_compile_definitions(VADOCAST_VERSION="${VADOCAST_VERSION}")

add_subdirectory(libobs)
if(OS_WINDOWS)
  add_subdirectory(libobs-d3d11)
  add_subdirectory(libobs-winrt)
endif()
add_subdirectory(libobs-opengl)
if(OS_MACOS)
  add_subdirectory(libobs-metal)
endif()
add_subdirectory(plugins)

add_subdirectory(test/test-input)

add_subdirectory(frontend)

message_configuration()
