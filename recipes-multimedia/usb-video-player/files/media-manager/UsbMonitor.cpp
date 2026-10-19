#include "UsbMonitor.hpp"
#include "../common/Logger.hpp"

#include <poll.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <fstream>
#include <sstream>
#include <chrono>

namespace automotive {

UsbMonitor::UsbMonitor() = default;

UsbMonitor::~UsbMonitor() {
    stop();
    if (m_monitor) {
        udev_monitor_unref(m_monitor);
        m_monitor = nullptr;
    }
    if (m_udev) {
        udev_unref(m_udev);
        m_udev = nullptr;
    }
}

bool UsbMonitor::initialize() {
    m_udev = udev_new();
    if (!m_udev) {
        LOG_E("UsbMonitor", "Failed to initialize libudev context");
        return false;
    }

    // Connect to kernel netlink udev events directly
    m_monitor = udev_monitor_new_from_netlink(m_udev, "udev");
    if (!m_monitor) {
        LOG_E("UsbMonitor", "Failed to create udev netlink monitor");
        return false;
    }

    // Filter block subsystem (disks and partitions)
    if (udev_monitor_filter_add_match_subsystem_devtype(m_monitor, "block", nullptr) < 0) {
        LOG_W("UsbMonitor", "Failed to add block subsystem filter, will filter manually");
    }

    if (udev_monitor_enable_receiving(m_monitor) < 0) {
        LOG_E("UsbMonitor", "Failed to enable udev monitor receiving");
        return false;
    }

    m_monitorFd = udev_monitor_get_fd(m_monitor);
    if (m_monitorFd < 0) {
        LOG_E("UsbMonitor", "Invalid udev monitor file descriptor");
        return false;
    }

    LOG_I("UsbMonitor", "Direct OS hardware event listener initialized on netlink socket fd=" + std::to_string(m_monitorFd));
    return true;
}

void UsbMonitor::setInsertedCallback(UsbInsertedCallback cb) {
    m_insertedCb = std::move(cb);
}

void UsbMonitor::setRemovedCallback(UsbRemovedCallback cb) {
    m_removedCb = std::move(cb);
}

void UsbMonitor::start() {
    if (m_running) return;
    m_running = true;
    m_thread = std::thread(&UsbMonitor::monitorThreadFunc, this);
    LOG_I("UsbMonitor", "USB hardware event listening thread started");
}

void UsbMonitor::stop() {
    if (!m_running) return;
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
    LOG_I("UsbMonitor", "USB hardware event listening thread stopped");
}

std::string UsbMonitor::findMountPointForDevice(const std::string& devNode) {
    std::ifstream mounts("/proc/mounts");
    if (!mounts.is_open()) return "";

    std::string line;
    while (std::getline(mounts, line)) {
        std::istringstream iss(line);
        std::string dev, mnt, fstype;
        if (iss >> dev >> mnt >> fstype) {
            if (dev == devNode) {
                return mnt;
            }
        }
    }
    return "";
}

bool UsbMonitor::tryAutoMount(const std::string& devNode, std::string& outMountPoint) {
    // Check if system automounter (udev/udisks2) already mounted it
    for (int retry = 0; retry < 5; ++retry) {
        std::string mnt = findMountPointForDevice(devNode);
        if (!mnt.empty()) {
            outMountPoint = mnt;
            LOG_I("UsbMonitor", "Detected system mount point: " + outMountPoint);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // If not mounted by OS automounter, create a fallback mount point
    std::string mountTarget = "/media/usb_ojt";
    struct stat st;
    if (stat(mountTarget.c_str(), &st) != 0) {
        mkdir("/media", 0755);
        if (mkdir(mountTarget.c_str(), 0755) != 0) {
            mountTarget = "/tmp/media_usb_ojt";
            mkdir(mountTarget.c_str(), 0755);
        }
    }

    LOG_I("UsbMonitor", "Attempting automount: " + devNode + " -> " + mountTarget);
    
    // Try vfat first, then ext4, ntfs, exfat
    const char* fsTypes[] = {"vfat", "ext4", "ntfs", "exfat", nullptr};
    for (int i = 0; fsTypes[i] != nullptr; ++i) {
        if (mount(devNode.c_str(), mountTarget.c_str(), fsTypes[i], MS_RDONLY | MS_NODEV | MS_NOEXEC, "") == 0) {
            LOG_I("UsbMonitor", "Successfully mounted " + devNode + " as " + fsTypes[i] + " at " + mountTarget);
            outMountPoint = mountTarget;
            return true;
        }
    }

    // Fallback: Check if device is readable or mounted elsewhere under /run/media or /media
    outMountPoint = mountTarget;
    return false;
}

void UsbMonitor::monitorThreadFunc() {
    struct pollfd pfd;
    pfd.fd = m_monitorFd;
    pfd.events = POLLIN;

    while (m_running) {
        int ret = poll(&pfd, 1, 500); // 500ms timeout
        if (ret < 0) {
            if (errno == EINTR) continue;
            LOG_E("UsbMonitor", "poll() error on udev socket");
            break;
        }

        if (ret > 0 && (pfd.revents & POLLIN)) {
            struct udev_device* dev = udev_monitor_receive_device(m_monitor);
            if (!dev) continue;

            const char* action = udev_device_get_action(dev);
            const char* devnode = udev_device_get_devnode(dev);
            const char* devtype = udev_device_get_devtype(dev);

            if (action && devnode) {
                // Verify if this block device originates from USB
                const char* idBus = udev_device_get_property_value(dev, "ID_BUS");
                bool isUsb = false;
                if (idBus && std::string(idBus) == "usb") {
                    isUsb = true;
                } else {
                    // Check parent USB subsystem
                    struct udev_device* parent = udev_device_get_parent_with_subsystem_devtype(dev, "usb", "usb_device");
                    if (parent) isUsb = true;
                }

                if (isUsb) {
                    std::string act(action);
                    std::string node(devnode);
                    std::string type = devtype ? devtype : "";

                    LOG_I("UsbMonitor", "Hardware Kernel Event: Action=" + act + 
                                        ", Node=" + node + ", Type=" + type);

                    if (act == "add") {
                        // Give system time to settle partition table
                        std::this_thread::sleep_for(std::chrono::milliseconds(300));
                        std::string mountPoint;
                        tryAutoMount(node, mountPoint);

                        m_lastActiveDevNode = node;
                        m_lastActiveMountPoint = mountPoint;

                        if (m_insertedCb) {
                            m_insertedCb(node, mountPoint);
                        }
                    } else if (act == "remove") {
                        if (node == m_lastActiveDevNode || m_lastActiveDevNode.empty()) {
                            LOG_W("UsbMonitor", "USB Device Unplugged: " + node);
                            m_lastActiveDevNode.clear();
                            m_lastActiveMountPoint.clear();
                            if (m_removedCb) {
                                m_removedCb(node);
                            }
                        }
                    }
                }
            }

            udev_device_unref(dev);
        }
    }
}

void UsbMonitor::simulateInsert(const std::string& mountPath) {
    LOG_I("UsbMonitor", "[SIMULATION] Simulating USB insertion with mountPath: " + mountPath);
    m_lastActiveDevNode = "/dev/simulated_usb0";
    m_lastActiveMountPoint = mountPath;
    if (m_insertedCb) {
        m_insertedCb(m_lastActiveDevNode, mountPath);
    }
}

void UsbMonitor::simulateRemove() {
    LOG_I("UsbMonitor", "[SIMULATION] Simulating USB removal");
    std::string dev = m_lastActiveDevNode.empty() ? "/dev/simulated_usb0" : m_lastActiveDevNode;
    m_lastActiveDevNode.clear();
    m_lastActiveMountPoint.clear();
    if (m_removedCb) {
        m_removedCb(dev);
    }
}

} // namespace automotive
