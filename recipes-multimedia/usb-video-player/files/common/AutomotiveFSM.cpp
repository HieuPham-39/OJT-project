#include "AutomotiveFSM.hpp"
#include "Logger.hpp"

namespace automotive {

std::string stateToString(State s) {
    switch (s) {
        case State::NO_MEDIA:         return "NO_MEDIA";
        case State::MEDIA_DETECTED:   return "MEDIA_DETECTED";
        case State::SCANNING:         return "SCANNING";
        case State::MEDIA_READY:      return "MEDIA_READY";
        case State::PLAYING:          return "PLAYING";
        case State::PAUSED:           return "PAUSED";
        case State::SPEED_RESTRICTED: return "SPEED_RESTRICTED";
        case State::ERROR:            return "ERROR";
        default:                      return "UNKNOWN";
    }
}

std::string eventToString(Event e) {
    switch (e) {
        case Event::EV_USB_INSERTED:    return "EV_USB_INSERTED";
        case Event::EV_USB_REMOVED:     return "EV_USB_REMOVED";
        case Event::EV_SCAN_STARTED:    return "EV_SCAN_STARTED";
        case Event::EV_SCAN_COMPLETED:  return "EV_SCAN_COMPLETED";
        case Event::EV_SCAN_EMPTY:      return "EV_SCAN_EMPTY";
        case Event::EV_PLAY_REQUEST:    return "EV_PLAY_REQUEST";
        case Event::EV_PAUSE_REQUEST:   return "EV_PAUSE_REQUEST";
        case Event::EV_RESUME_REQUEST:  return "EV_RESUME_REQUEST";
        case Event::EV_STOP_REQUEST:    return "EV_STOP_REQUEST";
        case Event::EV_SPEED_EXCEEDED:  return "EV_SPEED_EXCEEDED";
        case Event::EV_SPEED_NORMAL:    return "EV_SPEED_NORMAL";
        case Event::EV_PLAYBACK_EOS:    return "EV_PLAYBACK_EOS";
        case Event::EV_ERROR:           return "EV_ERROR";
        default:                        return "UNKNOWN_EVENT";
    }
}

AutomotiveFSM::AutomotiveFSM() {
    // Define explicit, deterministic transition table
    // Format: {FromState, Event} -> ToState

    // 1. From NO_MEDIA
    m_transitionTable[{State::NO_MEDIA, Event::EV_USB_INSERTED}]   = State::MEDIA_DETECTED;

    // 2. From MEDIA_DETECTED
    m_transitionTable[{State::MEDIA_DETECTED, Event::EV_SCAN_STARTED}]   = State::SCANNING;
    m_transitionTable[{State::MEDIA_DETECTED, Event::EV_USB_REMOVED}]    = State::NO_MEDIA;

    // 3. From SCANNING
    m_transitionTable[{State::SCANNING, Event::EV_SCAN_COMPLETED}] = State::MEDIA_READY;
    m_transitionTable[{State::SCANNING, Event::EV_SCAN_EMPTY}]     = State::NO_MEDIA;
    m_transitionTable[{State::SCANNING, Event::EV_USB_REMOVED}]    = State::NO_MEDIA;

    // 4. From MEDIA_READY
    m_transitionTable[{State::MEDIA_READY, Event::EV_PLAY_REQUEST}]   = State::PLAYING;
    m_transitionTable[{State::MEDIA_READY, Event::EV_USB_REMOVED}]    = State::NO_MEDIA;

    // 5. From PLAYING
    m_transitionTable[{State::PLAYING, Event::EV_PAUSE_REQUEST}]    = State::PAUSED;
    m_transitionTable[{State::PLAYING, Event::EV_STOP_REQUEST}]     = State::MEDIA_READY;
    m_transitionTable[{State::PLAYING, Event::EV_PLAYBACK_EOS}]     = State::MEDIA_READY;
    m_transitionTable[{State::PLAYING, Event::EV_SPEED_EXCEEDED}]   = State::SPEED_RESTRICTED;
    m_transitionTable[{State::PLAYING, Event::EV_USB_REMOVED}]      = State::NO_MEDIA;

    // 6. From PAUSED
    m_transitionTable[{State::PAUSED, Event::EV_RESUME_REQUEST}]   = State::PLAYING;
    m_transitionTable[{State::PAUSED, Event::EV_STOP_REQUEST}]     = State::MEDIA_READY;
    m_transitionTable[{State::PAUSED, Event::EV_SPEED_EXCEEDED}]   = State::SPEED_RESTRICTED;
    m_transitionTable[{State::PAUSED, Event::EV_USB_REMOVED}]      = State::NO_MEDIA;

    // 7. From SPEED_RESTRICTED
    m_transitionTable[{State::SPEED_RESTRICTED, Event::EV_SPEED_NORMAL}] = State::PAUSED;
    m_transitionTable[{State::SPEED_RESTRICTED, Event::EV_STOP_REQUEST}] = State::MEDIA_READY;
    m_transitionTable[{State::SPEED_RESTRICTED, Event::EV_USB_REMOVED}]  = State::NO_MEDIA;

    // 8. From ERROR
    m_transitionTable[{State::ERROR, Event::EV_USB_REMOVED}]       = State::NO_MEDIA;

    LOG_I("FSM", "Automotive Finite State Machine initialized in state: " + stateToString(m_currentState));
}

State AutomotiveFSM::getCurrentState() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentState;
}

std::string AutomotiveFSM::getCurrentStateString() const {
    return stateToString(getCurrentState());
}

