import QtQuick 2.15
import QtQuick.Controls 2.15
import QtMultimedia 5.15

ApplicationWindow {
    id: root
    visible: true
    width: 1024
    height: 600
    minimumWidth: 800
    minimumHeight: 480
    title: qsTr("OJT Automotive QML Video Player")
    color: "#12151A"

    // Automotive color palette
    readonly property color colBg: "#12151A"
    readonly property color colCard: "#1B2028"
    readonly property color colAccent: "#00E5FF"
    readonly property color colWarning: "#FF3D00"
    readonly property color colText: "#ECEFF1"
    readonly property color colTextSec: "#90A4AE"

    // Synchronize QML MediaPlayer with MediaController
    MediaPlayer {
        id: player
        autoPlay: true
        source: mediaController.currentPath ? ("file://" + mediaController.currentPath) : ""

        onDurationChanged: {
            if (duration > 0) {
                mediaController.durationMs = duration
            }
        }

        onPositionChanged: {
            if (playbackState === MediaPlayer.PlayingState) {
                mediaController.positionMs = position
            }
        }

        onStatusChanged: {
            if (status === MediaPlayer.EndOfMedia) {
                mediaController.next()
            }
        }
    }

    Connections {
        target: mediaController
        function onPlaybackCommandReceived(command, path, index) {
            if (command === "PLAY" || command === "RESUME") {
                player.play()
            } else if (command === "PAUSE") {
                player.pause()
            } else if (command === "STOP") {
                player.stop()
            } else if (command === "SEEK") {
                player.seek(mediaController.positionMs)
            }
        }
    }

    // ==========================================
    // 1. TOP AUTOMOTIVE STATUS HEADER
    // ==========================================
    Rectangle {
        id: topHeader
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 52
        color: colCard
        z: 10

        Row {
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12

            Text {
                text: "🚘 OJT AUTOMOTIVE IVI"
                color: colAccent
                font.bold: true
                font.pixelSize: 15
                anchors.verticalCenter: parent.verticalCenter
            }

            Rectangle {
                width: 1
                height: 24
                color: "#2C3440"
                anchors.verticalCenter: parent.verticalCenter
            }

            // USB Status Pill
            Rectangle {
                height: 28
                width: usbLabel.implicitWidth + 36
                radius: 14
                anchors.verticalCenter: parent.verticalCenter
                color: mediaController.isUsbConnected ? "#0D3338" : "#2E1C1C"
                border.color: mediaController.isUsbConnected ? colAccent : colWarning
                border.width: 1

                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    Text {
                        text: mediaController.isUsbConnected ? "💾" : "⚠️"
                        font.pixelSize: 12
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        id: usbLabel
                        text: mediaController.isUsbConnected ? "USB MOUNTED" : "NO USB"
                        color: mediaController.isUsbConnected ? colAccent : colWarning
                        font.bold: true
                        font.pixelSize: 11
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            // CAN Telemetry Pill
            Rectangle {
                height: 28
                width: canLabel.implicitWidth + 20
                radius: 14
                anchors.verticalCenter: parent.verticalCenter
                color: mediaController.isSpeedRestricted ? "#3E1B1B" : "#1B2A32"
                border.color: mediaController.isSpeedRestricted ? colWarning : "#455A64"
                border.width: 1

                Text {
                    id: canLabel
                    anchors.centerIn: parent
                    text: qsTr("CAN: %1 km/h | GEAR [%2]").arg(mediaController.speed.toFixed(1)).arg(mediaController.gear)
                    color: mediaController.isSpeedRestricted ? colWarning : "#B0BEC5"
                    font.bold: true
                    font.pixelSize: 11
                }
            }

            // FSM State Badge
            Rectangle {
                height: 28
                width: fsmLabel.implicitWidth + 20
                radius: 14
                anchors.verticalCenter: parent.verticalCenter
                color: {
                    if (mediaController.state === "PLAYING") return "#1B3A2B";
                    if (mediaController.state === "SPEED_RESTRICTED") return "#4A1212";
                    if (mediaController.state === "PAUSED") return "#3A351B";
                    return "#263238";
                }
                border.color: {
                    if (mediaController.state === "PLAYING") return "#00E676";
                    if (mediaController.state === "SPEED_RESTRICTED") return colWarning;
                    if (mediaController.state === "PAUSED") return "#FFD600";
                    return "#78909C";
                }
                border.width: 1

                Text {
                    id: fsmLabel
                    anchors.centerIn: parent
                    text: qsTr("FSM: %1").arg(mediaController.state)
                    color: "#FFFFFF"
                    font.bold: true
                    font.pixelSize: 11
                }
            }
        }

        // Host Simulation Drawer Toggle Button
        Button {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: simDrawer.visible ? "Hide Testbench" : "🧪 CAN / Host Testbench"
            onClicked: simDrawer.visible = !simDrawer.visible
            background: Rectangle {
                color: simDrawer.visible ? colAccent : "#263238"
                radius: 6
            }
            contentItem: Text {
                text: parent.text
                color: simDrawer.visible ? "#000000" : "#ECEFF1"
                font.bold: true
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    // ==========================================
    // 2. MAIN CENTER AREA: VIDEO + PLAYLIST
    // ==========================================
    Item {
        id: centerArea
        anchors.top: topHeader.bottom
        anchors.bottom: bottomBar.top
        anchors.left: parent.left
        anchors.right: parent.right

        // Right Area: Playlist Panel
        Rectangle {
            id: playlistPanel
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            width: 320
            color: colCard

            // Playlist Header
            Rectangle {
                id: playlistHeader
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 44
                color: "#161B22"

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("PLAYLIST (%1)").arg(mediaController.playlist.length)
                    color: colText
                    font.bold: true
                    font.pixelSize: 13
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: ".mp4 / .mkv"
                    color: colTextSec
                    font.pixelSize: 11
                }
            }

            // Playlist Items List
            ListView {
                id: playlistView
                anchors.top: playlistHeader.bottom
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                clip: true
                model: mediaController.playlist

                delegate: Rectangle {
                    width: playlistView.width
                    height: 56
                    color: {
                        if (index === mediaController.currentIndex) return "#1A303A";
                        if (itemMouse.hovered) return "#222A35";
                        return index % 2 === 0 ? "#1B2028" : "#171C23";
                    }
                    border.color: index === mediaController.currentIndex ? colAccent : "transparent"
                    border.width: index === mediaController.currentIndex ? 1 : 0

                    MouseArea {
                        id: itemMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: mediaController.play(index)
                    }

                    Item {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12

                        Text {
                            id: trackNumber
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 20
                            text: index === mediaController.currentIndex ? "▶" : (index + 1)
                            color: index === mediaController.currentIndex ? colAccent : colTextSec
                            font.bold: true
                            font.pixelSize: 13
                        }

                        Column {
                            anchors.left: trackNumber.right
                            anchors.leftMargin: 10
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2

                            Text {
                                text: modelData.title
                                color: index === mediaController.currentIndex ? "#FFFFFF" : colText
                                font.bold: index === mediaController.currentIndex
                                font.pixelSize: 13
                                elide: Text.ElideRight
                                width: parent.width
                            }

                            Text {
                                text: modelData.size || "Video"
                                color: colTextSec
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }
        }

        // Left Area: Video Display Canvas
        Rectangle {
            id: videoArea
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: playlistPanel.left
            color: "#080A0D"
            clip: true

            VideoOutput {
                id: videoOutput
                anchors.fill: parent
                source: player
                visible: !mediaController.isSpeedRestricted && mediaController.state !== "NO_MEDIA"
            }

            // Active track banner on top of video
            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 40
                color: "#A0000000"
                visible: mediaController.currentTitle !== "" && !mediaController.isSpeedRestricted

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: "▶ NOW PLAYING: " + mediaController.currentTitle
                    color: "#FFFFFF"
                    font.bold: true
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
            }

            // --- SPEED RESTRICTION SAFETY OVERLAY ---
            Rectangle {
                anchors.fill: parent
                color: "#E60A0D14"
                visible: mediaController.isSpeedRestricted

                Column {
                    anchors.centerIn: parent
                    spacing: 16

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "🛑"
                        font.pixelSize: 56
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "DRIVER DISTRACTION LOCKOUT ACTIVE"
                        color: colWarning
                        font.bold: true
                        font.pixelSize: 20
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("Vehicle speed is %1 km/h (Gear: %2)\nVideo rendering is inhibited while the vehicle is in motion.\nAudio continues playing.").arg(mediaController.speed.toFixed(1)).arg(mediaController.gear)
                        color: colText
                        font.pixelSize: 14
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 260
                        height: 36
                        radius: 18
                        color: "#2C1212"
                        border.color: colWarning
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "Shift to PARK & Stop to Resume"
                            color: "#FF8A80"
                            font.bold: true
                            font.pixelSize: 12
                        }
                    }
                }
            }

            // --- NO MEDIA OVERLAY ---
            Rectangle {
                anchors.fill: parent
                color: "#080A0D"
                visible: mediaController.state === "NO_MEDIA" || mediaController.playlist.length === 0

                Column {
                    anchors.centerIn: parent
                    spacing: 14

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "📁"
                        font.pixelSize: 48
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "NO USB MEDIA DETECTED"
                        color: colTextSec
                        font.bold: true
                        font.pixelSize: 18
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "Please insert a USB flash drive containing .mp4 or .mkv files.\nOr click 'CAN / Host Testbench' on top right to inject sample media."
                        color: "#607D8B"
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }
    }

    // ==========================================
    // 3. BOTTOM CONTROL BAR (Play/Pause/Seek)
    // ==========================================
    Rectangle {
        id: bottomBar
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 88
        color: colCard
        z: 10

        Column {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.topMargin: 8
            anchors.bottomMargin: 8
            spacing: 4

            // Seek Slider & Progress Bar
            Item {
                width: parent.width
                height: 32

                Text {
                    id: timeLabelLeft
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: mediaController.formatTime(mediaController.positionMs)
                    color: colTextSec
                    font.pixelSize: 12
                    font.family: "Monospace"
                }

                Slider {
                    id: seekSlider
                    anchors.left: timeLabelLeft.right
                    anchors.leftMargin: 12
                    anchors.right: timeLabelRight.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    from: 0
                    to: mediaController.durationMs > 0 ? mediaController.durationMs : 100
                    value: mediaController.positionMs
                    live: false

                    onMoved: {
                        mediaController.seekAbsolute(value)
                        if (player.seekable) {
                            player.seek(value)
                        }
                    }

                    background: Rectangle {
                        x: seekSlider.leftPadding
                        y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
                        implicitWidth: 200
                        implicitHeight: 6
                        width: seekSlider.availableWidth
                        height: implicitHeight
                        radius: 3
                        color: "#2C3440"

                        Rectangle {
                            width: seekSlider.visualPosition * parent.width
                            height: parent.height
                            color: colAccent
                            radius: 3
                        }
                    }

                    handle: Rectangle {
                        x: seekSlider.leftPadding + seekSlider.visualPosition * (seekSlider.availableWidth - width)
                        y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
                        implicitWidth: 16
                        implicitHeight: 16
                        radius: 8
                        color: "#FFFFFF"
                        border.color: colAccent
                        border.width: 2
                    }
                }

                Text {
                    id: timeLabelRight
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: mediaController.formatTime(mediaController.durationMs)
                    color: colTextSec
                    font.pixelSize: 12
                    font.family: "Monospace"
                }
            }

            // Control Buttons: Prev, Rewind, Play/Pause, Fast-Forward, Next, Stop
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 16

                // Previous Track (⏮)
                RoundButton {
                    text: "⏮"
                    font.pixelSize: 16
                    width: 42
                    height: 42
                    onClicked: mediaController.previous()
                }

                // Fast Backward / Rewind 10s (◀◀)
                RoundButton {
                    text: "◀◀"
                    font.pixelSize: 14
                    width: 42
                    height: 42
                    onClicked: {
                        mediaController.seekRelative(-10)
                        if (player.seekable) {
                            player.seek(mediaController.positionMs)
                        }
                    }
                }

                // Play / Pause Main Button (▶ / ⏸)
                RoundButton {
                    text: mediaController.state === "PLAYING" ? "⏸" : "▶"
                    font.pixelSize: 20
                    width: 54
                    height: 54
                    palette.button: colAccent
                    onClicked: mediaController.togglePlayPause()
                }

                // Fast Forward 10s (▶▶)
                RoundButton {
                    text: "▶▶"
                    font.pixelSize: 14
                    width: 42
                    height: 42
                    onClicked: {
                        mediaController.seekRelative(10)
                        if (player.seekable) {
                            player.seek(mediaController.positionMs)
                        }
                    }
                }

                // Next Track (⏭)
                RoundButton {
                    text: "⏭"
                    font.pixelSize: 16
                    width: 42
                    height: 42
                    onClicked: mediaController.next()
                }

                // Stop Button (⏹)
                RoundButton {
                    text: "⏹"
                    font.pixelSize: 14
                    width: 42
                    height: 42
                    onClicked: {
                        player.stop()
                        mediaController.stop()
                    }
                }
            }
        }
    }

    // ==========================================
    // 4. HOST SIMULATION TESTBENCH PANEL
    // ==========================================
    Drawer {
        id: simDrawer
        width: 380
        height: root.height
        edge: Qt.RightEdge
        visible: false

        background: Rectangle {
            color: "#161C24"
            border.color: colAccent
            border.width: 1
        }

        Column {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12

            Text {
                text: "🧪 CAN & HOST TESTBENCH"
                color: colAccent
                font.bold: true
                font.pixelSize: 16
            }

            Text {
                text: "Test vehicle speed lockout and USB events directly on your PC."
                color: colTextSec
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                width: parent.width
            }

            Rectangle { width: parent.width; height: 1; color: "#2C3440" }

            // CAN Vehicle Speed Simulator
            Text { text: "1. Vehicle Speed (CAN 0x100):"; color: colText; font.bold: true; font.pixelSize: 13 }
            Row {
                spacing: 8
                Button {
                    text: "0 km/h (Park)"
                    onClicked: {
                        mediaController.setVehicleSpeed(0.0)
                        mediaController.setVehicleGear("P")
                    }
                }
                Button {
                    text: "25 km/h"
                    onClicked: {
                        mediaController.setVehicleGear("D")
                        mediaController.setVehicleSpeed(25.0)
                    }
                }
                Button {
                    text: "60 km/h"
                    onClicked: {
                        mediaController.setVehicleGear("D")
                        mediaController.setVehicleSpeed(60.0)
                    }
                }
            }

            // CAN Gear Simulator
            Text { text: "2. Vehicle Gear (CAN 0x101):"; color: colText; font.bold: true; font.pixelSize: 13 }
            Row {
                spacing: 8
                Button { text: "P (Park)"; onClicked: mediaController.setVehicleGear("P") }
                Button { text: "D (Drive)"; onClicked: mediaController.setVehicleGear("D") }
                Button { text: "R (Reverse)"; onClicked: mediaController.setVehicleGear("R") }
            }

            Rectangle { width: parent.width; height: 1; color: "#2C3440" }

            // USB Simulation
            Text { text: "3. USB Hardware Hotplug:"; color: colText; font.bold: true; font.pixelSize: 13 }
            Row {
                spacing: 8
                Button {
                    text: "Insert USB (Simulate)"
                    onClicked: mediaController.triggerUsbSimulation(true)
                }
                Button {
                    text: "Unplug USB"
                    onClicked: mediaController.triggerUsbSimulation(false)
                }
            }

            Rectangle { width: parent.width; height: 1; color: "#2C3440" }

            Text {
                text: qsTr("Status: %1").arg(mediaController.statusMessage)
                color: "#78909C"
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                width: parent.width
            }
        }
    }
}
