#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

#ifdef QT_DBUS_LIB
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#endif

class MediaController : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentTrackChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentTrackChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentTrackChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs WRITE setPositionMs NOTIFY positionChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs WRITE setDurationMs NOTIFY durationChanged)
    Q_PROPERTY(double speed READ speed NOTIFY telemetryChanged)
    Q_PROPERTY(QString gear READ gear NOTIFY telemetryChanged)
    Q_PROPERTY(bool isSpeedRestricted READ isSpeedRestricted NOTIFY safetyLockoutChanged)
    Q_PROPERTY(bool isUsbConnected READ isUsbConnected NOTIFY usbStatusChanged)
    Q_PROPERTY(QVariantList playlist READ playlist NOTIFY playlistChanged)
    Q_PROPERTY(bool isDbusConnected READ isDbusConnected NOTIFY dbusStatusChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    explicit MediaController(QObject* parent = nullptr);
    virtual ~MediaController() = default;

    QString state() const { return m_state; }
    QString currentTitle() const { return m_currentTitle; }
    QString currentPath() const { return m_currentPath; }
    int currentIndex() const { return m_currentIndex; }
    qint64 positionMs() const { return m_positionMs; }
    qint64 durationMs() const { return m_durationMs; }
    double speed() const { return m_speed; }
    QString gear() const { return m_gear; }
    bool isSpeedRestricted() const { return m_isSpeedRestricted; }
    bool isUsbConnected() const { return m_isUsbConnected; }
    QVariantList playlist() const { return m_playlist; }
    bool isDbusConnected() const { return m_isDbusConnected; }
    QString statusMessage() const { return m_statusMessage; }

    void setPositionMs(qint64 pos);
    void setDurationMs(qint64 dur);

public slots:
    // Core Playback Controls (Touch / Mouse / CAN dispatch)
    void play(int index = -1);
    void pause();
    void resume();
    void togglePlayPause();
    void stop();
    void next();
    void previous();
    void seekRelative(int seconds);
    void seekAbsolute(qint64 positionMs);

    // Automotive Telemetry & Drive Cycle Simulation
    void setVehicleSpeed(double speedKmh);
    void setVehicleGear(const QString& gear);
    void triggerUsbSimulation(bool inserted, const QString& path = QString());

    // Host Testing & Local Media
    void scanLocalDirectory(const QString& dirPath);
    QString formatTime(qint64 ms) const;

signals:
    void stateChanged(const QString& state);
    void currentTrackChanged(int index, const QString& title, const QString& path);
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    void telemetryChanged(double speed, const QString& gear);
    void safetyLockoutChanged(bool restricted);
    void usbStatusChanged(bool connected);
    void playlistChanged();
    void dbusStatusChanged(bool connected);
    void statusMessageChanged(const QString& message);
    void playbackCommandReceived(const QString& command, const QString& path, int index);

private slots:
    void onLocalProgressTimer();
    void connectToDbus();

private:
    void checkSafetyGuard();
    void updateFsmState(const QString& newState, const QString& reason = QString());
    void populateSamplePlaylist();

    QString m_state{"NO_MEDIA"};
    QString m_currentTitle{""};
    QString m_currentPath{""};
    int m_currentIndex{-1};
    qint64 m_positionMs{0};
    qint64 m_durationMs{0};
    double m_speed{0.0};
    QString m_gear{"P"};
    bool m_isSpeedRestricted{false};
    bool m_isUsbConnected{false};
    bool m_isDbusConnected{false};
    QString m_statusMessage{"Initializing automotive player..."};
    QVariantList m_playlist;

    QTimer* m_progressTimer{nullptr};
    QTimer* m_dbusRetryTimer{nullptr};
};
