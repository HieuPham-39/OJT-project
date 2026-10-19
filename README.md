# meta-ojt: Automotive Multiprocess USB Video Player Layer

`meta-ojt` is an Automotive Embedded Linux Yocto Layer designed for high-reliability In-Vehicle Infotainment (IVI) systems. It implements a decoupled, multiprocess USB Video Player following automotive safety guidelines (ISO 26262 / NHTSA driver distraction guidelines).

---

## 1. System Architecture

The player follows a **multi-process micro-service architecture** communicating via **D-Bus IPC**:

```
+-----------------------------------------------------------------------------------+
|                                 Operating System                                  |
|                                                                                   |
|  [USB Drive Inserted] ---> Kernel Netlink ---> libudev Event Listener              |
+------------------------------------------------------+----------------------------+
                                                       |
                                                       v
+-----------------------------------------------------------------------------------+
| Process 1: ojt-media-manager (Core Service & Broker Daemon)                       |
|                                                                                   |
|  - Hardware Event Detection (libudev / netlink)                                   |
|  - Recursive Video Filter & Playlist Generator (.mp4, .mkv)                       |
|  - Deterministic Automotive Finite State Machine (FSM)                            |
|  - Central D-Bus Service Provider: `org.automotive.MediaManager`                  |
+-------------^------------------------^---------------------------------^----------+
              | (D-Bus Signals)        | (D-Bus RPC)                     | (D-Bus RPC)
              v                        v                                 v
+--------------------------+ +--------------------+ +-------------------------------+
| Process 2:               | | Process 3:         | | Process 4:                    |
| ojt-can-processor        | | ojt-video-playback | | ojt-ui-controller             |
|                          | |                    | |                               |
| - SocketCAN (can0/vcan0) | | - GStreamer 1.0    | | - IVI Console HMI Dashboard   |
| - Telematics Parser      | | - Video Rendering  | | - Real-time State & Playlist  |
| - Speed & Gear Reporter  | | - Driver Lockout   | | - User Playback Controls      |
| - Automotive Drive Cycle | | - EOS / Status     | | - Driver Distraction Alerts   |
+--------------------------+ +--------------------+ +-------------------------------+
```

---

## 2. Key Components & Requirements

### Requirement 1: Direct OS Hardware Event Listener (`libudev`)
- **Direct Netlink Monitoring**: Interacts directly with the Linux kernel netlink socket via `libudev` (`udev_monitor_new_from_netlink`).
- **USB Block Device Filtering**: Captures block subsystem events (`subsystem = "block"`, `ID_BUS = "usb"`).
- **Asynchronous Mount Detection**: Inspects mount points (`/proc/mounts`, `/media/*`, `/run/media/*`) or automatically mounts to `/media/usb_ojt`.
- **Zero-Polling Cleanup**: Instantaneous notification on physical disconnection, immediately halting media streams and triggering state transition to `NO_MEDIA`.

### Requirement 2: Video File Filter & Playlist Generator
- **Format Validation**: Recursively scans mounted directory tree using modern C++17 `std::filesystem`.
- **Format Filtering**: Case-insensitive filtering for valid video formats: `.mp4` and `.mkv`.
- **Noise Filtering**: Automatically ignores hidden files (`.filename`), macOS metadata (`._*`), and OS system directories (`System Volume Information`, `$RECYCLE.BIN`).
- **Metadata Packaging**: Extracts file size, track name, assigns contiguous track IDs, and broadcasts `PlaylistChanged(count)` over D-Bus.

### Requirement 3: D-Bus Intermediary Communication Channel
- **Service Name**: `org.automotive.MediaManager`
- **Object Path**: `/org/automotive/MediaManager`
- **Interface**: `org.automotive.MediaManager1`
- **D-Bus Methods**:
  - `GetState() -> (s)`
  - `GetPlaylist() -> (a(ist))`
  - `GetCurrentTrack() -> (iss)`
  - `Play(int32 track_id) -> (b)`
  - `Pause() -> (b)`
  - `Resume() -> (b)`
  - `Stop() -> (b)`
  - `Next() -> (b)`
  - `Previous() -> (b)`
  - `UpdateVehicleSpeed(double speed_kmh) -> (b)`
  - `UpdateGear(string gear) -> (b)`
  - `ReportPlaybackStatus(string status, int64 pos_ms, int64 dur_ms) -> (b)`
  - `ReportEndOfStream() -> (b)`
  - `SimulateUsb(string action, string path) -> (b)`
