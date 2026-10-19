#include "CanSocketReceiver.hpp"
#include "../common/DBusCommon.hpp"
#include "../common/Logger.hpp"

#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <poll.h>
#include <chrono>

namespace automotive {

CanSocketReceiver::CanSocketReceiver() = default;

CanSocketReceiver::~CanSocketReceiver() {
    stop();
    if (m_socketFd >= 0) {
        close(m_socketFd);
        m_socketFd = -1;
    }
    if (m_dbusConnection) {
        g_object_unref(m_dbusConnection);
        m_dbusConnection = nullptr;
    }
}

bool CanSocketReceiver::initialize(const std::string& interfaceName) {
    m_interfaceName = interfaceName;

    // Connect to D-Bus (System bus preferred, fallback to Session bus)
    GError* error = nullptr;
    m_dbusConnection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (!m_dbusConnection) {
        LOG_W("CAN", "System D-Bus unavailable (" + std::string(error->message) + "). Trying Session Bus...");
        g_clear_error(&error);
        m_dbusConnection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
        if (!m_dbusConnection) {
            LOG_E("CAN", "Failed to connect to D-Bus: " + std::string(error->message));
            g_clear_error(&error);
            return false;
        }
    }
    LOG_I("CAN", "Connected to D-Bus IPC channel");

    // Initialize SocketCAN
    m_socketFd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (m_socketFd < 0) {
        LOG_W("CAN", "Failed to open CAN socket. Hardware SocketCAN unavailable. Will use simulation mode.");
        return true;
    }

    struct ifreq ifr;
    std::strncpy(ifr.ifr_name, m_interfaceName.c_str(), IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';

    if (ioctl(m_socketFd, SIOCGIFINDEX, &ifr) < 0) {
        LOG_W("CAN", "CAN interface " + m_interfaceName + " not found. Will use simulation mode.");
        close(m_socketFd);
        m_socketFd = -1;
        return true;
    }

    struct sockaddr_can addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(m_socketFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG_W("CAN", "Failed to bind to " + m_interfaceName + ". Will use simulation mode.");
        close(m_socketFd);
        m_socketFd = -1;
        return true;
    }

    LOG_I("CAN", "SocketCAN successfully bound to interface: " + m_interfaceName);
    return true;
}

void CanSocketReceiver::start() {
    if (m_running) return;
    m_running = true;

    if (m_socketFd >= 0) {
        m_canThread = std::thread(&CanSocketReceiver::socketCanThreadFunc, this);
        LOG_I("CAN", "SocketCAN listener thread running on " + m_interfaceName);
    } else {
        LOG_I("CAN", "SocketCAN hardware absent. Automatic Drive Cycle Simulator active.");
        startSimulationCycle();
    }
}

void CanSocketReceiver::stop() {
    if (!m_running) return;
    m_running = false;
    m_simulating = false;

    if (m_canThread.joinable()) {
        m_canThread.join();
    }
    if (m_simThread.joinable()) {
        m_simThread.join();
    }
    LOG_I("CAN", "CAN processor thread stopped");
}

void CanSocketReceiver::socketCanThreadFunc() {
    struct pollfd pfd;
    pfd.fd = m_socketFd;
    pfd.events = POLLIN;

    while (m_running) {
        int ret = poll(&pfd, 1, 500);
        if (ret < 0) {
            if (errno == EINTR) continue;
            LOG_E("CAN", "poll() error on CAN socket");
            break;
        }

        if (ret > 0 && (pfd.revents & POLLIN)) {
            struct can_frame frame;
            int nbytes = read(m_socketFd, &frame, sizeof(struct can_frame));
            if (nbytes < 0) {
                LOG_E("CAN", "read() error on CAN socket");
                continue;
            }

            // Standard Automotive IDs:
            // 0x100: Speed Frame (Bytes 0-1 = Speed in 0.01 km/h)
            // 0x101: Gear Frame  (Byte 0 = 'P', 'R', 'N', 'D')
            if (frame.can_id == 0x100 && frame.can_dlc >= 2) {
                uint16_t rawSpeed = (static_cast<uint16_t>(frame.data[0]) << 8) | frame.data[1];
                double speedKmh = rawSpeed * 0.01;
                LOG_I("CAN", "[CAN RX 0x100] Vehicle Speed: " + std::to_string(speedKmh) + " km/h");
                sendSpeedToMediaManager(speedKmh);
            } 
            else if (frame.can_id == 0x101 && frame.can_dlc >= 1) {
                char gearChar = static_cast<char>(frame.data[0]);
                std::string gear(1, gearChar);
                LOG_I("CAN", "[CAN RX 0x101] Vehicle Gear: " + gear);
                sendGearToMediaManager(gear);
            }
        }
    }
}

bool CanSocketReceiver::sendSpeedToMediaManager(double speedKmh) {
    if (!m_dbusConnection) return false;

    GError* error = nullptr;
    GVariant* res = g_dbus_connection_call_sync(
        m_dbusConnection,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE,
        "UpdateVehicleSpeed",
        g_variant_new("(d)", speedKmh),
        G_VARIANT_TYPE("(b)"),
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        nullptr,
        &error
    );

    if (!res) {
        LOG_W("CAN", "Failed to call UpdateVehicleSpeed over D-Bus: " + std::string(error ? error->message : "unknown"));
        if (error) g_error_free(error);
        return false;
    }

    g_variant_unref(res);
    return true;
}

bool CanSocketReceiver::sendGearToMediaManager(const std::string& gear) {
    if (!m_dbusConnection) return false;

    GError* error = nullptr;
    GVariant* res = g_dbus_connection_call_sync(
        m_dbusConnection,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE,
        "UpdateGear",
        g_variant_new("(s)", gear.c_str()),
        G_VARIANT_TYPE("(b)"),
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        nullptr,
        &error
    );

    if (!res) {
        LOG_W("CAN", "Failed to call UpdateGear over D-Bus: " + std::string(error ? error->message : "unknown"));
        if (error) g_error_free(error);
        return false;
    }

    g_variant_unref(res);
    return true;
}

void CanSocketReceiver::setSimulatedSpeed(double speed) {
    LOG_I("CAN", "[SIMULATION INJECT] Setting speed to " + std::to_string(speed) + " km/h");
    sendSpeedToMediaManager(speed);
}

void CanSocketReceiver::setSimulatedGear(const std::string& gear) {
    LOG_I("CAN", "[SIMULATION INJECT] Setting gear to " + gear);
    sendGearToMediaManager(gear);
}

void CanSocketReceiver::startSimulationCycle() {
    if (m_simulating) return;
    m_simulating = true;
    m_simThread = std::thread(&CanSocketReceiver::simulationThreadFunc, this);
}

void CanSocketReceiver::simulationThreadFunc() {
    LOG_I("CAN", "Automotive Drive Cycle Simulator started (Park -> Drive -> Speed Up -> Stop -> Park)");

    // Initial state: Stationary in Park
    sendGearToMediaManager("P");
    sendSpeedToMediaManager(0.0);

    while (m_running && m_simulating) {
        // Stay parked for 10 seconds
        for (int i = 0; i < 10 && m_running && m_simulating; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (!m_running || !m_simulating) break;

        // Shift to Drive
        LOG_S("CAN", ">> Driving Cycle: Shift to DRIVE");
        sendGearToMediaManager("D");
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Accelerate: 10, 30, 60 km/h
        const double speeds[] = {15.0, 35.0, 60.0, 45.0, 20.0};
        for (double s : speeds) {
            if (!m_running || !m_simulating) break;
            LOG_S("CAN", ">> Driving Cycle: Vehicle Speed = " + std::to_string(s) + " km/h (Safety lock active)");
            sendSpeedToMediaManager(s);
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }

        // Decelerate to 0 km/h and shift back to Park
        if (!m_running || !m_simulating) break;
        LOG_S("CAN", ">> Driving Cycle: Vehicle Stopped (0.0 km/h)");
        sendSpeedToMediaManager(0.0);
        std::this_thread::sleep_for(std::chrono::seconds(1));

        LOG_S("CAN", ">> Driving Cycle: Shift to PARK (Safety lock cleared)");
        sendGearToMediaManager("P");
    }
}

} // namespace automotive
