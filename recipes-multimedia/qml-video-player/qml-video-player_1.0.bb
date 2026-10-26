SUMMARY = "Automotive QML Video Player GUI"
DESCRIPTION = "Qt5/QML In-Vehicle Infotainment video player with touch/mouse controls, playlist view, and CAN telematics synchronization."
HOMEPAGE = "https://github.com/automotive/usb-video-player"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://CMakeLists.txt \
    file://main.cpp \
    file://MediaController.hpp \
    file://MediaController.cpp \
    file://qml.qrc \
    file://qml/main.qml \
"

S = "${WORKDIR}"

inherit cmake_qt5

DEPENDS = " \
    qtbase \
    qtdeclarative \
    qtquickcontrols2 \
    qtmultimedia \
"

RDEPENDS:${PN} = " \
    qtbase \
    qtdeclarative-qmlplugins \
    qtquickcontrols2-qmlplugins \
    qtmultimedia-qmlplugins \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
"

FILES:${PN} += "${bindir}/ojt-qml-player"
