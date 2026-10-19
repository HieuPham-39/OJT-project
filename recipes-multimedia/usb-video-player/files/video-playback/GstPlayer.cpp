#include "GstPlayer.hpp"
#include "../common/DBusCommon.hpp"
#include "../common/Logger.hpp"

#include <iostream>

namespace automotive {

static gboolean busCallback(GstBus* bus, GstMessage* msg, gpointer user_data) {
    auto* player = static_cast<GstPlayer*>(user_data);
    player->onBusMessage(bus, msg);
    return TRUE;
}

static gboolean positionTimerCallback(gpointer user_data) {
    auto* player = static_cast<GstPlayer*>(user_data);
    player->reportStatusToMediaManager("", 0, 0); // Trigger position query and update
    return TRUE;
}

GstPlayer::GstPlayer() = default;

GstPlayer::~GstPlayer() {
    stop();
}

bool GstPlayer::initialize(int argc, char* argv[]) {
    // 1. Initialize GStreamer
    GError* gstError = nullptr;
    if (!gst_init_check(&argc, &argv, &gstError)) {
        LOG_E("Player", "Failed to initialize GStreamer: " + std::string(gstError ? gstError->message : "unknown"));
        if (gstError) g_error_free(gstError);
        return false;
    }

    m_mainLoop = g_main_loop_new(nullptr, FALSE);

    // 2. Create GStreamer playbin pipeline
    m_pipeline = gst_element_factory_make("playbin", "automotive-playbin");
    if (!m_pipeline) {
        LOG_E("Player", "Failed to create GStreamer playbin element");
        return false;
    }

    // Connect bus
    m_bus = gst_element_get_bus(m_pipeline);
    m_busWatchId = gst_bus_add_watch(m_bus, busCallback, this);

    // 3. Connect to D-Bus
    setupDBusClient();

    // 4. Setup periodic position timer (every 500ms)
    m_timerId = g_timeout_add(500, positionTimerCallback, this);

    LOG_I("Player", "GStreamer Playback Engine successfully initialized");
    return true;
}

void GstPlayer::setupDBusClient() {
    GError* error = nullptr;
    m_dbusConnection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (!m_dbusConnection) {
        LOG_W("Player", "System D-Bus unavailable (" + std::string(error->message) + "). Trying Session Bus...");
        g_clear_error(&error);
        m_dbusConnection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
        if (!m_dbusConnection) {
            LOG_E("Player", "Failed to connect to D-Bus: " + std::string(error->message));
            g_clear_error(&error);
            return;
        }
    }

    // Subscribe to PlaybackCommand signals from MediaManager
    m_signalSubId = g_dbus_connection_signal_subscribe(
        m_dbusConnection,
        DBUS_SERVICE_NAME,
        DBUS_INTERFACE,
        "PlaybackCommand",
        DBUS_OBJECT_PATH,
        nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE,
        onPlaybackCommandSignal,
        this,
        nullptr
    );

    LOG_I("Player", "Subscribed to MediaManager PlaybackCommand signals on D-Bus");
}

void GstPlayer::onPlaybackCommandSignal(GDBusConnection* connection,
                                       const gchar* sender_name,
                                       const gchar* object_path,
                                       const gchar* interface_name,
                                       const gchar* signal_name,
                                       GVariant* parameters,
                                       gpointer user_data) {
    auto* player = static_cast<GstPlayer*>(user_data);

    const gchar* command = nullptr;
    const gchar* arg = nullptr;
    g_variant_get(parameters, "(&s&s)", &command, &arg);

    if (!command) return;
    std::string cmd(command);
    std::string argument(arg ? arg : "");

    LOG_I("Player", "Received Playback Command: " + cmd + " (" + argument + ")");

    if (cmd == "PLAY") {
        player->play(argument);
    } else if (cmd == "PAUSE") {
        player->pause();
    } else if (cmd == "RESUME") {
        player->resume();
    } else if (cmd == "STOP") {
        player->stopPlayback();
    } else if (cmd == "SPEED_LOCK") {
        player->setSpeedLock(true);
    }
}

bool GstPlayer::play(const std::string& filePath) {
    if (m_speedLocked) {
        LOG_S("Player", "Play inhibited by Speed Lock!");
        return false;
    }

    if (!m_pipeline) return false;

    // Convert local filepath to URI
    gchar* uri = nullptr;
    if (filePath.find("://") == std::string::npos) {
        uri = gst_filename_to_uri(filePath.c_str(), nullptr);
    } else {
        uri = g_strdup(filePath.c_str());
    }

    if (!uri) {
        LOG_E("Player", "Invalid file path for playback: " + filePath);
        return false;
    }

    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    g_object_set(m_pipeline, "uri", uri, nullptr);
    g_free(uri);

    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        LOG_E("Player", "Failed to transition GStreamer pipeline to PLAYING");
        return false;
    }

    m_playbackState = "PLAYING";
    m_speedLocked = false;
    LOG_I("Player", "Playback started for: " + filePath);
    reportStatusToMediaManager("PLAYING", 0, 0);
    return true;
}

