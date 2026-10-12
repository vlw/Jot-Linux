// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) to Linux/Qt.
#include "Settings.h"
#include "SecretStore.h"

#include <QUrl>

namespace jot {

Settings &Settings::instance()
{
    static Settings s;
    return s;
}

Settings::Settings(QObject *parent)
    : QObject(parent)
    , m_settings(QSettings::IniFormat, QSettings::UserScope, "jot-linux", "Jot")
{
}

QVariant Settings::value(const QString &key, const QVariant &def) const
{
    return m_settings.value(key, def);
}

void Settings::set(const QString &key, const QVariant &v)
{
    if (!v.isValid() || (v.typeId() == QMetaType::QString && v.toString().isNull()))
        m_settings.remove(key);
    else
        m_settings.setValue(key, v);
    m_settings.sync();
    emit changed(key);
}

void Settings::remove(const QString &key)
{
    m_settings.remove(key);
    m_settings.sync();
    emit changed(key);
}

TranscriptionProvider Settings::transcriptionProvider() const
{
    return value("transcriptionProvider", "gemini").toString() == "mai" ? TranscriptionProvider::MAI
                                                                         : TranscriptionProvider::Gemini;
}

TranscriptionOptions Settings::transcriptionOptions() const
{
    TranscriptionOptions o;
    o.provider = transcriptionProvider();
    o.maiStyle = maiStyle();
    o.geminiConfig = geminiConfig();
    o.formatting = formattingPolicy();
    o.legacy = usesLegacyTranscribeEndpoint();
    o.live = liveTranscriptionActive();
    return o;
}

bool Settings::hasSelectedAPIKey() const
{
    return transcriptionProvider() == TranscriptionProvider::Gemini ? !SecretStore::instance().geminiKey().isEmpty()
                                                                    : !SecretStore::instance().openRouterKey().isEmpty();
}

QString Settings::usableEndpointURL(const QString &raw)
{
    const QString t = raw.trimmed();
    if (t.isEmpty()) return {};
    QUrl url(t);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != "http" && scheme != "https")) return {};
    return t;
}

GeminiConfig Settings::geminiConfig() const
{
    GeminiConfig c;
    const QString ep = usableEndpointURL(endpointOverride());
    if (!ep.isEmpty()) c.endpoint = ep;
    if (!transcribeModelOverride().trimmed().isEmpty()) c.transcribeModel = transcribeModelOverride().trimmed();
    if (!cleanupModelOverride().trimmed().isEmpty()) c.cleanupModel = cleanupModelOverride().trimmed();
    while (c.endpoint.endsWith('/')) c.endpoint.chop(1);
    return c;
}

void Settings::setSmartCleanupPass(bool enabled)
{
    // A deliberate re-enable is a clean slate for the gate-trip counter.
    if (enabled) m_settings.remove("gateTrips");
    set("smartCleanupPass", enabled);
}

int Settings::recordGateTrip(const QDateTime &now)
{
    QStringList trips = m_settings.value("gateTrips").toStringList();
    QStringList kept;
    for (const QString &t : trips) {
        const QDateTime d = QDateTime::fromString(t, Qt::ISODate);
        if (d.isValid() && d.secsTo(now) < 86400) kept << t;
    }
    kept << now.toString(Qt::ISODate);
    m_settings.setValue("gateTrips", kept);
    m_settings.sync();
    return int(kept.size());
}

} // namespace jot
