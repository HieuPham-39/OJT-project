#include "fsm.h"
#include "logger.h"

#include <string.h>
#include <stdio.h>

static int s_transition_table[FSM_STATE_MAX][FSM_EV_MAX];
static bool s_table_initialized = false;

static void init_transition_table(void) {
    if (s_table_initialized) return;

    for (int s = 0; s < FSM_STATE_MAX; ++s) {
        for (int e = 0; e < FSM_EV_MAX; ++e) {
            s_transition_table[s][e] = -1; // Illegal transition sentinel
        }
    }

    // 1. From NO_MEDIA
    s_transition_table[FSM_STATE_NO_MEDIA][FSM_EV_USB_INSERTED]   = FSM_STATE_MEDIA_DETECTED;

    // 2. From MEDIA_DETECTED
    s_transition_table[FSM_STATE_MEDIA_DETECTED][FSM_EV_SCAN_STARTED] = FSM_STATE_SCANNING;
    s_transition_table[FSM_STATE_MEDIA_DETECTED][FSM_EV_USB_REMOVED]  = FSM_STATE_NO_MEDIA;

    // 3. From SCANNING
    s_transition_table[FSM_STATE_SCANNING][FSM_EV_SCAN_COMPLETED] = FSM_STATE_MEDIA_READY;
    s_transition_table[FSM_STATE_SCANNING][FSM_EV_SCAN_EMPTY]     = FSM_STATE_NO_MEDIA;
    s_transition_table[FSM_STATE_SCANNING][FSM_EV_USB_REMOVED]    = FSM_STATE_NO_MEDIA;

    // 4. From MEDIA_READY
    s_transition_table[FSM_STATE_MEDIA_READY][FSM_EV_PLAY_REQUEST] = FSM_STATE_PLAYING;
    s_transition_table[FSM_STATE_MEDIA_READY][FSM_EV_USB_REMOVED]  = FSM_STATE_NO_MEDIA;

    // 5. From PLAYING
    s_transition_table[FSM_STATE_PLAYING][FSM_EV_PAUSE_REQUEST]   = FSM_STATE_PAUSED;
    s_transition_table[FSM_STATE_PLAYING][FSM_EV_STOP_REQUEST]    = FSM_STATE_MEDIA_READY;
    s_transition_table[FSM_STATE_PLAYING][FSM_EV_PLAYBACK_EOS]    = FSM_STATE_MEDIA_READY;
    s_transition_table[FSM_STATE_PLAYING][FSM_EV_SPEED_EXCEEDED]  = FSM_STATE_SPEED_RESTRICTED;
    s_transition_table[FSM_STATE_PLAYING][FSM_EV_USB_REMOVED]     = FSM_STATE_NO_MEDIA;

    // 6. From PAUSED
    s_transition_table[FSM_STATE_PAUSED][FSM_EV_RESUME_REQUEST]  = FSM_STATE_PLAYING;
    s_transition_table[FSM_STATE_PAUSED][FSM_EV_STOP_REQUEST]    = FSM_STATE_MEDIA_READY;
    s_transition_table[FSM_STATE_PAUSED][FSM_EV_SPEED_EXCEEDED]  = FSM_STATE_SPEED_RESTRICTED;
    s_transition_table[FSM_STATE_PAUSED][FSM_EV_USB_REMOVED]     = FSM_STATE_NO_MEDIA;

    // 7. From SPEED_RESTRICTED
    s_transition_table[FSM_STATE_SPEED_RESTRICTED][FSM_EV_SPEED_NORMAL] = FSM_STATE_PAUSED;
    s_transition_table[FSM_STATE_SPEED_RESTRICTED][FSM_EV_STOP_REQUEST] = FSM_STATE_MEDIA_READY;
    s_transition_table[FSM_STATE_SPEED_RESTRICTED][FSM_EV_USB_REMOVED]  = FSM_STATE_NO_MEDIA;

    // 8. From ERROR
    s_transition_table[FSM_STATE_ERROR][FSM_EV_USB_REMOVED]        = FSM_STATE_NO_MEDIA;

    s_table_initialized = true;
}

const char* fsm_state_to_string(FsmState_t s) {
    switch (s) {
        case FSM_STATE_NO_MEDIA:         return "NO_MEDIA";
        case FSM_STATE_MEDIA_DETECTED:   return "MEDIA_DETECTED";
        case FSM_STATE_SCANNING:         return "SCANNING";
        case FSM_STATE_MEDIA_READY:      return "MEDIA_READY";
        case FSM_STATE_PLAYING:          return "PLAYING";
        case FSM_STATE_PAUSED:           return "PAUSED";
        case FSM_STATE_SPEED_RESTRICTED: return "SPEED_RESTRICTED";
        case FSM_STATE_ERROR:            return "ERROR";
        default:                         return "UNKNOWN";
    }
}

