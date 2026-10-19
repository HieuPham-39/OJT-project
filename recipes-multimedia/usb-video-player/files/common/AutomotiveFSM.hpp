#pragma once

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <map>

namespace automotive {

enum class State {
    NO_MEDIA,
    MEDIA_DETECTED,
    SCANNING,
    MEDIA_READY,
    PLAYING,
    PAUSED,
    SPEED_RESTRICTED,
    ERROR
};

enum class Event {
    EV_USB_INSERTED,
    EV_USB_REMOVED,
    EV_SCAN_STARTED,
    EV_SCAN_COMPLETED,
    EV_SCAN_EMPTY,
    EV_PLAY_REQUEST,
    EV_PAUSE_REQUEST,
    EV_RESUME_REQUEST,
    EV_STOP_REQUEST,
    EV_SPEED_EXCEEDED,
    EV_SPEED_NORMAL,
    EV_PLAYBACK_EOS,
    EV_ERROR
};

std::string stateToString(State s);
std::string eventToString(Event e);

using StateChangeCallback = std::function<void(State oldState, State newState, const std::string& reason)>;

class AutomotiveFSM {
public:
    AutomotiveFSM();
    ~AutomotiveFSM() = default;

    // State query
    State getCurrentState() const;
    std::string getCurrentStateString() const;

    // Event handling
    bool handleEvent(Event event, const std::string& reason = "");

    // Safety telemetry setters
    void setVehicleSpeed(double speedKmh);
    void setVehicleGear(const std::string& gear);

    double getVehicleSpeed() const;
    std::string getVehicleGear() const;
    bool isSpeedRestricted() const;

    // Callbacks
    void registerStateChangeCallback(StateChangeCallback cb);

    // Reset machine
    void reset();

private:
    bool isTransitionAllowed(State from, Event ev, State& to) const;
    bool checkPlaybackSafetyGuard() const;

    mutable std::mutex m_mutex;
    State m_currentState{State::NO_MEDIA};
    State m_previousPlaybackState{State::MEDIA_READY}; // To restore after speed restriction clears

    double m_vehicleSpeed{0.0};
    std::string m_gear{"P"};

    std::vector<StateChangeCallback> m_callbacks;

    // Transition table: map of (CurrentState, Event) -> NextState
    std::map<std::pair<State, Event>, State> m_transitionTable;
};

} // namespace automotive
