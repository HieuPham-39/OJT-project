#ifndef AUTOMOTIVE_FSM_H
#define AUTOMOTIVE_FSM_H

#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FSM_STATE_NO_MEDIA = 0,
    FSM_STATE_MEDIA_DETECTED,
    FSM_STATE_SCANNING,
    FSM_STATE_MEDIA_READY,
    FSM_STATE_PLAYING,
    FSM_STATE_PAUSED,
    FSM_STATE_SPEED_RESTRICTED,
    FSM_STATE_ERROR,
    FSM_STATE_MAX
} FsmState_t;

typedef enum {
    FSM_EV_USB_INSERTED = 0,
    FSM_EV_USB_REMOVED,
    FSM_EV_SCAN_STARTED,
    FSM_EV_SCAN_COMPLETED,
    FSM_EV_SCAN_EMPTY,
    FSM_EV_PLAY_REQUEST,
    FSM_EV_PAUSE_REQUEST,
    FSM_EV_RESUME_REQUEST,
    FSM_EV_STOP_REQUEST,
    FSM_EV_SPEED_EXCEEDED,
    FSM_EV_SPEED_NORMAL,
    FSM_EV_PLAYBACK_EOS,
    FSM_EV_ERROR,
    FSM_EV_MAX
} FsmEvent_t;

typedef void (*fsm_state_change_cb_t)(FsmState_t old_state, FsmState_t new_state, const char *reason, void *user_data);

#define FSM_MAX_CALLBACKS 8

typedef struct {
    FsmState_t current_state;
    double vehicle_speed;
    char gear[16];

    fsm_state_change_cb_t callbacks[FSM_MAX_CALLBACKS];
    void *callback_user_data[FSM_MAX_CALLBACKS];
    size_t callback_count;

    pthread_mutex_t mutex;
} Fsm_t;

const char* fsm_state_to_string(FsmState_t s);
const char* fsm_event_to_string(FsmEvent_t e);

void fsm_init(Fsm_t *fsm);
void fsm_destroy(Fsm_t *fsm);

FsmState_t fsm_get_state(Fsm_t *fsm);
const char* fsm_get_state_str(Fsm_t *fsm);

bool fsm_handle_event(Fsm_t *fsm, FsmEvent_t event, const char *reason);

void fsm_set_vehicle_speed(Fsm_t *fsm, double speed_kmh);
void fsm_set_vehicle_gear(Fsm_t *fsm, const char *gear);

double fsm_get_vehicle_speed(Fsm_t *fsm);
const char* fsm_get_vehicle_gear(Fsm_t *fsm);
bool fsm_is_speed_restricted(Fsm_t *fsm);

void fsm_register_state_change_cb(Fsm_t *fsm, fsm_state_change_cb_t cb, void *user_data);
void fsm_reset(Fsm_t *fsm);

#ifdef __cplusplus
}
#endif

#endif /* AUTOMOTIVE_FSM_H */
