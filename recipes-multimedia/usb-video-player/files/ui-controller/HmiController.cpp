#include "HmiController.hpp"
#include "../common/Logger.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>

namespace automotive {

static std::string formatTime(int64_t ms) {
    if (ms <= 0) return "00:00";
    int64_t totalSec = ms / 1000;
    int64_t minutes = totalSec / 60;
    int64_t seconds = totalSec % 60;
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << minutes << ":"
        << std::setfill('0') << std::setw(2) << seconds;
    return oss.str();
}

HmiController::HmiController() {
    m_mainLoop = g_main_loop_new(nullptr, FALSE);
}

HmiController::~HmiController() {
    stop();
    if (m_mainLoop) {
        g_main_loop_unref(m_mainLoop);
        m_mainLoop = nullptr;
    }
}

bool HmiController::initialize() {
    GError* error = nullptr;
    m_dbusConnection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (!m_dbusConnection) {
        LOG_W("UI", "System D-Bus unavailable (" + std::string(error->message) + "). Trying Session Bus...");
        g_clear_error(&error);
        m_dbusConnection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
        if (!m_dbusConnection) {
            LOG_E("UI", "Failed to connect to D-Bus: " + std::string(error->message));
            g_clear_error(&error);
            return false;
        }
    }

    subscribeSignals();
    fetchState();
    fetchPlaylist();

    LOG_I("UI", "Automotive UI Controller connected to D-Bus");
    return true;
}

void HmiController::subscribeSignals() {
    const char* signals[] = {
        "StateChanged", "PlaylistChanged", "TrackChanged", 
        "PlaybackStatusChanged", "VehicleSpeedChanged", "SafetyWarning", nullptr
    };

    for (int i = 0; signals[i] != nullptr; ++i) {
        guint subId = g_dbus_connection_signal_subscribe(
            m_dbusConnection,
            DBUS_SERVICE_NAME,
            DBUS_INTERFACE,
            signals[i],
            DBUS_OBJECT_PATH,
            nullptr,
            G_DBUS_SIGNAL_FLAGS_NONE,
            onSignalReceived,
            this,
            nullptr
        );
        m_subIds.push_back(subId);
    }
}

void HmiController::onSignalReceived(GDBusConnection* connection,
                                    const gchar* sender_name,
                                    const gchar* object_path,
                                    const gchar* interface_name,
                                    const gchar* signal_name,
                                    GVariant* parameters,
                                    gpointer user_data) {
    auto* ui = static_cast<HmiController*>(user_data);
    std::string sig(signal_name);

    if (sig == "StateChanged") {
        const gchar* state = nullptr;
        const gchar* reason = nullptr;
        g_variant_get(parameters, "(&s&s)", &state, &reason);
        {
            std::lock_guard<std::mutex> lock(ui->m_uiMutex);
            if (state) ui->m_currentState = state;
        }
        ui->renderDashboard();
    }
    else if (sig == "PlaylistChanged") {
        ui->fetchPlaylist();
        ui->renderDashboard();
    }
    else if (sig == "TrackChanged") {
        gint32 trackId = 0;
        const gchar* title = nullptr;
        const gchar* path = nullptr;
        g_variant_get(parameters, "(i&s&s)", &trackId, &title, &path);
        {
            std::lock_guard<std::mutex> lock(ui->m_uiMutex);
            ui->m_currentTrackId = trackId;
            if (title) ui->m_currentTitle = title;
        }
        ui->renderDashboard();
    }
    else if (sig == "PlaybackStatusChanged") {
        const gchar* status = nullptr;
        gint64 pos = 0, dur = 0;
        g_variant_get(parameters, "(&sxx)", &status, &pos, &dur);
        {
            std::lock_guard<std::mutex> lock(ui->m_uiMutex);
            if (status) ui->m_playbackStatus = status;
            ui->m_currentPosMs = pos;
            ui->m_durationMs = dur;
        }
        // Live progress update can be re-rendered
    }
    else if (sig == "VehicleSpeedChanged") {
        gdouble speed = 0.0;
        gboolean isRestricted = FALSE;
        g_variant_get(parameters, "(db)", &speed, &isRestricted);
        {
            std::lock_guard<std::mutex> lock(ui->m_uiMutex);
            ui->m_speedKmh = speed;
            ui->m_isSpeedRestricted = isRestricted;
        }
        ui->renderDashboard();
    }
    else if (sig == "SafetyWarning") {
        const gchar* msg = nullptr;
        g_variant_get(parameters, "(&s)", &msg);
        {
            std::lock_guard<std::mutex> lock(ui->m_uiMutex);
            if (msg) ui->m_safetyWarning = msg;
        }
        ui->renderDashboard();
    }
}

void HmiController::fetchState() {
    if (!m_dbusConnection) return;
    GError* error = nullptr;
    GVariant* res = g_dbus_connection_call_sync(
        m_dbusConnection,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE,
        "GetState",
        nullptr,
        G_VARIANT_TYPE("(s)"),
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        nullptr,
        &error
    );

    if (res) {
        const gchar* state = nullptr;
        g_variant_get(res, "(&s)", &state);
        if (state) {
            std::lock_guard<std::mutex> lock(m_uiMutex);
            m_currentState = state;
        }
        g_variant_unref(res);
    } else {
        if (error) g_error_free(error);
    }
}

void HmiController::fetchPlaylist() {
    if (!m_dbusConnection) return;
    GError* error = nullptr;
    GVariant* res = g_dbus_connection_call_sync(
        m_dbusConnection,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE,
        "GetPlaylist",
        nullptr,
        G_VARIANT_TYPE("(a(ist))"),
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        nullptr,
        &error
    );

    if (res) {
        GVariantIter* iter = nullptr;
        g_variant_get(res, "(a(ist))", &iter);

        gint32 id = 0;
        gchar* title = nullptr;
        guint64 size = 0;

        std::vector<VideoTrack> list;
        while (g_variant_iter_loop(iter, "(ist)", &id, &title, &size)) {
            VideoTrack t;
            t.id = id;
            t.title = title ? title : "";
            t.fileSizeBytes = size;
            list.push_back(t);
        }
        g_variant_iter_free(iter);
        g_variant_unref(res);

        {
            std::lock_guard<std::mutex> lock(m_uiMutex);
            m_playlist = list;
        }
    } else {
        if (error) g_error_free(error);
    }
}

void HmiController::requestPlay(int32_t trackId) {
    if (!m_dbusConnection) return;
    GError* error = nullptr;
    GVariant* res = g_dbus_connection_call_sync(
        m_dbusConnection,
        DBUS_SERVICE_NAME,
        DBUS_OBJECT_PATH,
        DBUS_INTERFACE,
        "Play",
        g_variant_new("(i)", trackId),
        G_VARIANT_TYPE("(b)"),
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        nullptr,
        &error
    );
    if (res) g_variant_unref(res);
    else if (error) g_error_free(error);
}

void HmiController::requestPause() {
    if (!m_dbusConnection) return;
    GVariant* res = g_dbus_connection_call_sync(
        m_dbusConnection, DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE,
        "Pause", nullptr, G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr
    );
    if (res) g_variant_unref(res);
}

void HmiController::requestResume() {
    if (!m_dbusConnection) return;
    GVariant* res = g_dbus_connection_call_sync(
        m_dbusConnection, DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE,
        "Resume", nullptr, G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr
    );
    if (res) g_variant_unref(res);
}

void HmiController::requestStop() {
    if (!m_dbusConnection) return;
    GVariant* res = g_dbus_connection_call_sync(
        m_dbusConnection, DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE,
        "Stop", nullptr, G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr
    );
    if (res) g_variant_unref(res);
}

void HmiController::requestNext() {
    if (!m_dbusConnection) return;
    GVariant* res = g_dbus_connection_call_sync(
        m_dbusConnection, DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE,
        "Next", nullptr, G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr
    );
    if (res) g_variant_unref(res);
}

void HmiController::requestPrev() {
    if (!m_dbusConnection) return;
    GVariant* res = g_dbus_connection_call_sync(
        m_dbusConnection, DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE,
        "Previous", nullptr, G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr
    );
    if (res) g_variant_unref(res);
}

void HmiController::requestSimulateUsb(const std::string& action, const std::string& path) {
    if (!m_dbusConnection) return;
    GVariant* res = g_dbus_connection_call_sync(
        m_dbusConnection, DBUS_SERVICE_NAME, DBUS_OBJECT_PATH, DBUS_INTERFACE,
        "SimulateUsb", g_variant_new("(ss)", action.c_str(), path.c_str()),
        G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr
    );
    if (res) g_variant_unref(res);
}

void HmiController::renderDashboard() {
    std::lock_guard<std::mutex> lock(m_uiMutex);

    std::cout << "\033[2J\033[H"; // Clear screen & move to top
    std::cout << "\033[1;37;44m================================================================================\033[0m\n";
    std::cout << "\033[1;37;44m                  AUTOMOTIVE INFOTAINMENT (IVI) - USB VIDEO PLAYER             \033[0m\n";
    std::cout << "\033[1;37;44m================================================================================\033[0m\n";

    // FSM State & Telematics
    std::string stateColor = "\033[1;32m";
    if (m_currentState == "NO_MEDIA") stateColor = "\033[1;30m";
    else if (m_currentState == "SCANNING") stateColor = "\033[1;33m";
    else if (m_currentState == "SPEED_RESTRICTED") stateColor = "\033[1;31;5m";

    std::cout << "  FSM State: " << stateColor << "[" << m_currentState << "]\033[0m";
    std::cout << "   |   CAN Speed: \033[1;36m" << std::fixed << std::setprecision(1) << m_speedKmh << " km/h\033[0m";
    std::cout << "   |   Gear: \033[1;36m" << m_gear << "\033[0m\n";

    // Safety Lockout Alert Banner
    if (m_isSpeedRestricted || m_currentState == "SPEED_RESTRICTED") {
        std::cout << "\n\033[1;37;41m  >>> [SAFETY LOCKOUT ACTIVE] VIDEO IN MOTION PROHIBITED (ISO 26262 / NHTSA) <<< \033[0m\n";
    }

    if (!m_safetyWarning.empty()) {
        std::cout << "\033[1;33m  Alert: " << m_safetyWarning << "\033[0m\n";
    }

    std::cout << "--------------------------------------------------------------------------------\n";
    std::cout << "  Playback: [\033[1;35m" << m_playbackStatus << "\033[0m] "
              << "Track " << m_currentTrackId << ": \033[1m" << m_currentTitle << "\033[0m\n";
    std::cout << "  Time: [" << formatTime(m_currentPosMs) << " / " << formatTime(m_durationMs) << "]\n";
    std::cout << "--------------------------------------------------------------------------------\n";

    std::cout << "  USB PLAYLIST (" << m_playlist.size() << " valid .mp4 / .mkv files):\n";
    if (m_playlist.empty()) {
        std::cout << "    (No USB media detected or no valid .mp4/.mkv files found)\n";
    } else {
        for (const auto& track : m_playlist) {
            std::string marker = (track.id == m_currentTrackId) ? "\033[1;32m >> \033[0m" : "    ";
            std::cout << marker << "[" << track.id << "] " << track.title 
                      << " (" << (track.fileSizeBytes / (1024 * 1024)) << " MB)\n";
        }
    }

    std::cout << "================================================================================\n";
    std::cout << "  Commands: [0..N] Play Track  |  [p] Pause  |  [r] Resume  |  [s] Stop\n";
    std::cout << "            [n] Next  |  [b] Prev  |  [u] Plug-in USB Mock  |  [x] Unplug USB  |  [q] Quit\n";
    std::cout << "  Command> " << std::flush;
}

void HmiController::inputLoop() {
    renderDashboard();

    std::string line;
    while (m_running && std::getline(std::cin, line)) {
        if (line.empty()) continue;
        if (line == "q" || line == "quit") {
            stop();
            break;
        }

        if (line == "p") {
            requestPause();
        } else if (line == "r") {
            requestResume();
        } else if (line == "s") {
            requestStop();
        } else if (line == "n") {
            requestNext();
        } else if (line == "b") {
            requestPrev();
        } else if (line == "u") {
            requestSimulateUsb("add", "/tmp/usb_video_sample");
        } else if (line == "x") {
            requestSimulateUsb("remove", "");
        } else {
            // Check if user entered a number for track ID
            try {
                int32_t trackId = std::stoi(line);
                requestPlay(trackId);
            } catch (...) {
                // Ignore invalid input
            }
        }
        renderDashboard();
    }
}

void HmiController::run() {
    m_running = true;
    m_inputThread = std::thread(&HmiController::inputLoop, this);
    if (m_mainLoop) {
        g_main_loop_run(m_mainLoop);
    }
}

void HmiController::stop() {
    m_running = false;
    for (guint id : m_subIds) {
        if (id > 0 && m_dbusConnection) {
            g_dbus_connection_signal_unsubscribe(m_dbusConnection, id);
        }
    }
    m_subIds.clear();

    if (m_inputThread.joinable()) {
        m_inputThread.detach(); // Allow prompt thread to exit
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
