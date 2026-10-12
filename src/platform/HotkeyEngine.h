// Linux replacement for upstream EventTapEngine.
//
// Wayland gives applications no global key events, so there are two backends:
//  * evdev  — reads /dev/input/event* keyboards directly (needs read access,
//             granted once by scripts/setup-permissions.sh via a udev uaccess
//             rule). Full grammar: any key incl. a bare modifier, hold, Space-lock,
//             Esc cancel, accidental-chord detection. Events are observed, not
//             grabbed, so Space/Esc also reach the focused app.
//  * portal — org.freedesktop.portal.GlobalShortcuts (KDE: press + release).
//             Works without permissions; the key is chosen in System Settings,
//             a second shortcut cancels.
#pragma once

#include "../core/HotkeyProcessor.h"
#include <QDBusObjectPath>
#include <QElapsedTimer>
#include <QVariantMap>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>

class QSocketNotifier;
class QFileSystemWatcher;
struct libevdev;

namespace jot {

class EvdevKeyboards : public QObject {
    Q_OBJECT
public:
    explicit EvdevKeyboards(QObject *parent = nullptr);
    ~EvdevKeyboards() override;
    bool start();
    void stop();
    int keyboardCount() const { return int(m_devices.size()); }
    bool permissionDenied() const { return m_permissionDenied; }
    static bool canReadAnyKeyboard();

signals:
    void key(int code, bool down);

private:
    void rescan();
    void readDevice(int fd);
    void dropDevice(const QString &path);
    struct Device {
        int fd = -1;
        libevdev *dev = nullptr;
        QSocketNotifier *notifier = nullptr;
        QString path;
        quint64 rdev = 0; // device number, to notice a node reused by a new device
    };
    QHash<QString, Device> m_devices;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer m_rescanDebounce;
    bool m_permissionDenied = false;
};

class PortalShortcuts : public QObject {
    Q_OBJECT
public:
    explicit PortalShortcuts(QObject *parent = nullptr);
    void start();
    bool isActive() const { return !m_session.isEmpty(); }
    void configure(); // opens the system dialog to (re)bind

signals:
    void activated(const QString &id);
    void deactivated(const QString &id);
    void failed(const QString &why);

private slots:
    void onCreateSessionResponse(uint response, const QVariantMap &results);
    void onBindResponse(uint response, const QVariantMap &results);
    void onActivated(const QDBusObjectPath &session, const QString &id, qulonglong ts, const QVariantMap &opts);
    void onDeactivated(const QDBusObjectPath &session, const QString &id, qulonglong ts, const QVariantMap &opts);

private:
    QString m_session;
    QString m_token;
};

/// Feeds the pure HotkeyProcessor from whichever backend is live and emits
/// intents. Also offers a "capture next key" mode for the settings UI.
class HotkeyEngine : public QObject {
    Q_OBJECT
public:
    static HotkeyEngine &instance();

    enum class Backend { None, Evdev, Portal };
    Q_ENUM(Backend)

    bool start();
    /// Re-try direct keyboard access only (never falls back to the portal).
    bool retryEvdev();
    Backend backend() const { return m_backend; }
    bool evdevAvailable() const;
    void applySettings();
    /// The coordinator refused a begin — snap the grammar back to idle.
    void resetGrammar() { m_processor.reset(); m_timer.stop(); }
    /// Esc is only meaningful while a session is in flight (upstream "external session").
    void setExternalSessionActive(bool active) { m_externalSessionActive = active; }
    void captureNextKey(); // emits keyCaptured(code)
    void cancelCapture() { m_capturing = false; }

    static QString keyDisplayName(int code);
    static bool isModifier(int code);

signals:
    void intent(jot::HotkeyIntent intent);
    void keyCaptured(int code);
    void backendChanged();

private:
    HotkeyEngine();
    void onKey(int code, bool down);
    void feed(HotkeyProcessor::Event e);
    double now() const { return m_clock.elapsed() / 1000.0; }

    EvdevKeyboards *m_evdev = nullptr;
    PortalShortcuts *m_portal = nullptr;
    HotkeyProcessor m_processor;
    QTimer m_timer;
    QElapsedTimer m_clock;
    Backend m_backend = Backend::None;
    int m_hotkey = 97;
    bool m_hotkeyHeld = false;
    bool m_externalSessionActive = false;
    bool m_capturing = false;
    QSet<int> m_down;
};

} // namespace jot
