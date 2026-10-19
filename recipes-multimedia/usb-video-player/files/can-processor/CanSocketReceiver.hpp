#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <functional>
#include <gio/gio.h>

namespace automotive {

using SpeedCallback = std::function<void(double speedKmh)>;
using GearCallback = std::function<void(const std::string& gear)>;

class CanSocketReceiver {
public:
    CanSocketReceiver();
    ~CanSocketReceiver();

    bool initialize(const std::string& interfaceName = "vcan0");
    void start();
    void stop();

    // Sends updates to Media Manager over D-Bus
    bool sendSpeedToMediaManager(double speedKmh);
    bool sendGearToMediaManager(const std::string& gear);

    // Simulation helpers
    void startSimulationCycle();
    void setSimulatedSpeed(double speed);
    void setSimulatedGear(const std::string& gear);

private:
    void socketCanThreadFunc();
    void simulationThreadFunc();

    std::string m_interfaceName{"vcan0"};
    int m_socketFd{-1};

    std::thread m_canThread;
    std::thread m_simThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_simulating{false};

    GDBusConnection* m_dbusConnection{nullptr};
};

} // namespace automotive
