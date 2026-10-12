// Linux replacement for upstream EventTapEngine.
#include "HotkeyEngine.h"
#include "../core/Settings.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDir>
#include <QGuiApplication>
#include <QFileSystemWatcher>
#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QSocketNotifier>

#include <cerrno>
#include <sys/stat.h>
#include <fcntl.h>
#include <libevdev/libevdev.h>
#include <linux/input-event-codes.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(lcHotkey, "jot.hotkey")

using PortalShortcut = QPair<QString, QVariantMap>;
using PortalShortcutList = QList<PortalShortcut>;
Q_DECLARE_METATYPE(PortalShortcutList)

namespace jot {

// ================================================================ evdev

EvdevKeyboards::EvdevKeyboards(QObject *parent)
    : QObject(parent)
{
    m_rescanDebounce.setSingleShot(true);
    m_rescanDebounce.setInterval(400);
    connect(&m_rescanDebounce, &QTimer::timeout, this, &EvdevKeyboards::rescan);
}

EvdevKeyboards::~EvdevKeyboards() { stop(); }

static bool isKeyboard(libevdev *dev)
{
    return libevdev_has_event_type(dev, EV_KEY) && libevdev_has_event_code(dev, EV_KEY, KEY_SPACE)
        && libevdev_has_event_code(dev, EV_KEY, KEY_A) && libevdev_has_event_code(dev, EV_KEY, KEY_ESC);
}

bool EvdevKeyboards::canReadAnyKeyboard()
{
    const QDir dir("/dev/input");
    for (const QString &name : dir.entryList({"event*"}, QDir::System)) {
        const QByteArray path = dir.absoluteFilePath(name).toLocal8Bit();
        const int fd = ::open(path.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        libevdev *dev = nullptr;
        bool kb = false;
        if (libevdev_new_from_fd(fd, &dev) == 0) {
            kb = isKeyboard(dev);
            libevdev_free(dev);
        }
        ::close(fd);
        if (kb) return true;
    }
    return false;
}

bool EvdevKeyboards::start()
{
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        m_watcher->addPath("/dev/input");
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_rescanDebounce.start(); });
    }
    rescan();
    return !m_devices.isEmpty();
}

void EvdevKeyboards::stop()
{
    for (auto &d : m_devices) {
        delete d.notifier;
        if (d.dev) libevdev_free(d.dev);
        if (d.fd >= 0) ::close(d.fd);
    }
    m_devices.clear();
}

void EvdevKeyboards::dropDevice(const QString &path)
{
    auto it = m_devices.find(path);
    if (it == m_devices.end()) return;
    if (it->notifier) {
        it->notifier->setEnabled(false);
        it->notifier->deleteLater();
    }
    if (it->dev) libevdev_free(it->dev);
    if (it->fd >= 0) ::close(it->fd);
    qCInfo(lcHotkey) << "keyboard gone:" << path;
    m_devices.erase(it);
}

static quint64 nodeRdev(const QString &path)
{
    struct stat st;
    return ::stat(path.toLocal8Bit().constData(), &st) == 0 ? quint64(st.st_rdev) : 0;
}

void EvdevKeyboards::rescan()
{
    const QDir dir("/dev/input");
    const QStringList names = dir.entryList({"event*"}, QDir::System);
    QSet<QString> present;
    bool denied = false;
    for (const QString &name : names) {
        const QString path = dir.absoluteFilePath(name);
        present.insert(path);
        if (m_devices.contains(path)) {
            // Same node name, different device (e.g. RustDesk's virtual keyboard re-created):
            // the old fd is dead and would spin the event loop — reopen.
            if (nodeRdev(path) == m_devices.value(path).rdev) continue;
            dropDevice(path);
        }
        const int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            if (errno == EACCES) denied = true;
            continue;
        }
        libevdev *dev = nullptr;
        if (libevdev_new_from_fd(fd, &dev) != 0 || !isKeyboard(dev)) {
            if (dev) libevdev_free(dev);
            ::close(fd);
            continue;
        }
        Device d;
        d.fd = fd;
        d.dev = dev;
        d.path = path;
        d.rdev = nodeRdev(path);
        d.notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(d.notifier, &QSocketNotifier::activated, this, [this, fd] { readDevice(fd); });
        qCInfo(lcHotkey) << "keyboard:" << libevdev_get_name(dev) << path;
        m_devices.insert(path, d);
    }
    const QStringList known = m_devices.keys();
    for (const QString &path : known)
        if (!present.contains(path)) dropDevice(path);
    m_permissionDenied = m_devices.isEmpty() && denied;
}

