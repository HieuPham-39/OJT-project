SUMMARY = "Automotive Multiprocess USB Video Player"
DESCRIPTION = "Automotive-grade multiprocess USB video player featuring direct OS hardware udev event detection, recursive .mp4/.mkv playlist generation, D-Bus IPC broker, SocketCAN speed interlock, and strict Finite State Machine."
HOMEPAGE = "https://github.com/automotive/usb-video-player"
SECTION = "multimedia"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://CMakeLists.txt \
    file://common/ \
    file://media-manager-VerC/ \
    file://can-processor/ \
    file://video-playback/ \
    file://ui-controller/ \
    file://config/ \
    file://systemd/ \
    file://sysvinit/ \
"

S = "${WORKDIR}"

inherit cmake pkgconfig systemd update-rc.d

DEPENDS = " \
    glib-2.0 \
    udev \
    gstreamer1.0 \
    gstreamer1.0-plugins-base \
"

RDEPENDS:${PN} = " \
    glib-2.0 \
    udev \
    gstreamer1.0 \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
"

RRECOMMENDS:${PN} = " \
    can-utils \
"

# Systemd integration
SYSTEMD_PACKAGES = "${PN}"
SYSTEMD_SERVICE:${PN} = " \
    ojt-media-manager.service \
    ojt-can-processor.service \
    ojt-video-playback.service \
"
SYSTEMD_AUTO_ENABLE = "enable"

# SysVinit integration
INITSCRIPT_NAME = "ojt-media-manager"
INITSCRIPT_PARAMS = "defaults 90 10"

do_install:append() {
    # Install SysVinit script
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/sysvinit/ojt-media-manager.init ${D}${sysconfdir}/init.d/ojt-media-manager

    # Install udev rules
    install -d ${D}${sysconfdir}/udev/rules.d
    install -m 0644 ${WORKDIR}/config/99-usb-video.rules ${D}${sysconfdir}/udev/rules.d/99-usb-video.rules

    # Ensure systemd directory is present if systemd is active
    if ${@bb.utils.contains('DISTRO_FEATURES', 'systemd', 'true', 'false', d)}; then
        install -d ${D}${systemd_system_unitdir}
        install -m 0644 ${WORKDIR}/systemd/ojt-media-manager.service ${D}${systemd_system_unitdir}/
        install -m 0644 ${WORKDIR}/systemd/ojt-can-processor.service ${D}${systemd_system_unitdir}/
        install -m 0644 ${WORKDIR}/systemd/ojt-video-playback.service ${D}${systemd_system_unitdir}/
    fi
}

FILES:${PN} += " \
    ${datadir}/dbus-1/system.d/org.automotive.MediaManager.conf \
    ${sysconfdir}/udev/rules.d/99-usb-video.rules \
    ${sysconfdir}/init.d/ojt-media-manager \
    ${systemd_system_unitdir}/*.service \
"
