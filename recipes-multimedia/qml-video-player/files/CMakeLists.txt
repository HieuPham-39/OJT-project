cmake_minimum_required(VERSION 3.14)
project(ojt-qml-video-player LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
    Multimedia
)

find_package(Qt5DBus QUIET)
if (Qt5DBus_FOUND)
    add_definitions(-DQT_DBUS_LIB)
endif()

set(SOURCES
    main.cpp
    MediaController.hpp
    MediaController.cpp
    qml.qrc
)

add_executable(ojt-qml-player ${SOURCES})

target_link_libraries(ojt-qml-player PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Qml
    Qt5::Quick
    Qt5::QuickControls2
    Qt5::Multimedia
)

if (Qt5DBus_FOUND)
    target_link_libraries(ojt-qml-player PRIVATE Qt5::DBus)
endif()

include(GNUInstallDirs)
install(TARGETS ojt-qml-player DESTINATION ${CMAKE_INSTALL_BINDIR})