void EvdevKeyboards::readDevice(int fd)
{
    Device *d = nullptr;
    for (auto &dev : m_devices) if (dev.fd == fd) { d = &dev; break; }
    if (!d) return;
    input_event ev;
    int rc;
    do {
        rc = libevdev_next_event(d->dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            while (rc == LIBEVDEV_READ_STATUS_SYNC) rc = libevdev_next_event(d->dev, LIBEVDEV_READ_FLAG_SYNC, &ev);
            continue;
        }
        if (rc == LIBEVDEV_READ_STATUS_SUCCESS && ev.type == EV_KEY && ev.value != 2)
            emit key(ev.code, ev.value == 1);
    } while (rc == LIBEVDEV_READ_STATUS_SUCCESS || rc == LIBEVDEV_READ_STATUS_SYNC);
    if (rc != -EAGAIN && rc != LIBEVDEV_READ_STATUS_SUCCESS) {
        // Device unplugged or broken: stop watching NOW, otherwise the notifier
        // fires forever on the hung-up fd and pins a CPU core.
        const QString path = d->path;
        QMetaObject::invokeMethod(this, [this, path] { dropDevice(path); }, Qt::QueuedConnection);
        d->notifier->setEnabled(false);
        m_rescanDebounce.start();
    }
}

// ================================================================ portal

static const char *kPortalService = "org.freedesktop.portal.Desktop";
static const char *kPortalPath = "/org/freedesktop/portal/desktop";
static const char *kShortcutsIface = "org.freedesktop.portal.GlobalShortcuts";

PortalShortcuts::PortalShortcuts(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<PortalShortcut>();
    qDBusRegisterMetaType<PortalShortcutList>();
}

static QString requestPath(const QString &token)
{
    QString sender = QDBusConnection::sessionBus().baseService().mid(1);
    sender.replace('.', '_');
    return QString("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
}

void PortalShortcuts::start()
{
    m_token = QString("jot%1").arg(QRandomGenerator::global()->generate());
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(kPortalService, requestPath(m_token), "org.freedesktop.portal.Request", "Response", this,
                SLOT(onCreateSessionResponse(uint, QVariantMap)));
    QDBusInterface iface(kPortalService, kPortalPath, kShortcutsIface, bus);
    QVariantMap options{{"handle_token", m_token}, {"session_handle_token", QString("jotsession%1").arg(QRandomGenerator::global()->generate())}};
    QDBusMessage reply = iface.call("CreateSession", options);
    if (reply.type() == QDBusMessage::ErrorMessage) emit failed(reply.errorMessage());
}

void PortalShortcuts::onCreateSessionResponse(uint response, const QVariantMap &results)
{
    if (response != 0) { emit failed("GlobalShortcuts session refused"); return; }
    m_session = results.value("session_handle").toString();
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(kPortalService, kPortalPath, kShortcutsIface, "Activated", this,
                SLOT(onActivated(QDBusObjectPath, QString, qulonglong, QVariantMap)));
    bus.connect(kPortalService, kPortalPath, kShortcutsIface, "Deactivated", this,
                SLOT(onDeactivated(QDBusObjectPath, QString, qulonglong, QVariantMap)));

    const QString token = QString("jotbind%1").arg(QRandomGenerator::global()->generate());
    bus.connect(kPortalService, requestPath(token), "org.freedesktop.portal.Request", "Response", this,
                SLOT(onBindResponse(uint, QVariantMap)));
    PortalShortcutList shortcuts{
        {"dictate", QVariantMap{{"description", QStringLiteral("Jot: hold to dictate")}, {"preferred_trigger", QStringLiteral("CTRL+ALT+D")}}},
        {"cancel", QVariantMap{{"description", QStringLiteral("Jot: cancel dictation")}, {"preferred_trigger", QStringLiteral("CTRL+ALT+X")}}},
        {"handsfree", QVariantMap{{"description", QStringLiteral("Jot: hands-free dictation (press again to finish)")}}},
    };
    QDBusMessage msg = QDBusMessage::createMethodCall(kPortalService, kPortalPath, kShortcutsIface, "BindShortcuts");
    msg << QVariant::fromValue(QDBusObjectPath(m_session)) << QVariant::fromValue(shortcuts) << QString()
        << QVariantMap{{"handle_token", token}};
    bus.call(msg);
}

void PortalShortcuts::onBindResponse(uint response, const QVariantMap &)
{
    if (response != 0) emit failed("Shortcut binding was not confirmed");
    else qCInfo(lcHotkey) << "portal shortcuts bound";
}

void PortalShortcuts::configure()
{
    if (m_session.isEmpty()) return;
    QDBusMessage msg = QDBusMessage::createMethodCall(kPortalService, kPortalPath, kShortcutsIface, "ConfigureShortcuts");
    msg << QVariant::fromValue(QDBusObjectPath(m_session)) << QString() << QVariantMap{};
    QDBusConnection::sessionBus().call(msg, QDBus::NoBlock);
}

void PortalShortcuts::onActivated(const QDBusObjectPath &session, const QString &id, qulonglong, const QVariantMap &)
{
    if (session.path() == m_session) emit activated(id);
}

void PortalShortcuts::onDeactivated(const QDBusObjectPath &session, const QString &id, qulonglong, const QVariantMap &)
{
    if (session.path() == m_session) emit deactivated(id);
}

// ================================================================ engine

HotkeyEngine &HotkeyEngine::instance()
{
    static HotkeyEngine e;
    return e;
}

HotkeyEngine::HotkeyEngine()
{
    m_clock.start();
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] { feed(HotkeyProcessor::Event::DoubleTapTimeout); });
    connect(&Settings::instance(), &Settings::changed, this, [this](const QString &key) {
        if (key == "hotkeyCode" || key == "doubleTapLock") applySettings();
        if (key == "hotkeyBackend") start();
    });
}

