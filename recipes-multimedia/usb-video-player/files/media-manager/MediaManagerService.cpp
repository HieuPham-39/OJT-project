#include "MediaManagerService.hpp"
#include "../common/Logger.hpp"

#include <cstring>

namespace automotive {

static const GDBusInterfaceVTable interfaceVTable = {
    [](GDBusConnection* conn, const gchar* sender, const gchar* path,
       const gchar* iface, const gchar* method, GVariant* params,
       GDBusMethodInvocation* inv, gpointer user_data) {
        auto* service = static_cast<MediaManagerService*>(user_data);
        service->handleMethodCall(conn, sender, path, iface, method, params, inv);
    },
    nullptr, // get_property
    nullptr, // set_property
    {nullptr}
};

MediaManagerService::MediaManagerService() {
    m_mainLoop = g_main_loop_new(nullptr, FALSE);
    m_introspectionData = g_dbus_node_info_new_for_xml(INTROSPECTION_XML, nullptr);
}

MediaManagerService::~MediaManagerService() {
    stop();
    if (m_introspectionData) {
        g_dbus_node_info_unref(m_introspectionData);
        m_introspectionData = nullptr;
    }
    if (m_mainLoop) {
        g_main_loop_unref(m_mainLoop);
        m_mainLoop = nullptr;
    }
}

bool MediaManagerService::initialize() {
    // 1. Setup State Machine Callback
    m_fsm.registerStateChangeCallback([this](State oldState, State newState, const std::string& reason) {
        emitStateChanged(stateToString(newState), reason);

        if (newState == State::SPEED_RESTRICTED) {
            emitPlaybackCommand("SPEED_LOCK", "");
            emitSafetyWarning("AUTOMOTIVE SAFETY WARNING: Video display restricted while vehicle in motion!");
        } else if (newState == State::PAUSED && oldState == State::SPEED_RESTRICTED) {
            emitSafetyWarning("Vehicle stationary. Video playback may be resumed.");
        } else if (newState == State::NO_MEDIA) {
            emitPlaybackCommand("STOP", "");
            m_scanner.clear();
            emitPlaylistChanged(0);
        }
    });

    // 2. Setup UsbMonitor Callbacks
    m_usbMonitor.setInsertedCallback([this](const std::string& devNode, const std::string& mountPoint) {
        handleUsbInserted(devNode, mountPoint);
    });

    m_usbMonitor.setRemovedCallback([this](const std::string& devNode) {
        handleUsbRemoved(devNode);
    });

    // 3. Initialize OS Hardware Event Listener (libudev netlink)
    if (!m_usbMonitor.initialize()) {
        LOG_W("MediaManager", "Hardware udev initialization warning - simulation still active");
    }

    // 4. Connect to D-Bus System Bus (fallback to Session bus for local dev)
    GBusType busType = G_BUS_TYPE_SYSTEM;
    GError* error = nullptr;
    m_connection = g_bus_get_sync(busType, nullptr, &error);
    if (!m_connection) {
        LOG_W("MediaManager", "System D-Bus unavailable (" + std::string(error->message) + "). Falling back to Session Bus.");
        g_clear_error(&error);
        busType = G_BUS_TYPE_SESSION;
        m_connection = g_bus_get_sync(busType, nullptr, &error);
        if (!m_connection) {
            LOG_E("MediaManager", "Failed to connect to D-Bus: " + std::string(error->message));
            g_clear_error(&error);
            return false;
        }
    }

    m_ownerId = g_bus_own_name_on_connection(
        m_connection,
        DBUS_SERVICE_NAME,
        G_BUS_NAME_OWNER_FLAGS_REPLACE,
        onNameAcquired,
        onNameLost,
        this,
        nullptr
    );

    registerDBusInterface(m_connection);
    m_usbMonitor.start();

    LOG_I("MediaManager", "Automotive Media Manager Service initialized successfully on D-Bus: " + std::string(DBUS_SERVICE_NAME));
    return true;
}

void MediaManagerService::registerDBusInterface(GDBusConnection* connection) {
    if (!m_introspectionData || !m_introspectionData->interfaces[0]) {
        LOG_E("MediaManager", "Introspection data missing interface definition");
        return;
    }

    GError* error = nullptr;
    m_registrationId = g_dbus_connection_register_object(
        connection,
        DBUS_OBJECT_PATH,
        m_introspectionData->interfaces[0],
        &interfaceVTable,
        this,
        nullptr,
        &error
    );

    if (m_registrationId == 0) {
        LOG_E("MediaManager", "Failed to register D-Bus object: " + std::string(error ? error->message : "unknown"));
        if (error) g_error_free(error);
    } else {
        LOG_I("MediaManager", "Registered D-Bus Object at path: " + std::string(DBUS_OBJECT_PATH));
    }
}

void MediaManagerService::onBusAcquired(GDBusConnection* connection, const gchar* name, gpointer user_data) {
    LOG_I("MediaManager", "D-Bus bus acquired: " + std::string(name));
}

void MediaManagerService::onNameAcquired(GDBusConnection* connection, const gchar* name, gpointer user_data) {
    LOG_I("MediaManager", "D-Bus well-known name successfully owned: " + std::string(name));
}

void MediaManagerService::onNameLost(GDBusConnection* connection, const gchar* name, gpointer user_data) {
    LOG_W("MediaManager", "D-Bus well-known name lost or unacquired: " + std::string(name ? name : "unknown"));
}

void MediaManagerService::run() {
    if (m_mainLoop) {
        LOG_I("MediaManager", "Entering Automotive Main Loop...");
        g_main_loop_run(m_mainLoop);
    }
}

void MediaManagerService::stop() {
    m_usbMonitor.stop();
    if (m_ownerId > 0) {
        g_bus_unown_name(m_ownerId);
        m_ownerId = 0;
    }
    if (m_registrationId > 0 && m_connection) {
        g_dbus_connection_unregister_object(m_connection, m_registrationId);
        m_registrationId = 0;
    }
    if (m_mainLoop && g_main_loop_is_running(m_mainLoop)) {
        g_main_loop_quit(m_mainLoop);
    }
}

void MediaManagerService::handleUsbInserted(const std::string& devNode, const std::string& mountPoint) {
    LOG_I("MediaManager", "USB Plugged In: " + devNode + " at " + mountPoint);
    m_fsm.handleEvent(Event::EV_USB_INSERTED, devNode);

    if (mountPoint.empty()) {
        LOG_W("MediaManager", "Mount point not ready yet for " + devNode);
        return;
    }

    m_fsm.handleEvent(Event::EV_SCAN_STARTED, mountPoint);

    // Scan for valid video files (.mp4, .mkv)
    m_scanner.scanDirectoryAsync(mountPoint, [this](const std::vector<VideoTrack>& playlist) {
        if (!playlist.empty()) {
            m_fsm.handleEvent(Event::EV_SCAN_COMPLETED, "Found " + std::to_string(playlist.size()) + " files");
            emitPlaylistChanged(static_cast<int32_t>(playlist.size()));
        } else {
            LOG_W("MediaManager", "No valid .mp4 or .mkv files found on USB drive");
            m_fsm.handleEvent(Event::EV_SCAN_EMPTY, "Zero valid files");
            emitPlaylistChanged(0);
        }
    });
}

void MediaManagerService::handleUsbRemoved(const std::string& devNode) {
    LOG_W("MediaManager", "USB Unplugged: " + devNode);
    m_fsm.handleEvent(Event::EV_USB_REMOVED, devNode);
}

void MediaManagerService::handleMethodCall(GDBusConnection* connection,
                                          const gchar* sender,
                                          const gchar* object_path,
                                          const gchar* interface_name,
                                          const gchar* method_name,
                                          GVariant* parameters,
                                          GDBusMethodInvocation* invocation) {
    std::string method(method_name);
    LOG_D("MediaManager", "D-Bus Method Invocation: " + method);

    if (method == "GetState") {
        std::string state = m_fsm.getCurrentStateString();
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", state.c_str()));
    }
    else if (method == "GetPlaylist") {
        auto list = m_scanner.getPlaylist();
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ist)"));

        for (const auto& track : list) {
            g_variant_builder_add(&builder, "(ist)", track.id, track.title.c_str(), track.fileSizeBytes);
        }

        g_dbus_method_invocation_return_value(invocation, g_variant_new("(a(ist))", &builder));
    }
    else if (method == "GetCurrentTrack") {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        g_dbus_method_invocation_return_value(invocation, 
            g_variant_new("(iss)", m_currentTrack.id, m_currentTrack.title.c_str(), m_currentTrack.filePath.c_str()));
    }
    else if (method == "Play") {
        gint32 trackId = 0;
        g_variant_get(parameters, "(i)", &trackId);

        VideoTrack track;
        if (!m_scanner.getTrackById(trackId, track)) {
            LOG_E("MediaManager", "Track ID not found: " + std::to_string(trackId));
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
            return;
        }

        // Check FSM guard
        if (m_fsm.getVehicleSpeed() > 0.0 || (m_fsm.getVehicleGear() != "P" && m_fsm.getVehicleGear() != "PARK")) {
            LOG_S("MediaManager", "Play rejected: Vehicle in motion or not in Park");
            m_fsm.handleEvent(Event::EV_PLAY_REQUEST, "Play requested while moving");
            emitSafetyWarning("CANNOT PLAY: Vehicle is in motion or not in Park!");
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
            return;
        }

        if (m_fsm.handleEvent(Event::EV_PLAY_REQUEST, "User initiated play")) {
            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                m_currentTrackIndex = track.id;
                m_currentTrack = track;
            }
            emitTrackChanged(track.id, track.title, track.filePath);
            emitPlaybackCommand("PLAY", track.filePath);
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
        } else {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
        }
    }
    else if (method == "Pause") {
        if (m_fsm.handleEvent(Event::EV_PAUSE_REQUEST, "User paused")) {
            emitPlaybackCommand("PAUSE", "");
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
        } else {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
        }
    }
    else if (method == "Resume") {
        if (m_fsm.getVehicleSpeed() > 0.0) {
            emitSafetyWarning("CANNOT RESUME: Vehicle speed > 0 km/h!");
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
            return;
        }

        if (m_fsm.handleEvent(Event::EV_RESUME_REQUEST, "User resumed")) {
            emitPlaybackCommand("RESUME", "");
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
        } else {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
        }
    }
    else if (method == "Stop") {
        if (m_fsm.handleEvent(Event::EV_STOP_REQUEST, "User stopped")) {
            emitPlaybackCommand("STOP", "");
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
        } else {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
        }
    }
    else if (method == "Next") {
        auto list = m_scanner.getPlaylist();
        if (list.empty()) {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
            return;
        }
        int32_t nextId = (m_currentTrackIndex + 1) % static_cast<int32_t>(list.size());
        VideoTrack track = list[nextId];
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_currentTrackIndex = nextId;
            m_currentTrack = track;
        }
        m_fsm.handleEvent(Event::EV_PLAY_REQUEST, "Next track");
        emitTrackChanged(track.id, track.title, track.filePath);
        emitPlaybackCommand("PLAY", track.filePath);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
    }
    else if (method == "Previous") {
        auto list = m_scanner.getPlaylist();
        if (list.empty()) {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
            return;
        }
        int32_t prevId = (m_currentTrackIndex - 1 + static_cast<int32_t>(list.size())) % static_cast<int32_t>(list.size());
        VideoTrack track = list[prevId];
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_currentTrackIndex = prevId;
            m_currentTrack = track;
        }
        m_fsm.handleEvent(Event::EV_PLAY_REQUEST, "Prev track");
        emitTrackChanged(track.id, track.title, track.filePath);
        emitPlaybackCommand("PLAY", track.filePath);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
    }
    else if (method == "UpdateVehicleSpeed") {
        gdouble speed = 0.0;
        g_variant_get(parameters, "(d)", &speed);
        m_fsm.setVehicleSpeed(speed);
        emitVehicleSpeedChanged(speed, m_fsm.isSpeedRestricted());
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
    }
    else if (method == "UpdateGear") {
        const gchar* gearStr = nullptr;
        g_variant_get(parameters, "(&s)", &gearStr);
        if (gearStr) {
            m_fsm.setVehicleGear(gearStr);
        }
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
    }
    else if (method == "ReportPlaybackStatus") {
        const gchar* status = nullptr;
        gint64 pos = 0, dur = 0;
        g_variant_get(parameters, "(&sxx)", &status, &pos, &dur);
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            if (status) m_lastPlaybackStatus = status;
            m_lastPositionMs = pos;
            m_lastDurationMs = dur;
        }
        emitPlaybackStatusChanged(status ? status : "", pos, dur);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
    }
    else if (method == "ReportEndOfStream") {
        m_fsm.handleEvent(Event::EV_PLAYBACK_EOS, "Playback finished");
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
    }
    else if (method == "SimulateUsb") {
        const gchar* action = nullptr;
        const gchar* path = nullptr;
        g_variant_get(parameters, "(&s&s)", &action, &path);
        if (action && std::string(action) == "add") {
            m_usbMonitor.simulateInsert(path ? path : "/tmp/usb_test");
        } else if (action && std::string(action) == "remove") {
            m_usbMonitor.simulateRemove();
        }
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
    }
    else {
        LOG_W("MediaManager", "Unknown D-Bus method invoked: " + method);
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                              "Unknown method: %s", method.c_str());
    }
}