const char* fsm_event_to_string(FsmEvent_t e) {
    switch (e) {
        case FSM_EV_USB_INSERTED:   return "EV_USB_INSERTED";
        case FSM_EV_USB_REMOVED:    return "EV_USB_REMOVED";
        case FSM_EV_SCAN_STARTED:   return "EV_SCAN_STARTED";
        case FSM_EV_SCAN_COMPLETED: return "EV_SCAN_COMPLETED";
        case FSM_EV_SCAN_EMPTY:     return "EV_SCAN_EMPTY";
        case FSM_EV_PLAY_REQUEST:   return "EV_PLAY_REQUEST";
        case FSM_EV_PAUSE_REQUEST:  return "EV_PAUSE_REQUEST";
        case FSM_EV_RESUME_REQUEST: return "EV_RESUME_REQUEST";
        case FSM_EV_STOP_REQUEST:   return "EV_STOP_REQUEST";
        case FSM_EV_SPEED_EXCEEDED: return "EV_SPEED_EXCEEDED";
        case FSM_EV_SPEED_NORMAL:   return "EV_SPEED_NORMAL";
        case FSM_EV_PLAYBACK_EOS:   return "EV_PLAYBACK_EOS";
        case FSM_EV_ERROR:          return "EV_ERROR";
        default:                    return "UNKNOWN_EVENT";
    }
}

void fsm_init(Fsm_t *fsm) {
    if (!fsm) return;
    init_transition_table();

    memset(fsm, 0, sizeof(Fsm_t));
    fsm->current_state = FSM_STATE_NO_MEDIA;
    fsm->vehicle_speed = 0.0;
    strncpy(fsm->gear, "P", sizeof(fsm->gear) - 1);
    pthread_mutex_init(&fsm->mutex, NULL);

    LOG_I("FSM", "C-based Automotive FSM initialized in state: %s", fsm_state_to_string(fsm->current_state));
}

void fsm_destroy(Fsm_t *fsm) {
    if (!fsm) return;
    pthread_mutex_destroy(&fsm->mutex);
}

FsmState_t fsm_get_state(Fsm_t *fsm) {
    if (!fsm) return FSM_STATE_ERROR;
    pthread_mutex_lock(&fsm->mutex);
    FsmState_t state = fsm->current_state;
    pthread_mutex_unlock(&fsm->mutex);
    return state;
}

const char* fsm_get_state_str(Fsm_t *fsm) {
    return fsm_state_to_string(fsm_get_state(fsm));
}

static bool check_playback_guard(Fsm_t *fsm) {
    if (fsm->vehicle_speed > 0.0) {
        LOG_S("FSM", "Safety Guard: Vehicle Speed (%.1f km/h) > 0. Playback locked.", fsm->vehicle_speed);
        return false;
    }
    if (strcmp(fsm->gear, "P") != 0 && strcmp(fsm->gear, "PARK") != 0) {
        LOG_S("FSM", "Safety Guard: Gear (%s) != P. Playback locked.", fsm->gear);
        return false;
    }
    return true;
}

bool fsm_handle_event(Fsm_t *fsm, FsmEvent_t event, const char *reason) {
    if (!fsm) return false;

    pthread_mutex_lock(&fsm->mutex);

    LOG_FSM("FSM", "Processing Event: %s (Current State: %s, Reason: %s)",
            fsm_event_to_string(event),
            fsm_state_to_string(fsm->current_state),
            reason ? reason : "None");

    FsmState_t next_state;

    // Universal error / removal checks
    if (event == FSM_EV_ERROR) {
        next_state = FSM_STATE_ERROR;
    } else if (event == FSM_EV_USB_REMOVED) {
        next_state = FSM_STATE_NO_MEDIA;
    } else {
        int target = s_transition_table[fsm->current_state][event];
        if (target < 0) {
            LOG_W("FSM", "Illegal State Transition Rejected: %s --(%s)--> [BLOCKED]",
                  fsm_state_to_string(fsm->current_state),
                  fsm_event_to_string(event));
            pthread_mutex_unlock(&fsm->mutex);
            return false;
        }
        next_state = (FsmState_t)target;
    }

    // Safety guard enforcement when entering PLAYING
    if (next_state == FSM_STATE_PLAYING) {
        if (!check_playback_guard(fsm)) {
            LOG_S("FSM", "Driver distraction prevention: Forcing SPEED_RESTRICTED state!");
            next_state = FSM_STATE_SPEED_RESTRICTED;
        }
    }

    FsmState_t old_state = fsm->current_state;
    fsm->current_state = next_state;

    LOG_FSM("FSM", "State Transition: [%s] ===> [%s]",
            fsm_state_to_string(old_state),
            fsm_state_to_string(fsm->current_state));

    // Copy callbacks to execute outside lock
    fsm_state_change_cb_t cbs[FSM_MAX_CALLBACKS];
    void *cb_data[FSM_MAX_CALLBACKS];
    size_t count = fsm->callback_count;
    for (size_t i = 0; i < count; ++i) {
        cbs[i] = fsm->callbacks[i];
        cb_data[i] = fsm->callback_user_data[i];
    }

    pthread_mutex_unlock(&fsm->mutex);

    // Trigger observers
    for (size_t i = 0; i < count; ++i) {
        if (cbs[i]) {
            cbs[i](old_state, next_state, reason ? reason : "", cb_data[i]);
        }
    }

    return true;
}

