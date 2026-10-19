add_executable(ojt-ui-controller
    main.cpp
    HmiController.cpp
)

target_link_libraries(ojt-ui-controller PRIVATE
    ojt_common
    PkgConfig::GLIB
    pthread
)

install(TARGETS ojt-ui-controller DESTINATION bin)
