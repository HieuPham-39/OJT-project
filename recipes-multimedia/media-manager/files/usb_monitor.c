#include "usb_monitor.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <pthread.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <libudev.h>

struct UsbMonitor {
    struct udev *udev;
    struct udev_monitor *monitor;
    int monitor_fd;

    pthread_t thread;
    bool running;

    usb_inserted_cb_t inserted_cb;
    usb_removed_cb_t removed_cb;
    void *user_data;

    char last_dev_node[128];
    char last_mount_point[256];
    pthread_mutex_t lock;
};

UsbMonitor_t* usb_monitor_create(void) {
    UsbMonitor_t *mon = (UsbMonitor_t*)calloc(1, sizeof(UsbMonitor_t));
    if (!mon) return NULL;
    mon->monitor_fd = -1;
    pthread_mutex_init(&mon->lock, NULL);
    return mon;
}

void usb_monitor_destroy(UsbMonitor_t *mon) {
    if (!mon) return;
    usb_monitor_stop(mon);

    if (mon->monitor) {
        udev_monitor_unref(mon->monitor);
        mon->monitor = NULL;
    }
    if (mon->udev) {
        udev_unref(mon->udev);
        mon->udev = NULL;
    }
    pthread_mutex_destroy(&mon->lock);
    free(mon);
}

bool usb_monitor_init(UsbMonitor_t *mon) {
    if (!mon) return false;

    mon->udev = udev_new();
    if (!mon->udev) {
        LOG_E("UsbMon-C", "Failed to create udev context");
        return false;
    }

    // Direct OS hardware kernel event listener via Netlink socket
    mon->monitor = udev_monitor_new_from_netlink(mon->udev, "udev");
    if (!mon->monitor) {
        LOG_E("UsbMon-C", "Failed to create netlink udev monitor");
        return false;
    }

    // Filter block subsystem
    udev_monitor_filter_add_match_subsystem_devtype(mon->monitor, "block", NULL);

    if (udev_monitor_enable_receiving(mon->monitor) < 0) {
        LOG_E("UsbMon-C", "Failed to enable udev monitor receiving");
        return false;
    }

    mon->monitor_fd = udev_monitor_get_fd(mon->monitor);
    if (mon->monitor_fd < 0) {
        LOG_E("UsbMon-C", "Invalid udev netlink socket file descriptor");
        return false;
    }

    LOG_I("UsbMon-C", "Direct OS hardware event listener active on netlink fd=%d", mon->monitor_fd);
    return true;
}

void usb_monitor_set_callbacks(UsbMonitor_t *mon,
                               usb_inserted_cb_t ins_cb,
                               usb_removed_cb_t rem_cb,
                               void *user_data) {
    if (!mon) return;
    pthread_mutex_lock(&mon->lock);
    mon->inserted_cb = ins_cb;
    mon->removed_cb = rem_cb;
    mon->user_data = user_data;
    pthread_mutex_unlock(&mon->lock);
}

static bool find_mount_point(const char *dev_node, char *out_mount, size_t max_len) {
    FILE *fp = fopen("/proc/mounts", "r");
    if (!fp) return false;

    char line[512];
    char dev[128], mnt[256], fstype[64];
    bool found = false;

    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%127s %255s %63s", dev, mnt, fstype) >= 2) {
            if (strcmp(dev, dev_node) == 0) {
                strncpy(out_mount, mnt, max_len - 1);
                out_mount[max_len - 1] = '\0';
                found = true;
                break;
            }
        }
    }
    fclose(fp);
    return found;
}

static bool try_automount(struct udev_device *dev, const char *dev_node, char *out_mount, size_t max_len) {
    // 1. Check if device is already mounted (e.g. active system mount point)
    if (find_mount_point(dev_node, out_mount, max_len)) {
        LOG_I("UsbMon-C", "Detected active system mount point: %s", out_mount);
        return true;
    }

    // 2. Headless manual mount point
    const char *target = "/media/usb_ojt";
    struct stat st;
    if (stat(target, &st) != 0) {
        mkdir("/media", 0755);
        if (mkdir(target, 0755) != 0) {
            target = "/tmp/media_usb_ojt";
            mkdir(target, 0755);
        }
    }

    // 3. Try filesystem type directly reported by udev (e.g. "vfat", "ext4", "exfat")
    const char *fstype = dev ? udev_device_get_property_value(dev, "ID_FS_TYPE") : NULL;
    if (fstype && fstype[0] != '\0') {
        LOG_I("UsbMon-C", "Attempting manual mount using udev fstype '%s': %s -> %s", fstype, dev_node, target);
        if (mount(dev_node, target, fstype, MS_RDONLY | MS_NODEV | MS_NOEXEC, "") == 0) {
            LOG_I("UsbMon-C", "Successfully mounted %s (%s) at %s", dev_node, fstype, target);
            strncpy(out_mount, target, max_len - 1);
            out_mount[max_len - 1] = '\0';
            return true;
        }
        LOG_W("UsbMon-C", "Mount with '%s' failed (errno=%d: %s), falling back to common filesystems",
              fstype, errno, strerror(errno));
    }

    // 4. Fallback probing for common filesystems
    LOG_I("UsbMon-C", "Attempting fallback manual mount: %s -> %s", dev_node, target);
    const char *fs_types[] = {"vfat", "ext4", "exfat", "ntfs", NULL};
    for (int i = 0; fs_types[i] != NULL; ++i) {
        if (fstype && strcmp(fs_types[i], fstype) == 0) continue;
        if (mount(dev_node, target, fs_types[i], MS_RDONLY | MS_NODEV | MS_NOEXEC, "") == 0) {
            LOG_I("UsbMon-C", "Mounted %s (%s) at %s", dev_node, fs_types[i], target);
            strncpy(out_mount, target, max_len - 1);
            out_mount[max_len - 1] = '\0';
            return true;
        }
    }

    // 5. Final check if the device was mounted anywhere else
    if (find_mount_point(dev_node, out_mount, max_len)) {
        return true;
    }

    out_mount[0] = '\0';
    return false;
}

