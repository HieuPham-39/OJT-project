#include "media_manager_service.h"
#include "fsm.h"
#include "usb_monitor.h"
#include "playlist_scanner.h"
#include "logger.h"

#include <gio/gio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#define DBUS_SERVICE_NAME "org.automotive.MediaManager"
#define DBUS_OBJECT_PATH  "/org/automotive/MediaManager"
#define DBUS_INTERFACE    "org.automotive.MediaManager1"

static const char *s_introspection_xml =
"<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
"\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
"<node>\n"
"  <interface name=\"org.automotive.MediaManager1\">\n"
"    <method name=\"GetState\">\n"
"      <arg name=\"state\" type=\"s\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"GetPlaylist\">\n"
"      <arg name=\"playlist\" type=\"a(ist)\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"GetCurrentTrack\">\n"
"      <arg name=\"track_id\" type=\"i\" direction=\"out\"/>\n"
"      <arg name=\"title\" type=\"s\" direction=\"out\"/>\n"
"      <arg name=\"path\" type=\"s\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"Play\">\n"
"      <arg name=\"track_id\" type=\"i\" direction=\"in\"/>\n"
"      <arg name=\"success\" type=\"b\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"Pause\">\n"
"      <arg name=\"success\" type=\"b\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"Resume\">\n"
"      <arg name=\"success\" type=\"b\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"Stop\">\n"
"      <arg name=\"success\" type=\"b\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"Next\">\n"
"      <arg name=\"success\" type=\"b\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"Previous\">\n"
"      <arg name=\"success\" type=\"b\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"UpdateVehicleSpeed\">\n"
"      <arg name=\"speed_kmh\" type=\"d\" direction=\"in\"/>\n"
"      <arg name=\"acknowledged\" type=\"b\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"UpdateGear\">\n"
"      <arg name=\"gear\" type=\"s\" direction=\"in\"/>\n"
"      <arg name=\"acknowledged\" type=\"b\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"ReportPlaybackStatus\">\n"
"      <arg name=\"status\" type=\"s\" direction=\"in\"/>\n"
"      <arg name=\"position_ms\" type=\"x\" direction=\"in\"/>\n"
"      <arg name=\"duration_ms\" type=\"x\" direction=\"in\"/>\n"
"      <arg name=\"acknowledged\" type=\"b\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"ReportEndOfStream\">\n"
"      <arg name=\"acknowledged\" type=\"b\" direction=\"out\"/>\n"
"    </method>\n"
"    <method name=\"SimulateUsb\">\n"
"      <arg name=\"action\" type=\"s\" direction=\"in\"/>\n"
"      <arg name=\"path\" type=\"s\" direction=\"in\"/>\n"
"      <arg name=\"success\" type=\"b\" direction=\"out\"/>\n"
"    </method>\n"
"    <signal name=\"StateChanged\">\n"
"      <arg name=\"new_state\" type=\"s\"/>\n"
"      <arg name=\"reason\" type=\"s\"/>\n"
"    </signal>\n"
"    <signal name=\"PlaylistChanged\">\n"
"      <arg name=\"count\" type=\"i\"/>\n"
"    </signal>\n"
"    <signal name=\"TrackChanged\">\n"
"      <arg name=\"track_id\" type=\"i\"/>\n"
"      <arg name=\"title\" type=\"s\"/>\n"
"      <arg name=\"path\" type=\"s\"/>\n"
"    </signal>\n"
"    <signal name=\"PlaybackCommand\">\n"
"      <arg name=\"command\" type=\"s\"/>\n"
"      <arg name=\"arg\" type=\"s\"/>\n"
"    </signal>\n"
"    <signal name=\"PlaybackStatusChanged\">\n"
"      <arg name=\"status\" type=\"s\"/>\n"
"      <arg name=\"position_ms\" type=\"x\"/>\n"
"      <arg name=\"duration_ms\" type=\"x\"/>\n"
"    </signal>\n"
"    <signal name=\"VehicleSpeedChanged\">\n"
"      <arg name=\"speed_kmh\" type=\"d\"/>\n"
"      <arg name=\"is_restricted\" type=\"b\"/>\n"
"    </signal>\n"
"    <signal name=\"SafetyWarning\">\n"
"      <arg name=\"warning_message\" type=\"s\"/>\n"
"    </signal>\n"
"  </interface>\n"
"</node>";