void fsm_set_vehicle_speed(Fsm_t *fsm, double speed_kmh) {
    if (!fsm) return;
    pthread_mutex_lock(&fsm->mutex);

    double old_speed = fsm->vehicle_speed;
    fsm->vehicle_speed = (speed_kmh < 0.0) ? 0.0 : speed_kmh;

    if (old_speed != fsm->vehicle_speed) {
        LOG_I("FSM", "Vehicle Speed: %.1f km/h", fsm->vehicle_speed);
    }

    if (fsm->vehicle_speed > 0.0 && fsm->current_state == FSM_STATE_PLAYING) {
        LOG_S("FSM", "Speed exceeded 0 while playing! Transitioning to SPEED_RESTRICTED");
        pthread_mutex_unlock(&fsm->mutex);
        fsm_handle_event(fsm, FSM_EV_SPEED_EXCEEDED, "Speed > 0 km/h");
        return;
    }

    if (fsm->vehicle_speed == 0.0 && 
        (strcmp(fsm->gear, "P") == 0 || strcmp(fsm->gear, "PARK") == 0) &&
        fsm->current_state == FSM_STATE_SPEED_RESTRICTED) {
        LOG_S("FSM", "Vehicle stopped and in Park. Clearing speed restriction.");
        pthread_mutex_unlock(&fsm->mutex);
        fsm_handle_event(fsm, FSM_EV_SPEED_NORMAL, "Vehicle stationary");
        return;
    }

    pthread_mutex_unlock(&fsm->mutex);
}

void fsm_set_vehicle_gear(Fsm_t *fsm, const char *gear) {
    if (!fsm || !gear) return;
    pthread_mutex_lock(&fsm->mutex);

    strncpy(fsm->gear, gear, sizeof(fsm->gear) - 1);
    fsm->gear[sizeof(fsm->gear) - 1] = '\0';
    LOG_I("FSM", "Vehicle Gear: %s", fsm->gear);

    if (strcmp(fsm->gear, "P") != 0 && strcmp(fsm->gear, "PARK") != 0 &&
        fsm->current_state == FSM_STATE_PLAYING) {
        LOG_S("FSM", "Gear shifted out of Park while playing! Transitioning to SPEED_RESTRICTED");
        pthread_mutex_unlock(&fsm->mutex);
        fsm_handle_event(fsm, FSM_EV_SPEED_EXCEEDED, "Gear shifted out of Park");
        return;
    }

    if (fsm->vehicle_speed == 0.0 && 
        (strcmp(fsm->gear, "P") == 0 || strcmp(fsm->gear, "PARK") == 0) &&
        fsm->current_state == FSM_STATE_SPEED_RESTRICTED) {
        LOG_S("FSM", "Vehicle in Park and stationary. Clearing speed restriction.");
        pthread_mutex_unlock(&fsm->mutex);
        fsm_handle_event(fsm, FSM_EV_SPEED_NORMAL, "Gear in Park");
        return;
    }

    pthread_mutex_unlock(&fsm->mutex);
}

double fsm_get_vehicle_speed(Fsm_t *fsm) {
    if (!fsm) return 0.0;
    pthread_mutex_lock(&fsm->mutex);
    double s = fsm->vehicle_speed;
    pthread_mutex_unlock(&fsm->mutex);
    return s;
}

const char* fsm_get_vehicle_gear(Fsm_t *fsm) {
    if (!fsm) return "P";
    return fsm->gear;
}

bool fsm_is_speed_restricted(Fsm_t *fsm) {
    if (!fsm) return false;
    pthread_mutex_lock(&fsm->mutex);
    bool res = (fsm->current_state == FSM_STATE_SPEED_RESTRICTED);
    pthread_mutex_unlock(&fsm->mutex);
    return res;
}

void fsm_register_state_change_cb(Fsm_t *fsm, fsm_state_change_cb_t cb, void *user_data) {
    if (!fsm || !cb) return;
    pthread_mutex_lock(&fsm->mutex);
    if (fsm->callback_count < FSM_MAX_CALLBACKS) {
        fsm->callbacks[fsm->callback_count] = cb;
        fsm->callback_user_data[fsm->callback_count] = user_data;
        fsm->callback_count++;
    }
    pthread_mutex_unlock(&fsm->mutex);
}

void fsm_reset(Fsm_t *fsm) {
    if (!fsm) return;
    pthread_mutex_lock(&fsm->mutex);
    FsmState_t old = fsm->current_state;
    fsm->current_state = FSM_STATE_NO_MEDIA;
    fsm->vehicle_speed = 0.0;
    strncpy(fsm->gear, "P", sizeof(fsm->gear) - 1);
    pthread_mutex_unlock(&fsm->mutex);

    LOG_I("FSM", "FSM Reset to NO_MEDIA");
    for (size_t i = 0; i < fsm->callback_count; ++i) {
        if (fsm->callbacks[i]) {
            fsm->callbacks[i](old, FSM_STATE_NO_MEDIA, "Reset", fsm->callback_user_data[i]);
        }
    }
}
