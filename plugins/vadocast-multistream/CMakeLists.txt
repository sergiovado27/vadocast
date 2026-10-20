cmake_minimum_required(VERSION 3.28...3.30)

# vadocast built-in module: extra simultaneous stream outputs for the main and
# the vertical canvas. Derived from Aitum's GPL-2.0 obs-aitum-multistream
# (https://github.com/Aitum/obs-aitum-multistream), see NOTICE.

find_package(Qt6 REQUIRED Widgets Core)

add_library(vadocast-multistream MODULE)
add_library(OBS::vadocast-multistream ALIAS vadocast-multistream)

target_sources(
  vadocast-multistream
  PRIVATE
    config-dialog.cpp
    config-dialog.hpp
    config-utils.cpp
    config-utils.hpp
    golive-dialog.cpp
    golive-dialog.hpp
    golive.cpp
    multistream.cpp
    multistream.hpp
    obs-websocket-api.h
    output-dialog.cpp
    output-dialog.hpp
    resolution-utils.hpp
    resources.qrc
    stream-key-input.cpp
    stream-key-input.hpp
)

target_include_directories(vadocast-multistream PRIVATE "${CMAKE_SOURCE_DIR}/shared/vadocast")

target_compile_definitions(vadocast-multistream PRIVATE PROJECT_VERSION="${OBS_VERSION_CANONICAL}")

target_compile_options(
  vadocast-multistream
  PRIVATE $<$<PLATFORM_ID:Darwin>:-Wno-quoted-include-in-framework-header> $<$<PLATFORM_ID:Darwin>:-Wno-comma>
)

target_link_libraries(vadocast-multistream PRIVATE OBS::frontend-api OBS::libobs Qt::Core Qt::Widgets)

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in vadocast-multistream.rc)
  target_sources(vadocast-multistream PRIVATE vadocast-multistream.rc)

  set_property(TARGET vadocast-multistream APPEND PROPERTY AUTORCC_OPTIONS --format-version 1)
endif()

set_target_properties_obs(
  vadocast-multistream
  PROPERTIES FOLDER plugins/vadocast
             PREFIX ""
             AUTOMOC ON
             AUTOUIC ON
             AUTORCC ON
)
