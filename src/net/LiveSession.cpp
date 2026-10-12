// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) to Linux/Qt.
#include "LiveSession.h"
#include "TranscriptionService.h"
#include "../core/DictionaryStore.h"
#include "../core/SecretStore.h"
#include "../core/Settings.h"
#include "../core/TextPipeline.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QNetworkRequest>

Q_DECLARE_LOGGING_CATEGORY(lcTranscription)

namespace jot {

// ================================================================ PCMRing

void PCMRing::append(const QByteArray &chunk)
{
    if (chunk.isEmpty()) return;
    QMutexLocker l(&m_lock);
    m_chunks << chunk;
    m_queued += int(chunk.size());
    while (m_queued > m_capacity && !m_chunks.isEmpty()) {
        const QByteArray evicted = m_chunks.takeFirst();
        m_queued -= int(evicted.size());
        m_dropped += evicted.size();
        ++m_droppedChunks;
    }
}

QList<QByteArray> PCMRing::drain()
{
    QMutexLocker l(&m_lock);
    QList<QByteArray> out;
    out.swap(m_chunks);
    m_queued = 0;
    return out;
}

// ================================================================ LiveStats

static QString fallbackKey(LiveStats::Fallback f)
{
    switch (f) {
    case LiveStats::Fallback::NeverOpened: return "neverOpened";
    case LiveStats::Fallback::DroppedMidSession: return "droppedMidSession";
    case LiveStats::Fallback::Truncated: return "truncated";
    case LiveStats::Fallback::NoFinal: return "noFinal";
    }
    return "noFinal";
}

void LiveStats::recordSuccess()
{
    auto &s = Settings::instance();
    s.set("liveAttempts", attempts() + 1);
    s.set("liveSuccesses", successes() + 1);
    s.set("liveConsecutiveFailures", 0);
}

void LiveStats::recordFallback(Fallback reason)
{
    auto &s = Settings::instance();
    s.set("liveAttempts", attempts() + 1);
    const QString key = "liveFallback_" + fallbackKey(reason);
    s.set(key, s.value(key, 0).toInt() + 1);
    s.set("liveConsecutiveFailures", consecutiveFailures() + 1);
    s.set("liveLastFailureAt", double(QDateTime::currentSecsSinceEpoch()));
}

int LiveStats::attempts() const { return Settings::instance().value("liveAttempts", 0).toInt(); }
int LiveStats::successes() const { return Settings::instance().value("liveSuccesses", 0).toInt(); }
int LiveStats::consecutiveFailures() const { return Settings::instance().value("liveConsecutiveFailures", 0).toInt(); }
void LiveStats::clearStreak() { Settings::instance().set("liveConsecutiveFailures", 0); }

bool LiveStats::shouldStopTrying() const
{
    if (consecutiveFailures() < failureLimit) return false;
    const double last = Settings::instance().value("liveLastFailureAt", 0.0).toDouble();
    return double(QDateTime::currentSecsSinceEpoch()) - last < retryAfter;
}

LiveStats::Fallback LiveStats::classify(const QString &why)
{
    const QString l = why.toLower();
    if (l.contains("truncated") || l.contains("dropped")) return Fallback::Truncated;
    if (l.contains("setup") || l.contains("connect") || l.contains("refused")) return Fallback::NeverOpened;
    if (l.contains(QString("no final")) || l.contains("empty")) return Fallback::NoFinal;
    return Fallback::DroppedMidSession;
}

QString LiveStats::summary() const
{
    const int a = attempts();
    if (a <= 0) return {};
    const int s = successes();
    const int percent = int(std::lround(double(s) / double(a) * 100));
    QString line = QCoreApplication::translate("Jot", "Used live for %1 of the last %2 dictations (%3%).").arg(s).arg(a).arg(percent);
    if (s < a) {
        Fallback worst = Fallback::NeverOpened;
        int worstCount = 0;
        for (auto f : {Fallback::NeverOpened, Fallback::DroppedMidSession, Fallback::Truncated, Fallback::NoFinal}) {
            const int c = Settings::instance().value("liveFallback_" + fallbackKey(f), 0).toInt();
            if (c > worstCount) { worst = f; worstCount = c; }
        }
        if (worstCount > 0) {
            const QMap<Fallback, QString> phrases = {
                {Fallback::NeverOpened, QCoreApplication::translate("Jot", "the connection could not be opened")},
                {Fallback::DroppedMidSession, QCoreApplication::translate("Jot", "the connection dropped mid-sentence")},
                {Fallback::Truncated, QCoreApplication::translate("Jot", "audio arrived faster than it could be sent")},
                {Fallback::NoFinal, QCoreApplication::translate("Jot", "the transcript did not arrive in time")},
            };
            line += QCoreApplication::translate("Jot", " Most fell back because %1.").arg(phrases.value(worst));
        }
    }
    return line;
}

// ================================================================ Protocol

static QByteArray setupFrame(const LiveSetup &setup)
{
    QJsonObject transcription{{"mode", setup.smart ? "SMART" : "VERBATIM"}};
    if (!setup.customVocabulary.isEmpty()) transcription["customVocabulary"] = QJsonArray::fromStringList(setup.customVocabulary);
    QJsonObject frame{{"setup", QJsonObject{
        {"model", "models/" + setup.model},
        {"generationConfig", QJsonObject{{"responseModalities", QJsonArray{"TEXT"}}}},
        {"inputAudioTranscription", transcription},
        {"realtimeInputConfig", QJsonObject{{"automaticActivityDetection", QJsonObject{{"disabled", true}}}}},
    }}};
    return QJsonDocument(frame).toJson(QJsonDocument::Compact);
}

static QByteArray audioFrame(const QByteArray &pcm)
{
    QJsonObject frame{{"realtimeInput", QJsonObject{{"audio", QJsonObject{
        {"data", QString::fromLatin1(pcm.toBase64())}, {"mimeType", "audio/pcm;rate=16000"}}}}}};
    return QJsonDocument(frame).toJson(QJsonDocument::Compact);
}

static QByteArray activityFrame(const char *name)
{
    return QJsonDocument(QJsonObject{{"realtimeInput", QJsonObject{{name, QJsonObject{}}}}}).toJson(QJsonDocument::Compact);
}

QString LiveSession::endpoint()
{
    return QStringLiteral("wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent");
}

// ================================================================ Session

LiveSession::LiveSession(const QString &apiKey, LiveSetup setup, double ringSeconds, QObject *parent)
    : QObject(parent)
    , m_apiKey(apiKey)
    , m_setup(std::move(setup))
    , m_ring(ringSeconds)
{
    m_setupTimer.setSingleShot(true);
    m_finalTimer.setSingleShot(true);
    connect(&m_socket, &QWebSocket::textMessageReceived, this, &LiveSession::onText);
    connect(&m_socket, &QWebSocket::binaryMessageReceived, this, [this](const QByteArray &b) { onText(QString::fromUtf8(b)); });
    connect(&m_socket, &QWebSocket::connected, this, [this] {
        if (m_closed) return;
        sendJson(setupFrame(m_setup));
    });
    connect(&m_socket, &QWebSocket::disconnected, this, [this] {
        if (m_closed) return;
        const QString why = QString("receive failed: socket closed (") + m_socket.closeReason() + ")";
        if (!m_didSetup && m_beginCb) {
            m_setupTimer.stop();
            auto cb = std::exchange(m_beginCb, nullptr);
            cb(false, QString("connect failed: ") + m_socket.closeReason());
            return;
        }
        recordFailure(why);
        if (m_finishCb) completeFinish();
    });
    connect(&m_socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_closed) return;
        if (!m_didSetup && m_beginCb) {
            m_setupTimer.stop();
            auto cb = std::exchange(m_beginCb, nullptr);
            cb(false, QString("connect failed: ") + m_socket.errorString());
            return;
        }
        recordFailure(QString("receive failed: ") + m_socket.errorString());
        if (m_finishCb) completeFinish();
    });
    connect(&m_setupTimer, &QTimer::timeout, this, [this] {
        if (m_beginCb) {
            auto cb = std::exchange(m_beginCb, nullptr);
            cb(false, QString("setup timed out"));
        }
    });
    connect(&m_finalTimer, &QTimer::timeout, this, [this] { if (m_finishCb) completeFinish(); });
}

