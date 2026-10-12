// Linux replacement for the macOS Keychain store (freedesktop Secret Service).
#include "SecretStore.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QEventLoop>
#include <QLoggingCategory>
#include <QTimer>

Q_LOGGING_CATEGORY(lcSecret, "jot.secret")

namespace {

const char *kService = "org.freedesktop.secrets";
const char *kPath = "/org/freedesktop/secrets";
const char *kServiceIface = "org.freedesktop.Secret.Service";
const char *kAppAttr = "com.ammaar.jot.linux";

struct Secret {
    QDBusObjectPath session;
    QByteArray parameters;
    QByteArray value;
    QString contentType;
};

QDBusArgument &operator<<(QDBusArgument &arg, const Secret &s)
{
    arg.beginStructure();
    arg << s.session << s.parameters << s.value << s.contentType;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, Secret &s)
{
    arg.beginStructure();
    arg >> s.session >> s.parameters >> s.value >> s.contentType;
    arg.endStructure();
    return arg;
}

class PromptWaiter : public QObject {
    Q_OBJECT
public:
    QEventLoop loop;
    bool done = false;
    bool dismissed = true;
public slots:
    void completed(bool wasDismissed, const QDBusVariant &) { done = true; dismissed = wasDismissed; loop.quit(); }
};

using SecretMap = QMap<QDBusObjectPath, Secret>;
using StringMap = QMap<QString, QString>;

} // namespace

Q_DECLARE_METATYPE(Secret)
Q_DECLARE_METATYPE(SecretMap)
Q_DECLARE_METATYPE(StringMap)

namespace jot {

static void registerTypes()
{
    static bool done = false;
    if (done) return;
    done = true;
    qDBusRegisterMetaType<Secret>();
    qDBusRegisterMetaType<SecretMap>();
    qDBusRegisterMetaType<StringMap>();
}

SecretStore &SecretStore::instance()
{
    static SecretStore s;
    return s;
}

bool SecretStore::available()
{
    return ensureSession();
}

bool SecretStore::ensureSession()
{
    registerTypes();
    if (!m_session.isEmpty()) return true;
    QDBusInterface svc(kService, kPath, kServiceIface, QDBusConnection::sessionBus());
    if (!svc.isValid()) {
        m_lastError = QStringLiteral("Служба Secret Service недоступна (KWallet/ksecretd)");
        return false;
    }
    QDBusMessage reply = svc.call("OpenSession", QStringLiteral("plain"), QVariant::fromValue(QDBusVariant(QString())));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
        m_lastError = reply.errorMessage();
        qCWarning(lcSecret) << "OpenSession failed" << m_lastError;
        return false;
    }
    m_session = qvariant_cast<QDBusObjectPath>(reply.arguments().at(1)).path();
    return !m_session.isEmpty();
}

bool SecretStore::unlock(const QString &objectPath)
{
    QDBusInterface svc(kService, kPath, kServiceIface, QDBusConnection::sessionBus());
    QList<QDBusObjectPath> objs{QDBusObjectPath(objectPath)};
    QDBusMessage reply = svc.call("Unlock", QVariant::fromValue(objs));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) return false;
    const QString prompt = qvariant_cast<QDBusObjectPath>(reply.arguments().at(1)).path();
    if (prompt.isEmpty() || prompt == "/") return true;

    // A prompt (wallet password) is needed — run it and wait for Completed.
    PromptWaiter waiter;
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(kService, prompt, "org.freedesktop.Secret.Prompt", "Completed", &waiter,
                SLOT(completed(bool,QDBusVariant)));
    QDBusInterface promptIf(kService, prompt, "org.freedesktop.Secret.Prompt", bus);
    promptIf.call("Prompt", QString());
    QTimer::singleShot(120000, &waiter.loop, &QEventLoop::quit);
    waiter.loop.exec();
    return waiter.done && !waiter.dismissed;
}

QString SecretStore::defaultCollection()
{
    QDBusInterface svc(kService, kPath, kServiceIface, QDBusConnection::sessionBus());
    QDBusReply<QDBusObjectPath> r = svc.call("ReadAlias", QStringLiteral("default"));
    if (r.isValid() && r.value().path() != "/") return r.value().path();
    return QStringLiteral("/org/freedesktop/secrets/aliases/default");
}