static void handle_udev_add(UsbMonitor_t *mon, struct udev_device *dev) {
    const char *devtype    = udev_device_get_devtype(dev);
    const char *devnode    = udev_device_get_devnode(dev);
    const char *fs_usage   = udev_device_get_property_value(dev, "ID_FS_USAGE");
    const char *part_table = udev_device_get_property_value(dev, "ID_PART_TABLE_TYPE");

    // Only process block devices containing a valid filesystem
    if (!devnode || !fs_usage || strcmp(fs_usage, "filesystem") != 0) {
        return;
    }

    // If whole disk has a partition table (MBR/GPT/dos), wait for partition event (e.g. sda1, sdb1)
    if (devtype && strcmp(devtype, "disk") == 0 && part_table != NULL) {
        LOG_I("UsbMon-C", "Disk %s has partition table (%s); waiting for partition event",
              devnode, part_table);
        return;
    }

    char mount_pt[256] = {0};
    if (!try_automount(dev, devnode, mount_pt, sizeof(mount_pt))) {
        LOG_W("UsbMon-C", "Failed to mount %s", devnode);
        return;
    }

    pthread_mutex_lock(&mon->lock);
    strncpy(mon->last_dev_node, devnode, sizeof(mon->last_dev_node) - 1);
    mon->last_dev_node[sizeof(mon->last_dev_node) - 1] = '\0';
    strncpy(mon->last_mount_point, mount_pt, sizeof(mon->last_mount_point) - 1);
    mon->last_mount_point[sizeof(mon->last_mount_point) - 1] = '\0';
    usb_inserted_cb_t cb = mon->inserted_cb;
    void *ud = mon->user_data;
    pthread_mutex_unlock(&mon->lock);

    if (cb) {
        cb(devnode, mount_pt, ud);
    }
}

static void handle_udev_remove(UsbMonitor_t *mon, const char *devnode) {
    char target[256] = {0};
    bool is_active = false;

    pthread_mutex_lock(&mon->lock);
    if (mon->last_dev_node[0] != '\0') {
        if (strcmp(devnode, mon->last_dev_node) == 0 ||
            strncmp(mon->last_dev_node, devnode, strlen(devnode)) == 0) {
            is_active = true;
            snprintf(target, sizeof(target), "%s", mon->last_mount_point);
            mon->last_dev_node[0] = '\0';
            mon->last_mount_point[0] = '\0';
        }
    } else {
        is_active = true;
    }
    usb_removed_cb_t cb = mon->removed_cb;
    void *ud = mon->user_data;
    pthread_mutex_unlock(&mon->lock);

    if (is_active) {
        if (target[0] != '\0') {
            LOG_I("UsbMon-C", "Detaching mount point %s", target);
            umount2(target, MNT_DETACH);
        } else {
            umount2("/media/usb_ojt", MNT_DETACH);
        }

        if (cb) {
            LOG_W("UsbMon-C", "USB Hardware Unplugged: %s", devnode);
            cb(devnode, ud);
        }
    }
}