bool GstPlayer::pause() {
    if (!m_pipeline) return false;
    gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
    m_playbackState = "PAUSED";
    LOG_I("Player", "Playback paused");
    reportStatusToMediaManager("PAUSED", 0, 0);
    return true;
}

bool GstPlayer::resume() {
    if (m_speedLocked) {
        LOG_S("Player", "Cannot resume: Speed lock is active!");
        return false;
    }
    if (!m_pipeline) return false;
    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    m_playbackState = "PLAYING";
    LOG_I("Player", "Playback resumed");
    reportStatusToMediaManager("PLAYING", 0, 0);
    return true;
}

bool GstPlayer::stopPlayback() {
    if (!m_pipeline) return false;
    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    m_playbackState = "STOPPED";
    LOG_I("Player", "Playback stopped");
    reportStatusToMediaManager("STOPPED", 0, 0);
    return true;
}

void GstPlayer::setSpeedLock(bool locked) {
    m_speedLocked = locked;
    if (locked) {
        LOG_S("Player", "Enforcing Speed Lock! Pausing video rendering pipeline.");
        pause();
    }
}

void GstPlayer::onBusMessage(GstBus* bus, GstMessage* msg) {
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            LOG_I("Player", "End of Stream reached");
            m_playbackState = "STOPPED";
            reportEosToMediaManager();
            break;
        case GST_MESSAGE_ERROR: {
            GError* err = nullptr;
            gchar* dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);
            LOG_E("Player", "GStreamer Pipeline Error: " + std::string(err ? err->message : "unknown"));
            if (dbg) {
                LOG_D("Player", "Debug details: " + std::string(dbg));
                g_free(dbg);
            }
            if (err) g_error_free(err);
            stopPlayback();
            break;
        }
        case GST_MESSAGE_STATE_CHANGED: {
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(m_pipeline)) {
                GstState oldState, newState, pending;
                gst_message_parse_state_changed(msg, &oldState, &newState, &pending);
                LOG_D("Player", "Pipeline state changed: " + 
                                std::string(gst_element_state_get_name(oldState)) + " -> " + 
                                std::string(gst_element_state_get_name(newState)));
            }
            break;
        }
        default:
            break;
    }
}

void GstPlayer::reportStatusToMediaManager(const std::string& status, int64_t posMs, int64_t durMs) {
    if (!m_dbusConnection || !m_pipeline) return;

    std::string currentStatus = status.empty() ? m_playbackState : status;

    // Query current position and duration from GStreamer pipeline
    gint64 pos = 0, dur = 0;
    if (gst_element_query_position(m_pipeline, GST_FORMAT_TIME, &pos)) {
        posMs = pos / GST_MSECOND;
    }
    if (gst_element_query_duration(m_pipeline, GST_FORMAT_TIME, &dur)) {
        durMs = dur / GST_MSECOND;
    }

    GError* error = nullptr;
    GVariant* res = g_dbus_connection_call_sync(
        m_dbusConnection,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE,
        "ReportPlaybackStatus",
        g_variant_new("(sxx)", currentStatus.c_str(), posMs, durMs),
        G_VARIANT_TYPE("(b)"),
        G_DBUS_CALL_FLAGS_NONE,
        500,
        nullptr,
        &error
    );

    if (res) {
        g_variant_unref(res);
    } else {
        if (error) g_error_free(error);
    }
}

void GstPlayer::reportEosToMediaManager() {
    if (!m_dbusConnection) return;

    GError* error = nullptr;
    GVariant* res = g_dbus_connection_call_sync(
        m_dbusConnection,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE,
        "ReportEndOfStream",
        nullptr,
        G_VARIANT_TYPE("(b)"),
        G_DBUS_CALL_FLAGS_NONE,
        500,
        nullptr,
        &error
    );

    if (res) {
        g_variant_unref(res);
    } else {
        if (error) g_error_free(error);
    }
}

void GstPlayer::run() {
    if (m_mainLoop) {
        LOG_I("Player", "GStreamer engine running main loop");
        g_main_loop_run(m_mainLoop);
    }
}

void GstPlayer::stop() {
    if (m_timerId > 0) {
        g_source_remove(m_timerId);
        m_timerId = 0;
    }
    if (m_busWatchId > 0) {
        g_source_remove(m_busWatchId);
        m_busWatchId = 0;
    }
    if (m_signalSubId > 0 && m_dbusConnection) {
        g_dbus_connection_signal_unsubscribe(m_dbusConnection, m_signalSubId);
        m_signalSubId = 0;
    }
    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }
    if (m_bus) {
        gst_object_unref(m_bus);
        m_bus = nullptr;
    }
    if (m_dbusConnection) {
        g_object_unref(m_dbusConnection);
        m_dbusConnection = nullptr;
    }
    if (m_mainLoop && g_main_loop_is_running(m_mainLoop)) {
        g_main_loop_quit(m_mainLoop);
    }
}

} // namespace automotive