void MediaManagerService::emitStateChanged(const std::string& newState, const std::string& reason) {
    if (!m_connection) return;
    GError* error = nullptr;
    g_dbus_connection_emit_signal(
        m_connection,
        nullptr,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE,
        "StateChanged",
        g_variant_new("(ss)", newState.c_str(), reason.c_str()),
        &error
    );
    if (error) {
        LOG_W("MediaManager", "Failed to emit StateChanged signal: " + std::string(error->message));
        g_error_free(error);
    }
}

void MediaManagerService::emitPlaylistChanged(int32_t count) {
    if (!m_connection) return;
    g_dbus_connection_emit_signal(
        m_connection,
        nullptr,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE,
        "PlaylistChanged",
        g_variant_new("(i)", count),
        nullptr
    );
}

void MediaManagerService::emitTrackChanged(int32_t trackId, const std::string& title, const std::string& path) {
    if (!m_connection) return;
    g_dbus_connection_emit_signal(
        m_connection,
        nullptr,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE,
        "TrackChanged",
        g_variant_new("(iss)", trackId, title.c_str(), path.c_str()),
        nullptr
    );
}

void MediaManagerService::emitPlaybackCommand(const std::string& command, const std::string& arg) {
    if (!m_connection) return;
    LOG_I("MediaManager", "Broadcasting PlaybackCommand signal: " + command + " (" + arg + ")");
    g_dbus_connection_emit_signal(
        m_connection,
        nullptr,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE,
        "PlaybackCommand",
        g_variant_new("(ss)", command.c_str(), arg.c_str()),
        nullptr
    );
}

void MediaManagerService::emitPlaybackStatusChanged(const std::string& status, int64_t posMs, int64_t durMs) {
    if (!m_connection) return;
    g_dbus_connection_emit_signal(
        m_connection,
        nullptr,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE,
        "PlaybackStatusChanged",
        g_variant_new("(sxx)", status.c_str(), posMs, durMs),
        nullptr
    );
}

void MediaManagerService::emitVehicleSpeedChanged(double speedKmh, bool isRestricted) {
    if (!m_connection) return;
    g_dbus_connection_emit_signal(
        m_connection,
        nullptr,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE,
        "VehicleSpeedChanged",
        g_variant_new("(db)", speedKmh, isRestricted ? TRUE : FALSE),
        nullptr
    );
}

void MediaManagerService::emitSafetyWarning(const std::string& message) {
    if (!m_connection) return;
    LOG_S("MediaManager", "Broadcasting SafetyWarning signal: " + message);
    g_dbus_connection_emit_signal(
        m_connection,
        nullptr,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE,
        "SafetyWarning",
        g_variant_new("(s)", message.c_str()),
        nullptr
    );
}

} // namespace automotive
