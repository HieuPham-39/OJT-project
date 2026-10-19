add_executable(ojt-video-playback
    main.cpp
    GstPlayer.cpp
)

target_link_libraries(ojt-video-playback PRIVATE
    ojt_common
    PkgConfig::GLIB
    PkgConfig::GST
    pthread
)

install(TARGETS ojt-video-playback DESTINATION bin)
