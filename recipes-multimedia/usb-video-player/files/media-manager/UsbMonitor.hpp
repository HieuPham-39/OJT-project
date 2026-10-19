#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <libudev.h>

namespace automotive {

using UsbInsertedCallback = std::function<void(const std::string& devNode, const std::string& mountPoint)>;
using UsbRemovedCallback = std::function<void(const std::string& devNode)>;

class UsbMonitor {
public:
    UsbMonitor();
    ~UsbMonitor();

    bool initialize();
    void start();
    void stop();

    void setInsertedCallback(UsbInsertedCallback cb);
    void setRemovedCallback(UsbRemovedCallback cb);

    // Diagnostics / Simulation
    void simulateInsert(const std::string& mountPath);
    void simulateRemove();

private:
    void monitorThreadFunc();
    std::string findMountPointForDevice(const std::string& devNode);
    bool tryAutoMount(const std::string& devNode, std::string& outMountPoint);

    struct udev* m_udev{nullptr};
    struct udev_monitor* m_monitor{nullptr};
    int m_monitorFd{-1};

    std::thread m_thread;
    std::atomic<bool> m_running{false};

    UsbInsertedCallback m_insertedCb;
    UsbRemovedCallback m_removedCb;

    std::string m_lastActiveDevNode;
    std::string m_lastActiveMountPoint;
};

} // namespace automotive