- **D-Bus Signals**:
  - `StateChanged(string new_state, string reason)`
  - `PlaylistChanged(int32 count)`
  - `TrackChanged(int32 track_id, string title, string path)`
  - `PlaybackCommand(string command, string arg)`
  - `PlaybackStatusChanged(string status, int64 pos_ms, int64 dur_ms)`
  - `VehicleSpeedChanged(double speed_kmh, bool is_restricted)`
  - `SafetyWarning(string warning_message)`

### Requirement 4: Strict Automotive Finite State Machine (FSM)
- State Transitions & Safety Guard Rules:
  - `[NO_MEDIA]` --(USB Inserted)--> `[MEDIA_DETECTED]`
  - `[MEDIA_DETECTED]` --(Scan Started)--> `[SCANNING]`
  - `[SCANNING]` --(Files Found)--> `[MEDIA_READY]`
  - `[MEDIA_READY]` --(Play & Guard Valid: Speed==0 && Gear==P)--> `[PLAYING]`
  - `[PLAYING]` --(Pause)--> `[PAUSED]`
  - `[PLAYING]` --(Speed > 0 or Gear != P)--> `[SPEED_RESTRICTED]` *(Safety Lockout)*
  - `[SPEED_RESTRICTED]` --(Speed == 0 && Gear == P)--> `[PAUSED]`
  - Any State --(USB Removed)--> `[NO_MEDIA]`
- **Driver Distraction Prevention**: Guard conditions enforce NHTSA / ISO 26262 compliance. Video frames are suspended/blanked when vehicle is in motion.

---

## 3. Layer Setup & Integration in Yocto

### Step 1: Add `meta-ojt` to `bblayers.conf`
```bash
bitbake-layers add-layer /path/to/meta-ojt
```

### Step 2: Include Recipes in your Image
In your `conf/local.conf` or image recipe:
```bitbake
IMAGE_INSTALL:append = " packagegroup-ojt-multimedia"
```
Or install the player directly:
```bitbake
IMAGE_INSTALL:append = " usb-video-player"
```

---

## 4. Testing & Running on Target

### Running Core Daemons
```bash
# Start Core Service
/usr/bin/ojt-media-manager &

# Start CAN Bus Telematics Processor (with simulation mode or physical vcan0)
/usr/bin/ojt-can-processor --simulate &

# Start Video Playback Engine
/usr/bin/ojt-video-playback &

# Launch Interactive HMI / UI Dashboard
/usr/bin/ojt-ui-controller
```

### Manual Testing with SocketCAN
```bash
# Setup virtual CAN interface
ip link add dev vcan0 type vcan
ip link set up vcan0

# Send Speed = 50.0 km/h (Raw 5000 = 0x1388)
cansend vcan0 100#1388

# Send Gear = 'P' (0x50) or 'D' (0x44)
cansend vcan0 101#44
```

---

## 5. Pure C Core Daemon: `media-manager-VerC`

A pure C (C99/C11) implementation of the core service is provided in [`media-manager-VerC`](files/media-manager-VerC/):
- **MISRA C:2012 Aligned**: Written with deterministic memory allocation and zero C++ runtime overhead.
- **Native C Libraries**: Direct usage of `libudev` and GLib/GIO `GDBus`.
- **API Compatible**: Uses the identical D-Bus interface (`org.automotive.MediaManager`), method signatures, and signal formats, acting as a direct drop-in replacement for the C++ version.
- **Selected by Default**: Integrated in `usb-video-player_1.0.bb` via `file://media-manager-VerC/`.

