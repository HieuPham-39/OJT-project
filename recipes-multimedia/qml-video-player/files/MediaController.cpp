#include "MediaController.hpp"

MediaController::MediaController(QObject* parent)
    : QObject(parent),
      m_progressTimer(new QTimer(this)),
      m_dbusRetryTimer(new QTimer(this))
{
    connect(m_progressTimer, &QTimer::timeout, this, &MediaController::onLocalProgressTimer);
    m_progressTimer->setInterval(500);

    // Initial check for media directory
    scanLocalDirectory("/media/usb_ojt");
    if (m_playlist.isEmpty()) {
        scanLocalDirectory("./sample_videos");
    }
    if (m_playlist.isEmpty()) {
        scanLocalDirectory("/tmp/usb_test");
    }

    if (m_playlist.isEmpty()) {
        populateSamplePlaylist();
    }

    // Try connecting to automotive D-Bus
    connectToDbus();
}

void MediaController::connectToDbus() {
#ifdef QT_DBUS_LIB
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        bus = QDBusConnection::sessionBus();
    }

    if (bus.isConnected()) {
        m_isDbusConnected = true;
        emit dbusStatusChanged(true);
        m_statusMessage = "Connected to Automotive D-Bus System Bus";
        emit statusMessageChanged(m_statusMessage);

        // Subscribe to StateChanged
        bus.connect("org.automotive.MediaManager", "/org/automotive/MediaManager",
                    "org.automotive.MediaManager1", "StateChanged",
                    this, SLOT(onDbusStateChanged(QString,QString)));

        // Subscribe to PlaylistChanged
        bus.connect("org.automotive.MediaManager", "/org/automotive/MediaManager",
                    "org.automotive.MediaManager1", "PlaylistChanged",
                    this, SLOT(onDbusPlaylistChanged(int)));

        // Subscribe to TrackChanged
        bus.connect("org.automotive.MediaManager", "/org/automotive/MediaManager",
                    "org.automotive.MediaManager1", "TrackChanged",
                    this, SLOT(onDbusTrackChanged(int,QString,QString)));

        // Subscribe to PlaybackCommand
        bus.connect("org.automotive.MediaManager", "/org/automotive/MediaManager",
                    "org.automotive.MediaManager1", "PlaybackCommand",
                    this, SLOT(onDbusPlaybackCommand(QString,QString,int)));

        // Subscribe to SafetyWarning
        bus.connect("org.automotive.MediaManager", "/org/automotive/MediaManager",
                    "org.automotive.MediaManager1", "SafetyWarning",
                    this, SLOT(onDbusSafetyWarning(QString)));
    } else {
        m_isDbusConnected = false;
        emit dbusStatusChanged(false);
        m_statusMessage = "Host Standalone Mode (D-Bus disconnected)";
        emit statusMessageChanged(m_statusMessage);
    }
#else
    m_isDbusConnected = false;
    emit dbusStatusChanged(false);
    m_statusMessage = "Host Standalone Mode (Direct Player)";
    emit statusMessageChanged(m_statusMessage);
#endif
}

void MediaController::populateSamplePlaylist() {
    m_playlist.clear();

    QVariantMap t1;
    t1["id"] = 0;
    t1["title"] = "demo_01_adas_overview.mp4";
    t1["path"] = "./sample_videos/demo_01_adas_overview.mp4";
    t1["size"] = "14.2 MB";
    m_playlist.append(t1);

    QVariantMap t2;
    t2["id"] = 1;
    t2["title"] = "demo_02_can_telematics.mp4";
    t2["path"] = "./sample_videos/demo_02_can_telematics.mp4";
    t2["size"] = "22.8 MB";
    m_playlist.append(t2);

    QVariantMap t3;
    t3["id"] = 2;
    t3["title"] = "demo_03_cockpit_hmi.mkv";
    t3["path"] = "./sample_videos/demo_03_cockpit_hmi.mkv";
    t3["size"] = "38.5 MB";
    m_playlist.append(t3);

    m_isUsbConnected = true;
    emit usbStatusChanged(true);
    emit playlistChanged();

    updateFsmState("MEDIA_READY", "Local playlist initialized");
}

