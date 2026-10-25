# Enable networkd and resolved for static IP and DNS resolution
PACKAGECONFIG:append = " networkd resolved"

# Automatically enable systemd-networkd service on boot
SYSTEMD_AUTO_ENABLE:systemd-networkd = "enable"