bool AutomotiveFSM::checkPlaybackSafetyGuard() const {
    // NHTSA / Automotive Safety Interlock:
    // Video in motion is strictly forbidden if vehicle speed > 0 or transmission not in Park
    if (m_vehicleSpeed > 0.0) {
        LOG_S("FSM", "Safety Interlock Activated: Speed (" + std::to_string(m_vehicleSpeed) + " km/h) > 0");
        return false;
    }
    if (m_gear != "P" && m_gear != "PARK") {
        LOG_S("FSM", "Safety Interlock Activated: Gear (" + m_gear + ") != P");
        return false;
    }
    return true;
}

bool AutomotiveFSM::isTransitionAllowed(State from, Event ev, State& to) const {
    // Universal error transition
    if (ev == Event::EV_ERROR) {
        to = State::ERROR;
        return true;
    }

    // Universal USB removal transition: wherever we are, USB removed brings us back to NO_MEDIA
    if (ev == Event::EV_USB_REMOVED) {
        to = State::NO_MEDIA;
        return true;
    }

    auto it = m_transitionTable.find({from, ev});
    if (it != m_transitionTable.end()) {
        to = it->second;
        return true;
    }

    return false;
}

bool AutomotiveFSM::handleEvent(Event event, const std::string& reason) {
    std::lock_guard<std::mutex> lock(m_mutex);

    LOG_FSM("FSM", "Processing Event: " + eventToString(event) + 
                   " (Current: " + stateToString(m_currentState) + 
                   (reason.empty() ? "" : ", Reason: " + reason) + ")");

    State nextState;
    if (!isTransitionAllowed(m_currentState, event, nextState)) {
        LOG_W("FSM", "Illegal State Transition Rejected: " + 
                     stateToString(m_currentState) + " --(" + eventToString(event) + ")--> [BLOCKED]");
        return false;
    }

    // Safety guard on transitioning to PLAYING
    if (nextState == State::PLAYING) {
        if (!checkPlaybackSafetyGuard()) {
            LOG_S("FSM", "Playback request denied by safety interlock. Enforcing SPEED_RESTRICTED state.");
            nextState = State::SPEED_RESTRICTED;
        }
    }

    State oldState = m_currentState;
    m_currentState = nextState;

    LOG_FSM("FSM", "State Transition: [" + stateToString(oldState) + 
                   "] ===> [" + stateToString(m_currentState) + "]");

    // Dispatch callbacks to observers
    for (const auto& cb : m_callbacks) {
        if (cb) {
            cb(oldState, m_currentState, reason);
        }
    }

    return true;
}

void AutomotiveFSM::setVehicleSpeed(double speedKmh) {
    std::lock_guard<std::mutex> lock(m_mutex);
    double oldSpeed = m_vehicleSpeed;
    m_vehicleSpeed = (speedKmh < 0.0) ? 0.0 : speedKmh;

    if (oldSpeed != m_vehicleSpeed) {
        LOG_I("FSM", "Vehicle Speed updated: " + std::to_string(m_vehicleSpeed) + " km/h");
    }

    if (m_vehicleSpeed > 0.0 && m_currentState == State::PLAYING) {
        LOG_S("FSM", "Vehicle in motion! Speed = " + std::to_string(m_vehicleSpeed) + " km/h");
        lock.unlock();
        handleEvent(Event::EV_SPEED_EXCEEDED, "Speed > 0 km/h");
        return;
    }

    if (m_vehicleSpeed == 0.0 && (m_gear == "P" || m_gear == "PARK") && m_currentState == State::SPEED_RESTRICTED) {
        LOG_S("FSM", "Vehicle stationary and in Park. Speed restriction cleared.");
        lock.unlock();
        handleEvent(Event::EV_SPEED_NORMAL, "Vehicle stopped");
        return;
    }
}

void AutomotiveFSM::setVehicleGear(const std::string& gear) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_gear = gear;
    LOG_I("FSM", "Vehicle Gear updated: " + m_gear);

    if (m_gear != "P" && m_gear != "PARK" && m_currentState == State::PLAYING) {
        LOG_S("FSM", "Gear shifted out of Park while playing! Gear = " + m_gear);
        lock.unlock();
        handleEvent(Event::EV_SPEED_EXCEEDED, "Gear shifted out of Park");
        return;
    }

    if (m_vehicleSpeed == 0.0 && (m_gear == "P" || m_gear == "PARK") && m_currentState == State::SPEED_RESTRICTED) {
        LOG_S("FSM", "Vehicle stationary and in Park. Speed restriction cleared.");
        lock.unlock();
        handleEvent(Event::EV_SPEED_NORMAL, "Gear in Park");
        return;
    }
}

double AutomotiveFSM::getVehicleSpeed() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_vehicleSpeed;
}

std::string AutomotiveFSM::getVehicleGear() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_gear;
}

bool AutomotiveFSM::isSpeedRestricted() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return (m_currentState == State::SPEED_RESTRICTED);
}

void AutomotiveFSM::registerStateChangeCallback(StateChangeCallback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callbacks.push_back(cb);
}

void AutomotiveFSM::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    State old = m_currentState;
    m_currentState = State::NO_MEDIA;
    m_vehicleSpeed = 0.0;
    m_gear = "P";
    LOG_I("FSM", "FSM Reset to NO_MEDIA");
    for (const auto& cb : m_callbacks) {
        if (cb) {
            cb(old, m_currentState, "Reset");
        }
    }
}

} // namespace automotive
