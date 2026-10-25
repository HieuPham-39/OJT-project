#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace automotive {

constexpr const char* DBUS_SERVICE_NAME = "org.automotive.MediaManager";
constexpr const char* DBUS_OBJECT_PATH  = "/org/automotive/MediaManager";
constexpr const char* DBUS_INTERFACE    = "org.automotive.MediaManager1";

struct VideoTrack {
    int32_t id{0};
    std::string title;
    std::string filePath;
    uint64_t fileSizeBytes{0};
    int64_t durationMs{0};
};

// D-Bus Introspection XML representing the complete Automotive Media Manager Interface
constexpr const char* INTROSPECTION_XML = 
R"xml(<!DOCTYPE node PUBLIC "-//freedesktop//DTD D-BUS Object Introspection 1.0//EN"
"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd">
<node>
  <interface name="org.automotive.MediaManager1">
    <!-- State Management -->
    <method name="GetState">
      <arg name="state" type="s" direction="out"/>
    </method>
    
    <!-- Playlist Management -->
    <method name="GetPlaylist">
      <arg name="playlist" type="a(ist)" direction="out"/>
    </method>
    <method name="GetCurrentTrack">
      <arg name="track_id" type="i" direction="out"/>
      <arg name="title" type="s" direction="out"/>
      <arg name="path" type="s" direction="out"/>
    </method>

    <!-- Playback Control (Called by UI) -->
    <method name="Play">
      <arg name="track_id" type="i" direction="in"/>
      <arg name="success" type="b" direction="out"/>
    </method>
    <method name="Pause">
      <arg name="success" type="b" direction="out"/>
    </method>
    <method name="Resume">
      <arg name="success" type="b" direction="out"/>
    </method>
    <method name="Stop">
      <arg name="success" type="b" direction="out"/>
    </method>
    <method name="Next">
      <arg name="success" type="b" direction="out"/>
    </method>
    <method name="Previous">
      <arg name="success" type="b" direction="out"/>
    </method>

    <!-- Automotive Telemetry (Called by CAN Process) -->
    <method name="UpdateVehicleSpeed">
      <arg name="speed_kmh" type="d" direction="in"/>
      <arg name="acknowledged" type="b" direction="out"/>
    </method>
    <method name="UpdateGear">
      <arg name="gear" type="s" direction="in"/>
      <arg name="acknowledged" type="b" direction="out"/>
    </method>

    <!-- Playback Engine Status (Called by Video Playback Process) -->
    <method name="ReportPlaybackStatus">
      <arg name="status" type="s" direction="in"/>
      <arg name="position_ms" type="x" direction="in"/>
      <arg name="duration_ms" type="x" direction="in"/>
      <arg name="acknowledged" type="b" direction="out"/>
    </method>
    <method name="ReportEndOfStream">
      <arg name="acknowledged" type="b" direction="out"/>
    </method>

    <!-- Diagnostic & Simulation (For testing USB events) -->
    <method name="SimulateUsb">
      <arg name="action" type="s" direction="in"/>
      <arg name="path" type="s" direction="in"/>
      <arg name="success" type="b" direction="out"/>
    </method>

    <!-- Outgoing Broadcast Signals -->
    <signal name="StateChanged">
      <arg name="new_state" type="s"/>
      <arg name="reason" type="s"/>
    </signal>
    <signal name="PlaylistChanged">
      <arg name="count" type="i"/>
    </signal>
    <signal name="TrackChanged">
      <arg name="track_id" type="i"/>
      <arg name="title" type="s"/>
      <arg name="path" type="s"/>
    </signal>
    <signal name="PlaybackCommand">
      <arg name="command" type="s"/>
      <arg name="arg" type="s"/>
    </signal>
    <signal name="PlaybackStatusChanged">
      <arg name="status" type="s"/>
      <arg name="position_ms" type="x"/>
      <arg name="duration_ms" type="x"/>
    </signal>
    <signal name="VehicleSpeedChanged">
      <arg name="speed_kmh" type="d"/>
      <arg name="is_restricted" type="b"/>
    </signal>
    <signal name="SafetyWarning">
      <arg name="warning_message" type="s"/>
    </signal>
  </interface>
</node>)xml";

} // namespace automotive