bool HotkeyEngine::evdevAvailable() const { return EvdevKeyboards::canReadAnyKeyboard(); }

void HotkeyEngine::applySettings()
{
    m_hotkey = Settings::instance().hotkeyCode();
    m_processor.doubleTapLockEnabled = Settings::instance().doubleTapLockEnabled();
}

bool HotkeyEngine::start()
{
    if (QGuiApplication::platformName() == "offscreen") return false; // headless UI review
    applySettings();
    const QString pref = Settings::instance().hotkeyBackend();
    const Backend previous = m_backend;
    if (pref != "portal") {
        if (!m_evdev) {
            m_evdev = new EvdevKeyboards(this);
            connect(m_evdev, &EvdevKeyboards::key, this, &HotkeyEngine::onKey);
        }
        if (m_evdev->start()) {
            m_backend = Backend::Evdev;
            qCInfo(lcHotkey) << "hotkey backend: evdev," << m_evdev->keyboardCount() << "keyboard(s)";
            if (previous != m_backend) emit backendChanged();
            return true;
        }
        qCWarning(lcHotkey) << "evdev unavailable (permission denied:" << m_evdev->permissionDenied() << ")";
    } else if (m_evdev) {
        m_evdev->stop();
    }
    if (pref == "evdev") {
        m_backend = Backend::None;
        if (previous != m_backend) emit backendChanged();
        return false;
    }
    if (!m_portal) {
        m_portal = new PortalShortcuts(this);
        connect(m_portal, &PortalShortcuts::activated, this, [this](const QString &id) {
            if (id == "dictate") feed(HotkeyProcessor::Event::HotkeyDown);
            else if (id == "cancel") feed(HotkeyProcessor::Event::EscDown);
            else if (id == "handsfree") {
                // Press = begin + lock; second press = finalize.
                if (m_processor.phase() == HotkeyProcessor::Phase::Locked) {
                    feed(HotkeyProcessor::Event::HotkeyDown);
                } else if (m_processor.phase() == HotkeyProcessor::Phase::Idle) {
                    feed(HotkeyProcessor::Event::HotkeyDown);
                    feed(HotkeyProcessor::Event::SpaceLock);
                }
            }
        });
        connect(m_portal, &PortalShortcuts::deactivated, this, [this](const QString &id) {
            if (id == "dictate") feed(HotkeyProcessor::Event::HotkeyUp);
            else if (id == "handsfree") feed(HotkeyProcessor::Event::HotkeyUp);
        });
        connect(m_portal, &PortalShortcuts::failed, this, [](const QString &why) {
            qCWarning(lcHotkey) << "portal shortcuts failed:" << why;
        });
        m_portal->start();
    }
    m_backend = Backend::Portal;
    qCInfo(lcHotkey) << "hotkey backend: xdg GlobalShortcuts portal";
    if (previous != m_backend) emit backendChanged();
    return true;
}

bool HotkeyEngine::retryEvdev()
{
    if (QGuiApplication::platformName() == "offscreen") return false;
    if (m_backend == Backend::Evdev) return true;
    if (Settings::instance().hotkeyBackend() == "portal") return false;
    if (!m_evdev) {
        m_evdev = new EvdevKeyboards(this);
        connect(m_evdev, &EvdevKeyboards::key, this, &HotkeyEngine::onKey);
    }
    if (!m_evdev->start()) return false;
    if (m_portal) { m_portal->deleteLater(); m_portal = nullptr; }
    applySettings();
    m_backend = Backend::Evdev;
    qCInfo(lcHotkey) << "hotkey backend: evdev (access granted)";
    emit backendChanged();
    return true;
}