QString SecretStore::load(const QString &account)
{
    if (!ensureSession()) return {};
    QDBusInterface svc(kService, kPath, kServiceIface, QDBusConnection::sessionBus());
    StringMap attrs{{"application", kAppAttr}, {"account", account}};
    QDBusMessage reply = svc.call("SearchItems", QVariant::fromValue(attrs));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
        m_lastError = reply.errorMessage();
        return {};
    }
    QList<QDBusObjectPath> unlocked = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(0));
    const QList<QDBusObjectPath> locked = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(1));
    if (unlocked.isEmpty() && !locked.isEmpty()) {
        if (!unlock(locked.first().path())) return {};
        unlocked = {locked.first()};
    }
    if (unlocked.isEmpty()) return {};
    QDBusInterface item(kService, unlocked.first().path(), "org.freedesktop.Secret.Item", QDBusConnection::sessionBus());
    QDBusMessage sr = item.call("GetSecret", QVariant::fromValue(QDBusObjectPath(m_session)));
    if (sr.type() != QDBusMessage::ReplyMessage || sr.arguments().isEmpty()) {
        m_lastError = sr.errorMessage();
        return {};
    }
    Secret secret = qdbus_cast<Secret>(sr.arguments().at(0));
    return QString::fromUtf8(secret.value).trimmed();
}

bool SecretStore::store(const QString &account, const QString &label, const QString &value)
{
    if (!ensureSession()) return false;
    const QString collection = defaultCollection();
    QDBusInterface coll(kService, collection, "org.freedesktop.Secret.Collection", QDBusConnection::sessionBus());
    if (coll.property("Locked").toBool()) unlock(collection);

    QVariantMap props;
    props.insert("org.freedesktop.Secret.Item.Label", label);
    StringMap attrs{{"application", kAppAttr}, {"account", account}};
    props.insert("org.freedesktop.Secret.Item.Attributes", QVariant::fromValue(attrs));
    Secret secret{QDBusObjectPath(m_session), {}, value.toUtf8(), QStringLiteral("text/plain")};
    QDBusMessage r = coll.call("CreateItem", props, QVariant::fromValue(secret), true);
    if (r.type() != QDBusMessage::ReplyMessage) {
        m_lastError = r.errorMessage();
        qCWarning(lcSecret) << "CreateItem failed" << m_lastError;
        return false;
    }
    if (r.arguments().size() >= 2) {
        const QString prompt = qvariant_cast<QDBusObjectPath>(r.arguments().at(1)).path();
        if (!prompt.isEmpty() && prompt != "/") {
            QDBusInterface promptIf(kService, prompt, "org.freedesktop.Secret.Prompt", QDBusConnection::sessionBus());
            promptIf.call("Prompt", QString());
        }
    }
    return true;
}

bool SecretStore::remove(const QString &account)
{
    if (!ensureSession()) return false;
    QDBusInterface svc(kService, kPath, kServiceIface, QDBusConnection::sessionBus());
    StringMap attrs{{"application", kAppAttr}, {"account", account}};
    QDBusMessage reply = svc.call("SearchItems", QVariant::fromValue(attrs));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) return false;
    QList<QDBusObjectPath> all = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(0));
    all += qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(1));
    for (const auto &p : all) {
        QDBusInterface item(kService, p.path(), "org.freedesktop.Secret.Item", QDBusConnection::sessionBus());
        item.call("Delete");
    }
    return true;
}

QString SecretStore::geminiKey()
{
    if (!m_loadedGemini) {
        m_cacheGemini = load("gemini");
        m_loadedGemini = !m_session.isEmpty();
    }
    return m_cacheGemini;
}

QString SecretStore::openRouterKey()
{
    if (!m_loadedOpenRouter) {
        m_cacheOpenRouter = load("openrouter");
        m_loadedOpenRouter = !m_session.isEmpty();
    }
    return m_cacheOpenRouter;
}

bool SecretStore::setGeminiKey(const QString &key)
{
    const QString k = key.trimmed();
    const bool ok = k.isEmpty() ? remove("gemini") : store("gemini", "Jot — Gemini API key", k);
    if (ok) { m_cacheGemini = k; m_loadedGemini = true; }
    emit keyChanged("gemini");
    return ok;
}

bool SecretStore::setOpenRouterKey(const QString &key)
{
    const QString k = key.trimmed();
    const bool ok = k.isEmpty() ? remove("openrouter") : store("openrouter", "Jot — OpenRouter API key", k);
    if (ok) { m_cacheOpenRouter = k; m_loadedOpenRouter = true; }
    emit keyChanged("openrouter");
    return ok;
}

} // namespace jot

#include "SecretStore.moc"