struct MediaManagerService {
    Fsm_t fsm;
    UsbMonitor_t *usb_monitor;
    PlaylistScanner_t *scanner;

    GMainLoop *main_loop;
    GDBusConnection *connection;
    guint owner_id;
    guint registration_id;
    GDBusNodeInfo *introspection_data;

    int32_t current_track_id;
    VideoTrack_t current_track;
    char last_playback_status[32];
    int64_t last_position_ms;
    int64_t last_duration_ms;
    pthread_mutex_t state_lock;
};

// Signal emitters
static void emit_state_changed(MediaManagerService_t *svc, const char *new_state, const char *reason) {
    if (!svc || !svc->connection) return;
    g_dbus_connection_emit_signal(
        svc->connection, NULL, DBUS_OBJECT_PATH, DBUS_INTERFACE,
        "StateChanged", g_variant_new("(ss)", new_state, reason ? reason : ""), NULL
    );
}

static void emit_playlist_changed(MediaManagerService_t *svc, int32_t count) {
    if (!svc || !svc->connection) return;
    g_dbus_connection_emit_signal(
        svc->connection, NULL, DBUS_OBJECT_PATH, DBUS_INTERFACE,
        "PlaylistChanged", g_variant_new("(i)", count), NULL
    );
}

static void emit_track_changed(MediaManagerService_t *svc, int32_t track_id, const char *title, const char *path) {
    if (!svc || !svc->connection) return;
    g_dbus_connection_emit_signal(
        svc->connection, NULL, DBUS_OBJECT_PATH, DBUS_INTERFACE,
        "TrackChanged", g_variant_new("(iss)", track_id, title ? title : "", path ? path : ""), NULL
    );
}

static void emit_playback_command(MediaManagerService_t *svc, const char *cmd, const char *arg) {
    if (!svc || !svc->connection) return;
    LOG_I("MediaMgr-C", "Emitting PlaybackCommand: %s (%s)", cmd, arg ? arg : "");
    g_dbus_connection_emit_signal(
        svc->connection, NULL, DBUS_OBJECT_PATH, DBUS_INTERFACE,
        "PlaybackCommand", g_variant_new("(ss)", cmd, arg ? arg : ""), NULL
    );
}

static void emit_playback_status_changed(MediaManagerService_t *svc, const char *status, int64_t pos_ms, int64_t dur_ms) {
    if (!svc || !svc->connection) return;
    g_dbus_connection_emit_signal(
        svc->connection, NULL, DBUS_OBJECT_PATH, DBUS_INTERFACE,
        "PlaybackStatusChanged", g_variant_new("(sxx)", status ? status : "", pos_ms, dur_ms), NULL
    );
}

static void emit_vehicle_speed_changed(MediaManagerService_t *svc, double speed_kmh, bool is_restricted) {
    if (!svc || !svc->connection) return;
    g_dbus_connection_emit_signal(
        svc->connection, NULL, DBUS_OBJECT_PATH, DBUS_INTERFACE,
        "VehicleSpeedChanged", g_variant_new("(db)", speed_kmh, is_restricted ? TRUE : FALSE), NULL
    );
}

static void emit_safety_warning(MediaManagerService_t *svc, const char *msg) {
    if (!svc || !svc->connection) return;
    LOG_S("MediaMgr-C", "Safety Alert Broadcast: %s", msg);
    g_dbus_connection_emit_signal(
        svc->connection, NULL, DBUS_OBJECT_PATH, DBUS_INTERFACE,
        "SafetyWarning", g_variant_new("(s)", msg ? msg : ""), NULL
    );
}

