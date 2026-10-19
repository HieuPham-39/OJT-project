SUMMARY = "OJT Automotive Multimedia Packagegroup"
DESCRIPTION = "Packages required for Automotive USB Video Player with CAN bus and D-Bus IPC"
LICENSE = "MIT"

inherit packagegroup

RDEPENDS:${PN} = " \
    usb-video-player \
    gstreamer1.0 \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    can-utils \
    dbus \
    eudev \
"
