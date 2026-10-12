// Integrated media pause over MPRIS.
#include "MediaPause.h"
#include "../core/Settings.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDBusObjectPath>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QTimer>

Q_LOGGING_CATEGORY(lcMedia, "jot.media")

namespace jot {

struct Owned {
    QString service;
    QString identity; // trackid + url
};

class MediaWorker : public QObject {
    Q_OBJECT
public:
    QDBusConnection bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "jot-media");
    QList<Owned> owned;
    QTimer poll;
    QTimer releaseTimer;
    bool active = false;

    QVariant prop(const QString &service, const QString &name)
    {
        QDBusMessage m = QDBusMessage::createMethodCall(service, "/org/mpris/MediaPlayer2",
                                                        "org.freedesktop.DBus.Properties", "Get");
        m << QString("org.mpris.MediaPlayer2.Player") << name;
        QDBusMessage r = bus.call(m, QDBus::Block, 2000);
        if (r.type() != QDBusMessage::ReplyMessage || r.arguments().isEmpty()) return {};
        return qvariant_cast<QDBusVariant>(r.arguments().first()).variant();
    }

    QString status(const QString &service) { return prop(service, "PlaybackStatus").toString(); }

    QString identity(const QString &service)
    {
        const QVariant v = prop(service, "Metadata");
        QVariantMap md;
        if (v.canConvert<QDBusArgument>()) qvariant_cast<QDBusArgument>(v) >> md;
        else md = v.toMap();
        QString track;
        const QVariant tid = md.value("mpris:trackid");
        if (tid.canConvert<QDBusObjectPath>()) track = qvariant_cast<QDBusObjectPath>(tid).path();
        else track = tid.toString();
        return track + "|" + md.value("xesam:url").toString() + "|" + md.value("xesam:title").toString();
    }

    void command(const QString &service, const char *method)
    {
        QDBusMessage m = QDBusMessage::createMethodCall(service, "/org/mpris/MediaPlayer2",
                                                        "org.mpris.MediaPlayer2.Player", method);
        bus.call(m, QDBus::Block, 2000);
    }

    QStringList players()
    {
        QStringList out;
        const QStringList names = bus.interface()->registeredServiceNames().value();
        for (const QString &n : names) if (n.startsWith("org.mpris.MediaPlayer2.")) out << n;
        // Plasma's browser bridge last: a browser's own MPRIS entry pauses the
        // element directly; the bridge then reads back as already paused.
        std::stable_sort(out.begin(), out.end(), [](const QString &a, const QString &b) {
            return !a.contains("plasma-browser-integration") && b.contains("plasma-browser-integration");
        });
        return out;
    }

public slots:
    void activate()
    {
        releaseTimer.stop();
        if (active) return;
        active = true;
        if (!owned.isEmpty()) return; // quick stop/start: keep ownership, no second pause
        for (const QString &p : players()) {
            if (status(p) != "Playing") continue;
            const QString id = identity(p);
            command(p, "Pause");
            QElapsedTimer t;
            t.start();
            bool confirmed = false;
            while (t.elapsed() < 500) {
                if (status(p) != "Playing") { confirmed = true; break; }
                QThread::msleep(50);
            }
            if (confirmed) {
                owned << Owned{p, id};
                qCInfo(lcMedia) << "paused" << p;
            }
        }
        if (!owned.isEmpty()) poll.start(500);
    }

    void deactivate(bool immediate)
    {
        active = false;
        if (immediate) { resumeOwned(); return; }
        releaseTimer.start(1000);
    }

    void resumeOwned()
    {
        poll.stop();
        for (const Owned &o : owned) {
            if (status(o.service) == "Paused" && identity(o.service) == o.identity) {
                command(o.service, "Play");
                qCInfo(lcMedia) << "resumed" << o.service;
            }
        }
        owned.clear();
    }

    void checkOwnership()
    {
        for (int i = int(owned.size()) - 1; i >= 0; --i) {
            const QString st = status(owned[i].service);
            // Manual playback, a changed item or a vanished player relinquishes ownership.
            if (st.isEmpty() || st == "Playing" || identity(owned[i].service) != owned[i].identity) owned.removeAt(i);
        }
        if (owned.isEmpty()) poll.stop();
    }

    void abandon()
    {
        poll.stop();
        releaseTimer.stop();
        owned.clear();
    }
};

MediaPause &MediaPause::instance()
{
    static MediaPause m;
    return m;
}

MediaPause::MediaPause()
    : m_worker(new MediaWorker)
{
    m_thread.setObjectName("jot-media");
    m_worker->moveToThread(&m_thread);
    m_worker->poll.moveToThread(&m_thread);
    m_worker->releaseTimer.moveToThread(&m_thread);
    m_worker->releaseTimer.setSingleShot(true);
    connect(&m_worker->poll, &QTimer::timeout, m_worker, &MediaWorker::checkOwnership);
    connect(&m_worker->releaseTimer, &QTimer::timeout, m_worker, &MediaWorker::resumeOwned);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
}

MediaPause::~MediaPause()
{
    m_thread.quit();
    m_thread.wait(3000);
}

void MediaPause::captureActivityChanged(bool active)
{
    if (active && !Settings::instance().pauseMediaWhileRecording()) return;
    if (active) QMetaObject::invokeMethod(m_worker, &MediaWorker::activate, Qt::QueuedConnection);
    else QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->deactivate(false); }, Qt::QueuedConnection);
}

void MediaPause::finish()
{
    QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->deactivate(true); }, Qt::BlockingQueuedConnection);
}

void MediaPause::systemSleeping()
{
    QMetaObject::invokeMethod(m_worker, &MediaWorker::abandon, Qt::QueuedConnection);
}

} // namespace jot

#include "MediaPause.moc"