// FSM Callback
static void on_fsm_state_change(FsmState_t old_state, FsmState_t new_state, const char *reason, void *user_data) {
    MediaManagerService_t *svc = (MediaManagerService_t*)user_data;
    const char *new_str = fsm_state_to_string(new_state);
    emit_state_changed(svc, new_str, reason);

    if (new_state == FSM_STATE_SPEED_RESTRICTED) {
        emit_playback_command(svc, "SPEED_LOCK", "");
        emit_safety_warning(svc, "AUTOMOTIVE SAFETY WARNING: Video display restricted while vehicle in motion!");
    } else if (new_state == FSM_STATE_PAUSED && old_state == FSM_STATE_SPEED_RESTRICTED) {
        emit_safety_warning(svc, "Vehicle stationary. Video playback may be resumed.");
    } else if (new_state == FSM_STATE_NO_MEDIA) {
        emit_playback_command(svc, "STOP", "");
        playlist_scanner_clear(svc->scanner);
        emit_playlist_changed(svc, 0);
    }
}

// Scanner Async Callback
static void on_scan_complete(size_t count, void *user_data) {
    MediaManagerService_t *svc = (MediaManagerService_t*)user_data;
    if (count > 0) {
        char reason[64];
        snprintf(reason, sizeof(reason), "Found %zu files", count);
        fsm_handle_event(&svc->fsm, FSM_EV_SCAN_COMPLETED, reason);
        emit_playlist_changed(svc, (int32_t)count);
    } else {
        LOG_W("MediaMgr-C", "No valid .mp4 or .mkv files found on USB drive");
        fsm_handle_event(&svc->fsm, FSM_EV_SCAN_EMPTY, "Zero files found");
        emit_playlist_changed(svc, 0);
    }
}

// USB Callbacks
static void on_usb_inserted(const char *dev_node, const char *mount_point, void *user_data) {
    MediaManagerService_t *svc = (MediaManagerService_t*)user_data;
    LOG_I("MediaMgr-C", "USB Plugged: %s at %s", dev_node, mount_point);
    fsm_handle_event(&svc->fsm, FSM_EV_USB_INSERTED, dev_node);

    if (!mount_point || mount_point[0] == '\0') {
        LOG_W("MediaMgr-C", "Mount point pending for %s", dev_node);
        return;
    }

    fsm_handle_event(&svc->fsm, FSM_EV_SCAN_STARTED, mount_point);
    playlist_scanner_scan_async(svc->scanner, mount_point, on_scan_complete, svc);
}

static void on_usb_removed(const char *dev_node, void *user_data) {
    MediaManagerService_t *svc = (MediaManagerService_t*)user_data;
    LOG_W("MediaMgr-C", "USB Removed: %s", dev_node);
    fsm_handle_event(&svc->fsm, FSM_EV_USB_REMOVED, dev_node);
}

