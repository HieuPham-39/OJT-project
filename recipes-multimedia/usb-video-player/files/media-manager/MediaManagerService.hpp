#pragma once

#include "../common/AutomotiveFSM.hpp"
#include "../common/DBusCommon.hpp"
#include "UsbMonitor.hpp"
#include "PlaylistScanner.hpp"

#include <gio/gio.h>
#include <memory>
#include <string>
#include <mutex>

namespace automotive {

class MediaManagerService {
public:
    MediaManagerService();
    ~MediaManagerService();

    bool initialize();
    void run();
    void stop();

    // Internal actions triggered by USB events
    void handleUsbInserted(const std::string& devNode, const std::string& mountPoint);
    void handleUsbRemoved(const std::string& devNode);

    // D-Bus method dispatch
    void handleMethodCall(GDBusConnection* connection,
                         const gchar* sender,
                         const gchar* object_path,
                         const gchar* interface_name,
                         const gchar* method_name,
                         GVariant* parameters,
                         GDBusMethodInvocation* invocation);

    // D-Bus signal broadcasters
    void emitStateChanged(const std::string& newState, const std::string& reason);
    void emitPlaylistChanged(int32_t count);
    void emitTrackChanged(int32_t trackId, const std::string& title, const std::string& path);
    void emitPlaybackCommand(const std::string& command, const std::string& arg);
    void emitPlaybackStatusChanged(const std::string& status, int64_t posMs, int64_t durMs);
    void emitVehicleSpeedChanged(double speedKmh, bool isRestricted);
    void emitSafetyWarning(const std::string& message);

private:
    void registerDBusInterface(GDBusConnection* connection);

    static void onBusAcquired(GDBusConnection* connection, const gchar* name, gpointer user_data);
    static void onNameAcquired(GDBusConnection* connection, const gchar* name, gpointer user_data);
    static void onNameLost(GDBusConnection* connection, const gchar* name, gpointer user_data);

    AutomotiveFSM m_fsm;
    UsbMonitor m_usbMonitor;
    PlaylistScanner m_scanner;

    GMainLoop* m_mainLoop{nullptr};
    GDBusConnection* m_connection{nullptr};
    guint m_ownerId{0};
    guint m_registrationId{0};
    GDBusNodeInfo* m_introspectionData{nullptr};

    std::mutex m_stateMutex;
    int32_t m_currentTrackIndex{-1};
    VideoTrack m_currentTrack;
    std::string m_lastPlaybackStatus{"STOPPED"};
    int64_t m_lastPositionMs{0};
    int64_t m_lastDurationMs{0};
};

} // namespace automotive
