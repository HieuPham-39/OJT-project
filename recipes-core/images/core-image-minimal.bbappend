# 1. Enable OpenSSH server with root passwordless login
IMAGE_FEATURES += " \
    debug-tweaks \
    ssh-server-openssh \
"

# 2. Install Automotive Multimedia packages and Linux kernel hardware modules
IMAGE_INSTALL += " \
    packagegroup-ojt-multimedia \
    kernel-modules \
    dbus \
    uwe5622-firmware \
"