// D-Bus Method Call Handler
static void handle_method_call(GDBusConnection *conn,
                               const gchar *sender,
                               const gchar *path,
                               const gchar *interface_name,
                               const gchar *method_name,
                               GVariant *parameters,
                               GDBusMethodInvocation *invocation,
                               gpointer user_data) {
    (void)conn; (void)sender; (void)path; (void)interface_name;
    MediaManagerService_t *svc = (MediaManagerService_t*)user_data;
    const char *method = method_name;

    if (strcmp(method, "GetState") == 0) {
        const char *state = fsm_get_state_str(&svc->fsm);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", state));
    }
    else if (strcmp(method, "GetPlaylist") == 0) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("a(ist)"));

        size_t count = playlist_scanner_get_count(svc->scanner);
        for (size_t i = 0; i < count; ++i) {
            VideoTrack_t track;
            if (playlist_scanner_get_track_by_index(svc->scanner, i, &track)) {
                g_variant_builder_add(&builder, "(ist)", track.id, track.title, track.file_size);
            }
        }
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(a(ist))", &builder));
    }
    else if (strcmp(method, "GetCurrentTrack") == 0) {
        pthread_mutex_lock(&svc->state_lock);
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(iss)", svc->current_track.id, svc->current_track.title, svc->current_track.file_path));
        pthread_mutex_unlock(&svc->state_lock);
    }
    else if (strcmp(method, "Play") == 0) {
        gint32 track_id = 0;
        g_variant_get(parameters, "(i)", &track_id);

        VideoTrack_t track;
        if (!playlist_scanner_get_track_by_id(svc->scanner, track_id, &track)) {
            LOG_E("MediaMgr-C", "Track %d not found in playlist", track_id);
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
            return;
        }

        // Safety Guard
        double speed = fsm_get_vehicle_speed(&svc->fsm);
        const char *gear = fsm_get_vehicle_gear(&svc->fsm);
        if (speed > 0.0 || (strcmp(gear, "P") != 0 && strcmp(gear, "PARK") != 0)) {
            LOG_S("MediaMgr-C", "Play rejected: Vehicle moving or not in Park");
            fsm_handle_event(&svc->fsm, FSM_EV_PLAY_REQUEST, "Play while moving");
            emit_safety_warning(svc, "CANNOT PLAY: Vehicle is in motion or not in Park!");
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
            return;
        }

        if (fsm_handle_event(&svc->fsm, FSM_EV_PLAY_REQUEST, "User initiated play")) {
            pthread_mutex_lock(&svc->state_lock);
            svc->current_track_id = track.id;
            svc->current_track = track;
            pthread_mutex_unlock(&svc->state_lock);

            emit_track_changed(svc, track.id, track.title, track.file_path);
            emit_playback_command(svc, "PLAY", track.file_path);
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
        } else {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
        }
    }
    else if (strcmp(method, "Pause") == 0) {
        if (fsm_handle_event(&svc->fsm, FSM_EV_PAUSE_REQUEST, "User paused")) {
            emit_playback_command(svc, "PAUSE", "");
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
        } else {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
        }
    }
    else if (strcmp(method, "Resume") == 0) {
        if (fsm_get_vehicle_speed(&svc->fsm) > 0.0) {
            emit_safety_warning(svc, "CANNOT RESUME: Vehicle speed > 0 km/h!");
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
            return;
        }

        if (fsm_handle_event(&svc->fsm, FSM_EV_RESUME_REQUEST, "User resumed")) {
            emit_playback_command(svc, "RESUME", "");
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
        } else {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
        }
    }
    else if (strcmp(method, "Stop") == 0) {
        if (fsm_handle_event(&svc->fsm, FSM_EV_STOP_REQUEST, "User stopped")) {
            emit_playback_command(svc, "STOP", "");
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
        } else {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
        }
    }
    else if (strcmp(method, "Next") == 0) {
        size_t count = playlist_scanner_get_count(svc->scanner);
        if (count == 0) {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
            return;
        }

        pthread_mutex_lock(&svc->state_lock);
        int32_t next_idx = (svc->current_track_id + 1) % (int32_t)count;
        VideoTrack_t track;
        playlist_scanner_get_track_by_index(svc->scanner, next_idx, &track);
        svc->current_track_id = next_idx;
        svc->current_track = track;
        pthread_mutex_unlock(&svc->state_lock);

        fsm_handle_event(&svc->fsm, FSM_EV_PLAY_REQUEST, "Next track");
        emit_track_changed(svc, track.id, track.title, track.file_path);
        emit_playback_command(svc, "PLAY", track.file_path);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
    }
    else if (strcmp(method, "Previous") == 0) {
        size_t count = playlist_scanner_get_count(svc->scanner);
        if (count == 0) {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
            return;
        }

        pthread_mutex_lock(&svc->state_lock);
        int32_t prev_idx = (svc->current_track_id - 1 + (int32_t)count) % (int32_t)count;
        VideoTrack_t track;
        playlist_scanner_get_track_by_index(svc->scanner, prev_idx, &track);
        svc->current_track_id = prev_idx;
        svc->current_track = track;
        pthread_mutex_unlock(&svc->state_lock);

        fsm_handle_event(&svc->fsm, FSM_EV_PLAY_REQUEST, "Prev track");
        emit_track_changed(svc, track.id, track.title, track.file_path);
        emit_playback_command(svc, "PLAY", track.file_path);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
    }
    else if (strcmp(method, "UpdateVehicleSpeed") == 0) {
        gdouble speed = 0.0;
        g_variant_get(parameters, "(d)", &speed);
        fsm_set_vehicle_speed(&svc->fsm, speed);
        emit_vehicle_speed_changed(svc, speed, fsm_is_speed_restricted(&svc->fsm));
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
    }
    else if (strcmp(method, "UpdateGear") == 0) {
        const gchar *gear_str = NULL;
        g_variant_get(parameters, "(&s)", &gear_str);
        if (gear_str) {
            fsm_set_vehicle_gear(&svc->fsm, gear_str);
        }
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
    }
    else if (strcmp(method, "ReportPlaybackStatus") == 0) {
        const gchar *status = NULL;
        gint64 pos = 0, dur = 0;
        g_variant_get(parameters, "(&sxx)", &status, &pos, &dur);
        pthread_mutex_lock(&svc->state_lock);
        if (status) {
            strncpy(svc->last_playback_status, status, sizeof(svc->last_playback_status) - 1);
        }
        svc->last_position_ms = pos;
        svc->last_duration_ms = dur;
        pthread_mutex_unlock(&svc->state_lock);

        emit_playback_status_changed(svc, status ? status : "", pos, dur);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
    }
    else if (strcmp(method, "ReportEndOfStream") == 0) {
        fsm_handle_event(&svc->fsm, FSM_EV_PLAYBACK_EOS, "Playback finished");
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
    }
    else if (strcmp(method, "SimulateUsb") == 0) {
        const gchar *action = NULL;
        const gchar *path = NULL;
        g_variant_get(parameters, "(&s&s)", &action, &path);
        if (action && strcmp(action, "add") == 0) {
            usb_monitor_simulate_insert(svc->usb_monitor, path ? path : "/tmp/usb_test");
        } else if (action && strcmp(action, "remove") == 0) {
            usb_monitor_simulate_remove(svc->usb_monitor);
        }
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", TRUE));
    }
    else {
        LOG_W("MediaMgr-C", "Unknown D-Bus method: %s", method);
        g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                                              "Unknown method: %s", method);
    }
}