LiveSession::~LiveSession() { close(); }

void LiveSession::enqueue(const QByteArray &pcm)
{
    m_ring.append(pcm);
    QMetaObject::invokeMethod(this, "pump", Qt::QueuedConnection);
}

void LiveSession::sendJson(const QByteArray &json)
{
    m_socket.sendTextMessage(QString::fromUtf8(json));
}

void LiveSession::begin(std::function<void(bool, QString)> cb, double setupTimeout)
{
    if (m_closed) { cb(false, "aborted"); return; }
    m_beginCb = std::move(cb);
    QNetworkRequest req{QUrl(endpoint())};
    req.setRawHeader("x-goog-api-key", m_apiKey.toUtf8());
    m_setupTimer.start(int(setupTimeout * 1000));
    m_socket.open(req);
}

void LiveSession::onText(const QString &message)
{
    if (m_closed) return;
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) return;
    const QJsonObject root = doc.object();

    if (root.contains("setupComplete") || root.contains("setup_complete")) {
        if (m_didSetup) return;
        m_didSetup = true;
        m_setupTimer.stop();
        sendJson(activityFrame("activityStart"));
        m_streaming = true;
        if (m_beginCb) {
            auto cb = std::exchange(m_beginCb, nullptr);
            cb(true, {});
        }
        pump();
        return;
    }
    if (root.contains("error")) {
        const QString why = root.value("error").toObject().value("message").toString("unknown live error");
        if (!m_didSetup && m_beginCb) {
            m_setupTimer.stop();
            auto cb = std::exchange(m_beginCb, nullptr);
            cb(false, QString("refused: ") + why);
            return;
        }
        recordFailure(why);
        if (m_finishCb) completeFinish();
        return;
    }
    QJsonObject content = root.value("serverContent").toObject();
    if (content.isEmpty()) content = root.value("server_content").toObject();
    if (content.contains("goAway") || content.contains("go_away") || root.contains("goAway") || root.contains("go_away")) {
        recordFailure(QString("server sent goAway"));
        if (m_finishCb) completeFinish();
        return;
    }
    auto textOf = [&](const char *camel, const char *snake) -> QString {
        QJsonObject node = content.value(camel).toObject();
        if (node.isEmpty()) node = content.value(snake).toObject();
        return node.value("text").toString();
    };
    const QString interim = textOf("interimInputTranscription", "interim_input_transcription");
    if (!interim.isEmpty()) {
        emit partial(interim);
        return;
    }
    const QString final = textOf("inputTranscription", "input_transcription");
    if (!final.isEmpty()) {
        m_finals << final;
        if (m_finishCb && m_activityEndFlushed) completeFinish();
    }
}

