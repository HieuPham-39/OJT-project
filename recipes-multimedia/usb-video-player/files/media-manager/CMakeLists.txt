add_executable(ojt-media-manager
    main.cpp
    UsbMonitor.cpp
    PlaylistScanner.cpp
    MediaManagerService.cpp
)

target_link_libraries(ojt-media-manager PRIVATE
    ojt_common
    PkgConfig::GLIB
    PkgConfig::UDEV
    pthread
)

install(TARGETS ojt-media-manager DESTINATION bin)