static const GDBusInterfaceVTable s_interface_vtable = {
    handle_method_call,
    NULL, // get_property
    NULL, // set_property
    {NULL}
};

static void on_name_acquired(GDBusConnection *conn, const gchar *name, gpointer user_data) {
    (void)conn; (void)user_data;
    LOG_I("MediaMgr-C", "D-Bus service name successfully acquired: %s", name);
}

static void on_name_lost(GDBusConnection *conn, const gchar *name, gpointer user_data) {
    (void)conn; (void)user_data;
    LOG_W("MediaMgr-C", "D-Bus service name lost or failed: %s", name ? name : "unknown");
}

MediaManagerService_t* media_manager_service_create(void) {
    MediaManagerService_t *svc = (MediaManagerService_t*)calloc(1, sizeof(MediaManagerService_t));
    if (!svc) return NULL;

    fsm_init(&svc->fsm);
    svc->usb_monitor = usb_monitor_create();
    svc->scanner = playlist_scanner_create();
    svc->main_loop = g_main_loop_new(NULL, FALSE);
    svc->current_track_id = -1;
    strncpy(svc->last_playback_status, "STOPPED", sizeof(svc->last_playback_status) - 1);
    pthread_mutex_init(&svc->state_lock, NULL);

    svc->introspection_data = g_dbus_node_info_new_for_xml(s_introspection_xml, NULL);
    return svc;
}

