#pragma once

#include <string>
#include <atomic>
#include <gst/gst.h>
#include <gio/gio.h>

namespace automotive {

class GstPlayer {
public:
    GstPlayer();
    ~GstPlayer();

    bool initialize(int argc, char* argv[]);
    void run();
    void stop();

    // Playback control
    bool play(const std::string& filePath);
    bool pause();
    bool resume();
    bool stopPlayback();
    void setSpeedLock(bool locked);

    // GStreamer Message handlers
    void onBusMessage(GstBus* bus, GstMessage* msg);

    // D-Bus reporting
    void reportStatusToMediaManager(const std::string& status, int64_t posMs, int64_t durMs);
    void reportEosToMediaManager();

private:
    void setupDBusClient();
    void queryAndUpdatePosition();

    static void onPlaybackCommandSignal(GDBusConnection* connection,
                                       const gchar* sender_name,
                                       const gchar* object_path,
                                       const gchar* interface_name,
                                       const gchar* signal_name,
                                       GVariant* parameters,
                                       gpointer user_data);

    GMainLoop* m_mainLoop{nullptr};
    GstElement* m_pipeline{nullptr};
    GstBus* m_bus{nullptr};
    guint m_busWatchId{0};
    guint m_timerId{0};

    GDBusConnection* m_dbusConnection{nullptr};
    guint m_signalSubId{0};

    std::string m_currentUri;
    std::string m_playbackState{"STOPPED"};
    std::atomic<bool> m_speedLocked{false};
};

} // namespace automotive