void LiveSession::pump()
{
    if (m_closed || !m_streaming || m_activityEndFlushed) return;
    // Drain the ring FIRST: this keeps activityEnd behind the audio it must not overtake.
    for (const QByteArray &chunk : m_ring.drain()) {
        if (m_socket.state() != QAbstractSocket::ConnectedState) {
            recordFailure(QString("send failed: socket not connected"));
            return;
        }
        sendJson(audioFrame(chunk));
        m_ring.markAccepted(chunk.size());
    }
    if (m_endRequested) {
        sendJson(activityFrame("activityEnd"));
        m_socket.flush();
        m_activityEndFlushed = true;
    }
}

void LiveSession::finish(double deadline, std::function<void(std::optional<QString>, QString)> cb)
{
    if (!m_didSetup) { close(); cb(std::nullopt, setupIncompleteReason); return; }
    if (!m_failure.isEmpty()) { close(); cb(std::nullopt, m_failure); return; }
    m_finishCb = std::move(cb);
    m_endRequested = true;
    pump();
    if (!m_failure.isEmpty()) { completeFinish(); return; }
    if (!m_finals.isEmpty()) { completeFinish(); return; }
    m_finalTimer.start(int(deadline * 1000));
}

void LiveSession::completeFinish()
{
    m_finalTimer.stop();
    auto cb = std::exchange(m_finishCb, nullptr);
    if (!cb) return;
    const bool wasClosed = m_closed;
    close();
    if (wasClosed) { cb(std::nullopt, cancelledReason); return; }
    if (!m_failure.isEmpty()) { cb(std::nullopt, m_failure); return; }
    if (m_finals.isEmpty()) { cb(std::nullopt, QString("no final transcript before deadline")); return; }
    if (m_ring.didDrop()) {
        cb(std::nullopt, QString("dropped %1 chunks — stream is truncated").arg(m_ring.droppedChunks()));
        return;
    }
    const QString joined = m_finals.join(' ').trimmed();
    if (joined.isEmpty()) { cb(std::nullopt, QString("final transcript was empty")); return; }
    cb(joined, {});
}

