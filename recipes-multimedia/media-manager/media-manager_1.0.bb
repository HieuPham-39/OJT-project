SUMMARY = "Automotive USB Video Player - Core Media Manager (Pure C)"
DESCRIPTION = "Standalone Core Media Manager daemon in pure C implementing direct OS hardware udev event detection, recursive MP4/MKV playlist scanning, strict Automotive FSM, and D-Bus IPC broker."
HOMEPAGE = "https://github.com/automotive/usb-video-player"
SECTION = "multimedia"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://CMakeLists.txt \
    file://main.c \
    file://logger.h \
    file://logger.c \
    file://fsm.h \
    file://fsm.c \
    file://usb_monitor.h \
    file://usb_monitor.c \
    file://playlist_scanner.h \
    file://playlist_scanner.c \
    file://media_manager_service.h \
    file://media_manager_service.c \
    file://org.automotive.MediaManager.conf \
    file://ojt-media-manager.service \
    file://ojt-media-manager.init \
"

S = "${WORKDIR}"

inherit cmake pkgconfig systemd update-rc.d

DEPENDS = " \
    glib-2.0 \
    udev \
"

RDEPENDS:${PN} = " \
    glib-2.0 \
    udev \
"

SYSTEMD_PACKAGES = "${PN}"
SYSTEMD_SERVICE:${PN} = "ojt-media-manager.service"
SYSTEMD_AUTO_ENABLE = "enable"

INITSCRIPT_NAME = "ojt-media-manager"
INITSCRIPT_PARAMS = "defaults 90 10"

do_install:append() {
    # Install SysVinit script
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/ojt-media-manager.init ${D}${sysconfdir}/init.d/ojt-media-manager

    # Install D-Bus configuration
    install -d ${D}${datadir}/dbus-1/system.d
    install -m 0644 ${WORKDIR}/org.automotive.MediaManager.conf ${D}${datadir}/dbus-1/system.d/

    # Install systemd unit if enabled
    if ${@bb.utils.contains('DISTRO_FEATURES', 'systemd', 'true', 'false', d)}; then
        install -d ${D}${systemd_system_unitdir}
        install -m 0644 ${WORKDIR}/ojt-media-manager.service ${D}${systemd_system_unitdir}/
    fi
}

FILES:${PN} += " \
    ${datadir}/dbus-1/system.d/org.automotive.MediaManager.conf \
    ${sysconfdir}/init.d/ojt-media-manager \
    ${systemd_system_unitdir}/*.service \
"