void media_manager_service_destroy(MediaManagerService_t *svc) {
    if (!svc) return;
    media_manager_service_stop(svc);

    if (svc->usb_monitor) {
        usb_monitor_destroy(svc->usb_monitor);
        svc->usb_monitor = NULL;
    }
    if (svc->scanner) {
        playlist_scanner_destroy(svc->scanner);
        svc->scanner = NULL;
    }
    if (svc->introspection_data) {
        g_dbus_node_info_unref(svc->introspection_data);
        svc->introspection_data = NULL;
    }
    if (svc->main_loop) {
        g_main_loop_unref(svc->main_loop);
        svc->main_loop = NULL;
    }
    fsm_destroy(&svc->fsm);
    pthread_mutex_destroy(&svc->state_lock);
    free(svc);
}

bool media_manager_service_init(MediaManagerService_t *svc) {
    if (!svc) return false;

    // 1. Setup FSM callback
    fsm_register_state_change_cb(&svc->fsm, on_fsm_state_change, svc);

    // 2. Setup USB monitor callbacks
    usb_monitor_set_callbacks(svc->usb_monitor, on_usb_inserted, on_usb_removed, svc);

    // 3. Initialize hardware udev listener
    usb_monitor_init(svc->usb_monitor);

    // 4. Connect to D-Bus (System bus preferred, fallback to Session bus)
    GError *error = NULL;
    svc->connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!svc->connection) {
        LOG_W("MediaMgr-C", "System D-Bus unavailable (%s). Falling back to Session Bus.", error->message);
        g_clear_error(&error);
        svc->connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
        if (!svc->connection) {
            LOG_E("MediaMgr-C", "Failed to connect to D-Bus: %s", error->message);
            g_clear_error(&error);
            return false;
        }
    }

    svc->owner_id = g_bus_own_name_on_connection(
        svc->connection,
        DBUS_SERVICE_NAME,
        G_BUS_NAME_OWNER_FLAGS_REPLACE,
        on_name_acquired,
        on_name_lost,
        svc,
        NULL
    );

    // Register Object
    svc->registration_id = g_dbus_connection_register_object(
        svc->connection,
        DBUS_OBJECT_PATH,
        svc->introspection_data->interfaces[0],
        &s_interface_vtable,
        svc,
        NULL,
        &error
    );

    if (svc->registration_id == 0) {
        LOG_E("MediaMgr-C", "Failed to register D-Bus Object: %s", error ? error->message : "unknown");
        if (error) g_error_free(error);
        return false;
    }

    LOG_I("MediaMgr-C", "Registered C D-Bus Object at: %s", DBUS_OBJECT_PATH);

    // Start hardware monitor thread
    usb_monitor_start(svc->usb_monitor);

    return true;
}

void media_manager_service_run(MediaManagerService_t *svc) {
    if (!svc || !svc->main_loop) return;
    LOG_I("MediaMgr-C", "Entering GLib Main Loop (C Version)...");
    g_main_loop_run(svc->main_loop);
}

void media_manager_service_stop(MediaManagerService_t *svc) {
    if (!svc) return;

    if (svc->usb_monitor) {
        usb_monitor_stop(svc->usb_monitor);
    }
    if (svc->owner_id > 0) {
        g_bus_unown_name(svc->owner_id);
        svc->owner_id = 0;
    }
    if (svc->registration_id > 0 && svc->connection) {
        g_dbus_connection_unregister_object(svc->connection, svc->registration_id);
        svc->registration_id = 0;
    }
    if (svc->main_loop && g_main_loop_is_running(svc->main_loop)) {
        g_main_loop_quit(svc->main_loop);
    }
}