void LiveSession::recordFailure(const QString &why)
{
    if (m_failure.isEmpty()) m_failure = why;
}

void LiveSession::abort()
{
    const bool hadFinish = bool(m_finishCb);
    close();
    if (m_beginCb) {
        auto cb = std::exchange(m_beginCb, nullptr);
        cb(false, "aborted");
    }
    if (hadFinish) {
        auto cb = std::exchange(m_finishCb, nullptr);
        cb(std::nullopt, cancelledReason);
    }
}

void LiveSession::close()
{
    if (m_closed) return;
    m_closed = true;
    m_setupTimer.stop();
    m_finalTimer.stop();
    m_socket.abort();
}

// ================================================================ Transcriber

LiveTranscriber::LiveTranscriber(LiveSession *session, DictationContext context, QObject *parent)
    : QObject(parent)
    , m_session(session)
    , m_context(std::move(context))
{
    m_session->setParent(this);
}

LiveTranscriber *LiveTranscriber::create(const DictationContext &context, double ringSeconds, QObject *parent)
{
    const TranscriptionOptions options = context.options.value_or(Settings::instance().transcriptionOptions());
    if (options.provider != TranscriptionProvider::Gemini || !options.live || options.legacy) return nullptr;
    LiveStats stats;
    if (stats.shouldStopTrying()) {
        qCInfo(lcTranscription) << "live disabled for now after" << stats.consecutiveFailures()
                                << QString("consecutive failures — uploading instead");
        return nullptr;
    }
    const QString key = SecretStore::instance().geminiKey();
    if (key.isEmpty()) return nullptr;
    LiveSetup setup;
    setup.smart = options.formatting.nativeSmart;
    setup.customVocabulary = DictionaryStore::instance().vocabulary();
    auto *session = new LiveSession(key, setup, ringSeconds);
    DictationContext ctx = context;
    ctx.options = options;
    return new LiveTranscriber(session, ctx, parent);
}

void LiveTranscriber::begin(std::function<void(bool)> cb)
{
    m_session->begin([this, cb](bool ok, const QString &why) {
        if (!ok) {
            // Our own teardown is not a live-mode failure.
            if (why != "aborted") m_stats.recordFallback(LiveStats::Fallback::NeverOpened);
            qCInfo(lcTranscription) << "live session did not open (" << why << ")";
        }
        cb(ok);
    });
}

void LiveTranscriber::finish(double deadline, qint64 framesWritten, std::function<void(std::optional<TranscriptionResult>)> cb)
{
    QPointer<LiveTranscriber> self(this);
    m_session->finish(deadline, [self, framesWritten, cb](std::optional<QString> text, const QString &why) {
        if (!self) { cb(std::nullopt); return; }
        if (!text) {
            qCInfo(lcTranscription) << "live session unusable, falling back to upload:" << why;
            if (why != LiveSession::setupIncompleteReason && why != LiveSession::cancelledReason)
                self->m_stats.recordFallback(LiveStats::classify(why));
            cb(std::nullopt);
            return;
        }
        // The reconciliation: the only check that sees missing audio.
        const qint64 expected = framesWritten * 2;
        const qint64 accepted = self->m_session->acceptedBytes();
        if (accepted != expected) {
            qCInfo(lcTranscription) << "live byte mismatch: socket took" << accepted << "of" << expected << "— uploading instead";
            self->m_stats.recordFallback(LiveStats::Fallback::Truncated);
            cb(std::nullopt);
            return;
        }
        self->m_stats.recordSuccess();
        const auto options = *self->m_context.options;
        TranscriptionService::instance().finishTranscript(
            *text, self->m_context, "gemini-3.5-transcribe-live/" + modeKey(options.formatting.mode()),
            options.formatting, options.geminiConfig, makeToken(),
            [cb](TranscriptionResult r) { cb(r); });
    });
}

} // namespace jot
