if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.16)
    project(media-manager-VerC VERSION 1.0.0 LANGUAGES C)
    include(GNUInstallDirs)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0 gio-2.0 gio-unix-2.0)
    pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)
endif()

add_executable(ojt-media-manager
    main.c
    logger.c
    fsm.c
    usb_monitor.c
    playlist_scanner.c
    media_manager_service.c
)

target_link_libraries(ojt-media-manager PRIVATE
    PkgConfig::GLIB
    PkgConfig::UDEV
    pthread
)

install(TARGETS ojt-media-manager DESTINATION bin)
