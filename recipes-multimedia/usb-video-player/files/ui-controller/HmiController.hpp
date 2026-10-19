#pragma once

#include "../common/DBusCommon.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <gio/gio.h>

namespace automotive {

class HmiController {
public:
    HmiController();
    ~HmiController();

    bool initialize();
    void run();
    void stop();

    // D-Bus Method Calls (User actions)
    void requestPlay(int32_t trackId);
    void requestPause();
    void requestResume();
    void requestStop();
    void requestNext();
    void requestPrev();
    void requestSimulateUsb(const std::string& action, const std::string& path);

    // Initial data fetch
    void fetchState();
    void fetchPlaylist();

    // Render Dashboard
    void renderDashboard();

private:
    void subscribeSignals();
    void inputLoop();

    static void onSignalReceived(GDBusConnection* connection,
                                const gchar* sender_name,
                                const gchar* object_path,
                                const gchar* interface_name,
                                const gchar* signal_name,
                                GVariant* parameters,
                                gpointer user_data);

    GMainLoop* m_mainLoop{nullptr};
    GDBusConnection* m_dbusConnection{nullptr};
    std::vector<guint> m_subIds;

    std::mutex m_uiMutex;
    std::atomic<bool> m_running{false};
    std::thread m_inputThread;

    std::string m_currentState{"NO_MEDIA"};
    double m_speedKmh{0.0};
    std::string m_gear{"P"};
    bool m_isSpeedRestricted{false};
    std::string m_safetyWarning;

    std::vector<VideoTrack> m_playlist;
    int32_t m_currentTrackId{-1};
    std::string m_currentTitle{"None"};
    std::string m_playbackStatus{"STOPPED"};
    int64_t m_currentPosMs{0};
    int64_t m_durationMs{0};
};

} // namespace automotive