// Enumerate any USB block device already connected at daemon launch
static void enumerate_existing_devices(UsbMonitor_t *mon) {
    if (!mon || !mon->udev) return;

    struct udev_enumerate *enumerate = udev_enumerate_new(mon->udev);
    if (!enumerate) return;

    udev_enumerate_add_match_subsystem(enumerate, "block");
    udev_enumerate_scan_devices(enumerate);

    struct udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
    struct udev_list_entry *dev_list_entry;

    udev_list_entry_foreach(dev_list_entry, devices) {
        const char *path = udev_list_entry_get_name(dev_list_entry);
        struct udev_device *dev = udev_device_new_from_syspath(mon->udev, path);
        if (!dev) continue;

        const char *id_bus = udev_device_get_property_value(dev, "ID_BUS");
        bool is_usb = false;
        if (id_bus && strcmp(id_bus, "usb") == 0) {
            is_usb = true;
        } else {
            struct udev_device *parent = udev_device_get_parent_with_subsystem_devtype(dev, "usb", "usb_device");
            if (parent) is_usb = true;
        }

        if (is_usb) {
            const char *devnode = udev_device_get_devnode(dev);
            LOG_I("UsbMon-C", "Evaluating pre-attached USB block device: %s", devnode ? devnode : "unknown");
            handle_udev_add(mon, dev);

            pthread_mutex_lock(&mon->lock);
            bool mounted = (mon->last_mount_point[0] != '\0');
            pthread_mutex_unlock(&mon->lock);

            if (mounted) {
                LOG_I("UsbMon-C", "Successfully initialized pre-attached USB: %s", devnode);
                udev_device_unref(dev);
                break;
            }
        }
        udev_device_unref(dev);
    }

    udev_enumerate_unref(enumerate);
}

static void* monitor_thread_func(void *arg) {
    UsbMonitor_t *mon = (UsbMonitor_t*)arg;
    struct pollfd pfd;
    pfd.fd = mon->monitor_fd;
    pfd.events = POLLIN;

    while (mon->running) {
        int ret = poll(&pfd, 1, 500); // 500ms timeout
        if (ret < 0) {
            if (errno == EINTR) continue;
            LOG_E("UsbMon-C", "poll() failed on netlink fd");
            break;
        }

        if (ret > 0 && (pfd.revents & POLLIN)) {
            struct udev_device *dev = udev_monitor_receive_device(mon->monitor);
            if (!dev) continue;

            const char *action = udev_device_get_action(dev);
            const char *devnode = udev_device_get_devnode(dev);
            const char *devtype = udev_device_get_devtype(dev);

            if (action && devnode) {
                // Verify if USB-backed
                bool is_usb = false;
                const char *id_bus = udev_device_get_property_value(dev, "ID_BUS");
                if (id_bus && strcmp(id_bus, "usb") == 0) {
                    is_usb = true;
                } else {
                    struct udev_device *parent = udev_device_get_parent_with_subsystem_devtype(dev, "usb", "usb_device");
                    if (parent) is_usb = true;
                }

                if (is_usb) {
                    LOG_I("UsbMon-C", "OS Hardware Event: Action=%s, DevNode=%s, DevType=%s",
                          action, devnode, devtype ? devtype : "unknown");

                    if (strcmp(action, "add") == 0) {
                        handle_udev_add(mon, dev);
                    } else if (strcmp(action, "remove") == 0) {
                        handle_udev_remove(mon, devnode);
                    }
                }
            }

            udev_device_unref(dev);
        }
    }

    return NULL;
}

void usb_monitor_start(UsbMonitor_t *mon) {
    if (!mon || mon->running) return;
    mon->running = true;

    // Scan for already-connected USB media before starting live netlink listener
    enumerate_existing_devices(mon);

    pthread_create(&mon->thread, NULL, monitor_thread_func, mon);
    LOG_I("UsbMon-C", "USB Hardware event listener thread started");
}

void usb_monitor_stop(UsbMonitor_t *mon) {
    if (!mon || !mon->running) return;
    mon->running = false;
    pthread_join(mon->thread, NULL);
    LOG_I("UsbMon-C", "USB Hardware event listener thread stopped");
}

void usb_monitor_simulate_insert(UsbMonitor_t *mon, const char *mount_path) {
    if (!mon) return;
    LOG_I("UsbMon-C", "[SIMULATION] Simulating USB insertion: path=%s", mount_path);

    pthread_mutex_lock(&mon->lock);
    strncpy(mon->last_dev_node, "/dev/simulated_usb0", sizeof(mon->last_dev_node) - 1);
    strncpy(mon->last_mount_point, mount_path ? mount_path : "", sizeof(mon->last_mount_point) - 1);
    usb_inserted_cb_t cb = mon->inserted_cb;
    void *ud = mon->user_data;
    pthread_mutex_unlock(&mon->lock);

    if (cb) {
        cb("/dev/simulated_usb0", mount_path ? mount_path : "", ud);
    }
}

void usb_monitor_simulate_remove(UsbMonitor_t *mon) {
    if (!mon) return;
    LOG_I("UsbMon-C", "[SIMULATION] Simulating USB unplug");

    pthread_mutex_lock(&mon->lock);
    char dev[128];
    strncpy(dev, mon->last_dev_node[0] ? mon->last_dev_node : "/dev/simulated_usb0", sizeof(dev) - 1);
    mon->last_dev_node[0] = '\0';
    mon->last_mount_point[0] = '\0';
    usb_removed_cb_t cb = mon->removed_cb;
    void *ud = mon->user_data;
    pthread_mutex_unlock(&mon->lock);

    if (cb) {
        cb(dev, ud);
    }
}