void MediaController::scanLocalDirectory(const QString& dirPath) {
    QDir dir(dirPath);
    if (!dir.exists()) return;

    QStringList filters;
    filters << "*.mp4" << "*.mkv" << "*.MP4" << "*.MKV" << "*.avi" << "*.webm";
    dir.setNameFilters(filters);
    dir.setFilter(QDir::Files | QDir::NoDotAndDotDot | QDir::Readable);

    QFileInfoList list = dir.entryInfoList();
    if (list.isEmpty()) return;

    m_playlist.clear();
    int id = 0;
    for (const QFileInfo& fi : list) {
        QVariantMap track;
        track["id"] = id++;
        track["title"] = fi.fileName();
        track["path"] = fi.absoluteFilePath();
        double mb = fi.size() / (1024.0 * 1024.0);
        track["size"] = QString::number(mb, 'f', 1) + " MB";
        m_playlist.append(track);
    }

    m_isUsbConnected = true;
    emit usbStatusChanged(true);
    emit playlistChanged();

    updateFsmState("MEDIA_READY", QString("Scanned %1 video files from %2").arg(m_playlist.size()).arg(dirPath));
}

void MediaController::play(int index) {
    if (m_isSpeedRestricted) {
        m_statusMessage = "Cannot start playback: Vehicle is in motion (Speed > 0 or Gear != P)";
        emit statusMessageChanged(m_statusMessage);
        return;
    }

    if (m_playlist.isEmpty()) {
        m_statusMessage = "No media available to play";
        emit statusMessageChanged(m_statusMessage);
        return;
    }

    if (index >= 0 && index < m_playlist.size()) {
        m_currentIndex = index;
    } else if (m_currentIndex < 0) {
        m_currentIndex = 0;
    }

    QVariantMap currentTrack = m_playlist.at(m_currentIndex).toMap();
    m_currentTitle = currentTrack.value("title").toString();
    m_currentPath = currentTrack.value("path").toString();
    m_positionMs = 0;
    m_durationMs = 180000; // Default simulated duration (3 min) if unknown

    emit currentTrackChanged(m_currentIndex, m_currentTitle, m_currentPath);
    emit positionChanged(m_positionMs);
    emit durationChanged(m_durationMs);

    updateFsmState("PLAYING", "Playback requested");
    m_progressTimer->start();

    emit playbackCommandReceived("PLAY", m_currentPath, m_currentIndex);
}

void MediaController::pause() {
    if (m_state != "PLAYING") return;
    updateFsmState("PAUSED", "User paused playback");
    m_progressTimer->stop();
    emit playbackCommandReceived("PAUSE", m_currentPath, m_currentIndex);
}

void MediaController::resume() {
    if (m_isSpeedRestricted) {
        m_statusMessage = "Cannot resume: Vehicle moving or out of Park";
        emit statusMessageChanged(m_statusMessage);
        return;
    }
    if (m_state == "PAUSED") {
        updateFsmState("PLAYING", "User resumed playback");
        m_progressTimer->start();
        emit playbackCommandReceived("RESUME", m_currentPath, m_currentIndex);
    }
}

void MediaController::togglePlayPause() {
    if (m_state == "PLAYING") {
        pause();
    } else {
        if (m_state == "PAUSED") {
            resume();
        } else {
            play(m_currentIndex);
        }
    }
}

void MediaController::stop() {
    updateFsmState("MEDIA_READY", "Playback stopped");
    m_progressTimer->stop();
    m_positionMs = 0;
    emit positionChanged(m_positionMs);
    emit playbackCommandReceived("STOP", m_currentPath, m_currentIndex);
}

void MediaController::next() {
    if (m_playlist.isEmpty()) return;
    int nextIdx = (m_currentIndex + 1) % m_playlist.size();
    play(nextIdx);
}

void MediaController::previous() {
    if (m_playlist.isEmpty()) return;
    int prevIdx = (m_currentIndex - 1 + m_playlist.size()) % m_playlist.size();
    play(prevIdx);
}

void MediaController::seekRelative(int seconds) {
    qint64 target = m_positionMs + (seconds * 1000);
    seekAbsolute(target);
}

