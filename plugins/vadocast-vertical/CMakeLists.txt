cmake_minimum_required(VERSION 3.28...3.30)

# vadocast built-in module: second, vertical (9:16) canvas with its own scenes,
# streaming, recording and Backtrack. Derived from Aitum's GPL-2.0
# obs-vertical-canvas (https://github.com/Aitum/obs-vertical-canvas), see NOTICE.

find_package(Qt6 REQUIRED Widgets Core)

if(OS_LINUX OR OS_FREEBSD OR OS_OPENBSD)
  find_package(Qt6 REQUIRED Gui)
endif()

add_library(vadocast-vertical MODULE)
add_library(OBS::vadocast-vertical ALIAS vadocast-vertical)

target_sources(
  vadocast-vertical
  PRIVATE
    audio-wrapper-source.c
    audio-wrapper-source.h
    config-dialog.cpp
    config-dialog.hpp
    display-helpers.hpp
    hotkey-edit.cpp
    hotkey-edit.hpp
    multi-canvas-source.c
    multi-canvas-source.h
    name-dialog.cpp
    name-dialog.hpp
    obs-websocket-api.h
    projector.cpp
    projector.hpp
    qt-display.cpp
    qt-display.hpp
    resolution-utils.hpp
    resources.qrc
    scenes-dock.cpp
    scenes-dock.hpp
    source-tree.cpp
    source-tree.hpp
    sources-dock.cpp
    sources-dock.hpp
    transitions-dock.cpp
    transitions-dock.hpp
    vertical-canvas.cpp
    vertical-canvas.hpp
)

target_include_directories(vadocast-vertical PRIVATE "${CMAKE_SOURCE_DIR}/shared/vadocast")

target_compile_definitions(vadocast-vertical PRIVATE PROJECT_VERSION="${OBS_VERSION_CANONICAL}")

target_compile_options(
  vadocast-vertical
  PRIVATE $<$<PLATFORM_ID:Darwin>:-Wno-quoted-include-in-framework-header> $<$<PLATFORM_ID:Darwin>:-Wno-comma>
)

target_link_libraries(vadocast-vertical PRIVATE OBS::frontend-api OBS::libobs Qt::Core Qt::Widgets)

# qt-display.cpp uses qpa/qplatformnativeinterface.h on Wayland with Qt < 6.9
if((OS_LINUX OR OS_FREEBSD OR OS_OPENBSD) AND Qt6_VERSION VERSION_LESS "6.9.0")
  find_package(Qt6 REQUIRED GuiPrivate)
  target_link_libraries(vadocast-vertical PRIVATE Qt::GuiPrivate)
endif()

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in vadocast-vertical.rc)
  target_sources(vadocast-vertical PRIVATE vadocast-vertical.rc)

  set_property(TARGET vadocast-vertical APPEND PROPERTY AUTORCC_OPTIONS --format-version 1)
endif()

set_target_properties_obs(
  vadocast-vertical
  PROPERTIES FOLDER plugins/vadocast
             PREFIX ""
             AUTOMOC ON
             AUTOUIC ON
             AUTORCC ON
)