void HotkeyEngine::captureNextKey() { m_capturing = true; }

bool HotkeyEngine::isModifier(int code)
{
    switch (code) {
    case KEY_LEFTCTRL: case KEY_RIGHTCTRL: case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT:
    case KEY_LEFTALT: case KEY_RIGHTALT: case KEY_LEFTMETA: case KEY_RIGHTMETA:
    case KEY_CAPSLOCK: case KEY_FN: case KEY_COMPOSE:
        return true;
    default:
        return false;
    }
}

QString HotkeyEngine::keyDisplayName(int code)
{
    switch (code) {
    case KEY_RIGHTCTRL: return QCoreApplication::translate("Jot", "Right Ctrl");
    case KEY_LEFTCTRL: return QCoreApplication::translate("Jot", "Left Ctrl");
    case KEY_RIGHTALT: return QCoreApplication::translate("Jot", "Right Alt");
    case KEY_LEFTALT: return QCoreApplication::translate("Jot", "Left Alt");
    case KEY_RIGHTMETA: return QCoreApplication::translate("Jot", "Right Meta");
    case KEY_LEFTMETA: return QCoreApplication::translate("Jot", "Meta");
    case KEY_RIGHTSHIFT: return QCoreApplication::translate("Jot", "Right Shift");
    case KEY_LEFTSHIFT: return QCoreApplication::translate("Jot", "Left Shift");
    case KEY_CAPSLOCK: return QCoreApplication::translate("Jot", "Caps Lock");
    case KEY_COMPOSE: return QCoreApplication::translate("Jot", "Menu");
    case KEY_SCROLLLOCK: return QCoreApplication::translate("Jot", "Scroll Lock");
    case KEY_PAUSE: return QCoreApplication::translate("Jot", "Pause");
    case KEY_INSERT: return QCoreApplication::translate("Jot", "Insert");
    case KEY_SYSRQ: return QCoreApplication::translate("Jot", "Print Screen");
    case KEY_FN: return QCoreApplication::translate("Jot", "Fn");
    default: break;
    }
    if (code >= KEY_F1 && code <= KEY_F10) return QString("F%1").arg(code - KEY_F1 + 1);
    if (code == KEY_F11) return "F11";
    if (code == KEY_F12) return "F12";
    if (code >= KEY_F13 && code <= KEY_F24) return QString("F%1").arg(code - KEY_F13 + 13);
    const char *name = libevdev_event_code_get_name(EV_KEY, uint(code));
    if (name) {
        QString s = QString::fromLatin1(name);
        if (s.startsWith("KEY_")) s = s.mid(4);
        return s.left(1) + s.mid(1).toLower();
    }
    return QCoreApplication::translate("Jot", "Key %1").arg(code);
}

void HotkeyEngine::onKey(int code, bool down)
{
    if (m_capturing) {
        if (!down) return;
        if (code == KEY_ESC) { m_capturing = false; emit keyCaptured(-1); return; }
        m_capturing = false;
        emit keyCaptured(code);
        return;
    }
    if (down) m_down.insert(code); else m_down.remove(code);

    if (code == m_hotkey) {
        if (down && !m_hotkeyHeld) {
            m_hotkeyHeld = true;
            feed(HotkeyProcessor::Event::HotkeyDown);
        } else if (!down && m_hotkeyHeld) {
            m_hotkeyHeld = false;
            feed(HotkeyProcessor::Event::HotkeyUp);
        }
        return;
    }
    if (!down) return;
    if (code == KEY_ESC) {
        if (m_processor.isSessionActive() || m_externalSessionActive) feed(HotkeyProcessor::Event::EscDown);
        return;
    }
    if (code == KEY_SPACE && m_processor.isKeyHeld()) {
        feed(HotkeyProcessor::Event::SpaceLock);
        return;
    }
    if (isModifier(code)) return; // flagsChanged never counted as typing upstream
    if (m_processor.isSessionActive()) feed(HotkeyProcessor::Event::OtherKeyDown);
}

void HotkeyEngine::feed(HotkeyProcessor::Event e)
{
    const auto fx = m_processor.handle(e, now());
    if (fx.disarmTimer) m_timer.stop();
    if (fx.armTimer) m_timer.start(int(*fx.armTimer * 1000));
    for (HotkeyIntent i : fx.intents) emit intent(i);
}

} // namespace jot
