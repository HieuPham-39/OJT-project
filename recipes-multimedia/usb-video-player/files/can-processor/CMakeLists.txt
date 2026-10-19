add_executable(ojt-can-processor
    main.cpp
    CanSocketReceiver.cpp
)

target_link_libraries(ojt-can-processor PRIVATE
    ojt_common
    PkgConfig::GLIB
    pthread
)

install(TARGETS ojt-can-processor DESTINATION bin)