void MediaController::seekAbsolute(qint64 positionMs) {
    if (positionMs < 0) positionMs = 0;
    if (m_durationMs > 0 && positionMs > m_durationMs) positionMs = m_durationMs;

    m_positionMs = positionMs;
    emit positionChanged(m_positionMs);

    m_statusMessage = QString("Seek to %1").arg(formatTime(m_positionMs));
    emit statusMessageChanged(m_statusMessage);

    emit playbackCommandReceived("SEEK", QString::number(m_positionMs / 1000), m_currentIndex);
}

void MediaController::setPositionMs(qint64 pos) {
    if (m_positionMs != pos) {
        m_positionMs = pos;
        emit positionChanged(m_positionMs);
    }
}

void MediaController::setDurationMs(qint64 dur) {
    if (m_durationMs != dur && dur > 0) {
        m_durationMs = dur;
        emit durationChanged(m_durationMs);
    }
}

void MediaController::setVehicleSpeed(double speedKmh) {
    m_speed = speedKmh < 0.0 ? 0.0 : speedKmh;
    emit telemetryChanged(m_speed, m_gear);
    checkSafetyGuard();
}

void MediaController::setVehicleGear(const QString& gear) {
    m_gear = gear.toUpper();
    emit telemetryChanged(m_speed, m_gear);
    checkSafetyGuard();
}

void MediaController::checkSafetyGuard() {
    bool shouldRestrict = (m_speed > 0.0) || (m_gear != "P" && m_gear != "PARK");

    if (shouldRestrict != m_isSpeedRestricted) {
        m_isSpeedRestricted = shouldRestrict;
        emit safetyLockoutChanged(m_isSpeedRestricted);

        if (m_isSpeedRestricted) {
            if (m_state == "PLAYING") {
                updateFsmState("SPEED_RESTRICTED", QString("Vehicle active: Speed=%1 km/h, Gear=%2").arg(m_speed).arg(m_gear));
                m_progressTimer->stop();
                emit playbackCommandReceived("PAUSE", m_currentPath, m_currentIndex);
            }
        } else {
            if (m_state == "SPEED_RESTRICTED") {
                updateFsmState("PAUSED", "Vehicle stationary in Park: Restriction cleared");
            }
        }
    }
}

void MediaController::triggerUsbSimulation(bool inserted, const QString& path) {
    m_isUsbConnected = inserted;
    emit usbStatusChanged(m_isUsbConnected);

    if (inserted) {
        m_statusMessage = "USB Inserted: Scanning media...";
        emit statusMessageChanged(m_statusMessage);
        if (!path.isEmpty()) {
            scanLocalDirectory(path);
        } else {
            populateSamplePlaylist();
        }
    } else {
        stop();
        m_playlist.clear();
        emit playlistChanged();
        m_currentTitle = "";
        m_currentPath = "";
        m_currentIndex = -1;
        emit currentTrackChanged(-1, "", "");
        updateFsmState("NO_MEDIA", "USB Hardware unplugged");
    }
}

void MediaController::updateFsmState(const QString& newState, const QString& reason) {
    if (m_state != newState) {
        m_state = newState;
        m_statusMessage = reason.isEmpty() ? QString("State: %1").arg(m_state) : QString("[%1] %2").arg(m_state).arg(reason);
        emit stateChanged(m_state);
        emit statusMessageChanged(m_statusMessage);
        qDebug() << "[FSM]" << m_state << ":" << reason;
    }
}

void MediaController::onLocalProgressTimer() {
    if (m_state == "PLAYING") {
        m_positionMs += 500;
        if (m_durationMs > 0 && m_positionMs >= m_durationMs) {
            m_positionMs = m_durationMs;
            emit positionChanged(m_positionMs);
            m_progressTimer->stop();
            // Auto advance to next track
            next();
            return;
        }
        emit positionChanged(m_positionMs);
    }
}

QString MediaController::formatTime(qint64 ms) const {
    qint64 totalSec = ms / 1000;
    qint64 min = totalSec / 60;
    qint64 sec = totalSec % 60;
    return QString("%1:%2").arg(min, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
}
