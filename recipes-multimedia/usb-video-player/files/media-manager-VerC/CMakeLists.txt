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
