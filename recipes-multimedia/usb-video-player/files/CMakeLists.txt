cmake_minimum_required(VERSION 3.16)
project(usb-video-player VERSION 1.0.0 LANGUAGES CXX C)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags for automotive safety and quality
add_compile_options(-Wall -Wextra -Wpedantic -O2 -pthread)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0 gio-2.0 gio-unix-2.0)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-base-1.0)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/common
)

# Common library containing FSM and Logger
add_library(ojt_common STATIC
    common/AutomotiveFSM.cpp
)
target_link_libraries(ojt_common PUBLIC
    PkgConfig::GLIB
    pthread
)

# Subprojects (Using C-version of media-manager: media-manager-VerC)
add_subdirectory(media-manager-VerC)
add_subdirectory(can-processor)
add_subdirectory(video-playback)
add_subdirectory(ui-controller)

# Install D-Bus config
install(FILES config/org.automotive.MediaManager.conf
        DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/system.d)

# Install systemd service files
install(FILES systemd/ojt-media-manager.service
              systemd/ojt-can-processor.service
              systemd/ojt-video-playback.service
        DESTINATION lib/systemd/system)
