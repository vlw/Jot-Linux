// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) to Linux/Qt.
#include "GeminiClient.h"
#include "../core/FileLayout.h"
#include "../core/SecretStore.h"

#include <FLAC/stream_encoder.h>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>

Q_LOGGING_CATEGORY(lcTranscription, "jot.transcription")

namespace jot {

GeminiClient &GeminiClient::instance()
{
    static GeminiClient c;
    return c;
}

static QNetworkRequest authedRequest(const QUrl &url, const QString &key)
{
    QNetworkRequest req(url);
    // Header, never ?key= — query strings leak into logs and proxies.
    if (!key.isEmpty()) req.setRawHeader("x-goog-api-key", key.toUtf8());
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
    return req;
}

QJsonObject GeminiClient::errorObject(const QByteArray &data)
{
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject root;
    if (doc.isObject()) root = doc.object();
    else if (doc.isArray() && !doc.array().isEmpty()) root = doc.array().first().toObject();
    return root.value("error").toObject();
}

QString GeminiClient::errorMessage(const QByteArray &data)
{
    return errorObject(data).value("message").toString();
}

QStringList GeminiClient::errorReasons(const QByteArray &data)
{
    QStringList out;
    for (const auto &d : errorObject(data).value("details").toArray()) {
        const QString r = d.toObject().value("reason").toString();
        if (!r.isEmpty()) out << r;
    }
    return out;
}

static TranscriptionError mapNetworkError(QNetworkReply::NetworkError e)
{
    switch (e) {
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TemporaryNetworkFailureError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::UnknownNetworkError:
        return TranscriptionError::make(TranscriptionErrorKind::Offline);
    case QNetworkReply::TimeoutError:
        return TranscriptionError::make(TranscriptionErrorKind::Timeout);
    default:
        return TranscriptionError::make(TranscriptionErrorKind::Network, QString::number(int(e)));
    }
}

void GeminiClient::post(const QString &path, const QByteArray &body, const QString &endpoint, double deadline,
                        const QString &modelLabel, bool modelIsInPath, CancelToken token,
                        std::function<void(QByteArray, TranscriptionError)> cb)
{
    // No key means no request — an unauthenticated 403 would read as an endpoint fault.
    const QString key = SecretStore::instance().geminiKey();
    if (key.isEmpty()) { cb({}, TranscriptionError::make(TranscriptionErrorKind::Auth)); return; }
    if (token && token->cancelled) { cb({}, TranscriptionError::make(TranscriptionErrorKind::Cancelled)); return; }

    QNetworkRequest req = authedRequest(QUrl(endpoint + "/" + path), key);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_nam.post(req, body);
    if (token) token->current = reply;

    // transferTimeout is an IDLE timer; enforce the true wall-clock deadline ourselves.
    auto timedOut = std::make_shared<bool>(false);
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, [reply, timedOut] { *timedOut = true; reply->abort(); });
    timer->start(int(deadline * 1000));

    connect(reply, &QNetworkReply::finished, this, [=] {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        const QVariant statusAttr = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        if (token && token->cancelled) { cb({}, TranscriptionError::make(TranscriptionErrorKind::Cancelled)); return; }
        if (*timedOut) { cb({}, TranscriptionError::make(TranscriptionErrorKind::Timeout)); return; }
        if (!statusAttr.isValid()) { cb({}, mapNetworkError(reply->error())); return; }
        const int status = statusAttr.toInt();

        if ((status == 400 || status == 403) && errorReasons(data).contains("API_KEY_INVALID")) {
            qCWarning(lcTranscription) << "GeminiClient:" << status << "API_KEY_INVALID on" << path;
            cb({}, TranscriptionError::make(TranscriptionErrorKind::Auth));
            return;
        }
        switch (status) {
        case 200:
            cb(data, {});
            return;
        case 401:
            cb({}, TranscriptionError::make(TranscriptionErrorKind::Auth));
            return;
        case 402:
            cb({}, TranscriptionError::make(TranscriptionErrorKind::RateLimitedDaily));
            return;
        case 403:
        case 404: {
            const QString message = errorMessage(data);
            qCWarning(lcTranscription) << "GeminiClient:" << status << "on" << path << "(" << modelLabel << ")";
            if (!modelIsInPath) {
                cb({}, TranscriptionError::make(TranscriptionErrorKind::Network, QString("interactions_unavailable_%1").arg(status)));
                return;
            }
            TranscriptionError e = TranscriptionError::make(TranscriptionErrorKind::ModelUnavailable, message);
            e.model = modelLabel;
            cb({}, e);
            return;
        }
        case 429: {
            static const QRegularExpression perDay(R"("quotaId"\s*:\s*"[^"]*PerDay[^"]*")");
            if (perDay.match(QString::fromUtf8(data)).hasMatch())
                cb({}, TranscriptionError::make(TranscriptionErrorKind::RateLimitedDaily));
            else
                cb({}, TranscriptionError::make(TranscriptionErrorKind::RateLimitedTransient));
            return;
        }
        case 400: {
            QString message = errorMessage(data);
            if (message.isEmpty()) message = QString("http_%1").arg(status);
            cb({}, TranscriptionError::make(TranscriptionErrorKind::BadRequest, message));
            return;
        }
        default:
            if (status >= 500 && status <= 599) {
                cb({}, TranscriptionError::make(TranscriptionErrorKind::Network, QString("http_%1").arg(status)));
                return;
            }
            QString message = errorMessage(data);
            if (message.isEmpty()) message = QString("http_%1").arg(status);
            cb({}, TranscriptionError::make(TranscriptionErrorKind::Network, message));
        }
    });
}

static TranscriptionError extractText(const QByteArray &data, QString *out)
{
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return TranscriptionError::make(TranscriptionErrorKind::Network, "unparseable_response");
    const QJsonObject json = doc.object();
    if (json.value("promptFeedback").toObject().contains("blockReason"))
        return TranscriptionError::make(TranscriptionErrorKind::SafetyBlocked);
    const QJsonArray candidates = json.value("candidates").toArray();
    if (candidates.isEmpty()) return TranscriptionError::make(TranscriptionErrorKind::Network, "no_candidates");
    const QJsonObject first = candidates.first().toObject();
    if (first.value("finishReason").toString() == "SAFETY") return TranscriptionError::make(TranscriptionErrorKind::SafetyBlocked);
    QString text;
    for (const auto &p : first.value("content").toObject().value("parts").toArray()) text += p.toObject().value("text").toString();
    *out = text;
    return {};
}

static TranscriptionError extractInteractionText(const QByteArray &data, QString *out)
{
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return TranscriptionError::make(TranscriptionErrorKind::Network, "unparseable_response");
    const QJsonObject json = doc.object();
    const QString status = json.value("status").toString("missing");
    if (status != "completed") {
        const QString detail = json.value("error").toObject().value("message").toString();
        if (status == "failed" || status == "error") {
            const QString lowered = detail.toLower();
            if (lowered.contains("safety") || lowered.contains("blocked"))
                return TranscriptionError::make(TranscriptionErrorKind::SafetyBlocked);
            return TranscriptionError::make(TranscriptionErrorKind::Network, "interaction_failed");
        }
        return TranscriptionError::make(TranscriptionErrorKind::Network, "interaction_status_" + status);
    }
    if (!json.value("steps").isArray()) return TranscriptionError::make(TranscriptionErrorKind::Network, "no_steps");
    QString text;
    for (const auto &s : json.value("steps").toArray()) {
        const QJsonObject step = s.toObject();
        if (step.value("type").toString() != "model_output") continue;
        for (const auto &c : step.value("content").toArray()) {
            const QJsonObject part = c.toObject();
            if (part.value("type").toString() == "text") text += part.value("text").toString();
        }
    }
    *out = text;
    return {};
}

void GeminiClient::transcribe(const QByteArray &flac, const QString &model, const QString &endpoint, double deadline,
                              const QStringList &customVocabulary, CancelToken token, TextCallback cb)
{
    // `mode` is NOT available here; wordTimestamp is mandatory or the transcript comes back empty.
    QJsonObject audioConfig{{"wordTimestamp", true}, {"diarization", false}};
    if (!customVocabulary.isEmpty()) audioConfig["customVocabulary"] = QJsonArray::fromStringList(customVocabulary);
    QJsonObject inlineData{{"mime_type", "audio/flac"}, {"data", QString::fromLatin1(flac.toBase64())}};
    QJsonObject body{
        {"contents", QJsonArray{QJsonObject{{"role", "user"}, {"parts", QJsonArray{QJsonObject{{"inline_data", inlineData}}}}}}},
        {"generationConfig", QJsonObject{{"temperature", 0}, {"audioTranscriptionConfig", audioConfig}}},
    };
    post("v1beta/models/" + model + ":generateContent", QJsonDocument(body).toJson(QJsonDocument::Compact), endpoint,
         deadline, model, true, token, [cb](QByteArray data, TranscriptionError err) {
             if (!err.ok()) { cb({}, err); return; }
             QString text;
             cb(text, extractText(data, &text));
         });
}

void GeminiClient::transcribeInteraction(const QByteArray &audio, const QString &mimeType, const QString &model,
                                         const QString &endpoint, TranscriptionMode mode,
                                         const QStringList &customVocabulary, double deadline, CancelToken token,
                                         TextCallback cb)
{
    QJsonObject body{
        {"model", model},
        {"input", QJsonArray{QJsonObject{{"type", "audio"}, {"mime_type", mimeType}, {"data", QString::fromLatin1(audio.toBase64())}}}},
    };
    // NEVER add language_codes: it silently disables smart mode (verified upstream 2026-08-20).
    QJsonObject config;
    if (mode == TranscriptionMode::Smart) config["mode"] = "smart";
    if (!customVocabulary.isEmpty()) config["custom_vocabulary"] = QJsonArray::fromStringList(customVocabulary);
    if (!config.isEmpty()) body["generation_config"] = QJsonObject{{"transcription_config", config}};
    post("v1beta/interactions", QJsonDocument(body).toJson(QJsonDocument::Compact), endpoint, deadline, model, false,
         token, [cb](QByteArray data, TranscriptionError err) {
             if (!err.ok()) { cb({}, err); return; }
             QString text;
             cb(text, extractInteractionText(data, &text));
         });
}

void GeminiClient::cleanup(const QString &prompt, const QString &model, const QString &endpoint, double deadline,
                           CancelToken token, TextCallback cb)
{
    const QJsonObject thinking = model.startsWith("gemini-2") ? QJsonObject{{"thinkingBudget", 0}}
                                                              : QJsonObject{{"thinkingLevel", "low"}};
    QJsonObject body{
        {"contents", QJsonArray{QJsonObject{{"role", "user"}, {"parts", QJsonArray{QJsonObject{{"text", prompt}}}}}}},
        {"generationConfig", QJsonObject{{"temperature", 0}, {"thinkingConfig", thinking}}},
    };
    post("v1beta/models/" + model + ":generateContent", QJsonDocument(body).toJson(QJsonDocument::Compact), endpoint,
         deadline, model, true, token, [cb](QByteArray data, TranscriptionError err) {
             if (!err.ok()) { cb({}, err); return; }
             QString text;
             cb(text, extractText(data, &text));
         });
}

void GeminiClient::validateKey(const QString &endpoint, const QString &key, std::function<void(KeyCheck, QString)> cb)
{
    QUrl url(endpoint + "/v1beta/models");
    QUrlQuery q;
    q.addQueryItem("pageSize", "1");
    url.setQuery(q);
    QNetworkRequest req = authedRequest(url, key);
    req.setTransferTimeout(10000);
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, cb] {
        reply->deleteLater();
        const QVariant st = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        if (!st.isValid()) { cb(KeyCheck::Unreachable, {}); return; }
        const int status = st.toInt();
        const QByteArray data = reply->readAll();
        if (status == 200) cb(KeyCheck::Valid, {});
        else if (status >= 500) cb(KeyCheck::Unreachable, {});
        else cb(KeyCheck::Rejected, errorMessage(data));
    });
}

void GeminiClient::resolveAvailableModel(const QStringList &candidates, const QString &endpoint, const QString &key,
                                         std::function<void(QString)> cb)
{
    if (candidates.isEmpty()) { cb({}); return; }
    const QString model = candidates.first();
    QNetworkRequest req = authedRequest(QUrl(endpoint + "/v1beta/models/" + model), key);
    req.setTransferTimeout(8000);
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [=] {
        reply->deleteLater();
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) { cb(model); return; }
        resolveAvailableModel(candidates.mid(1), endpoint, key, cb);
    });
}

void GeminiClient::warmConnection(const QString &model, const QString &endpoint)
{
    // Opens TLS while the user is still speaking; the response is ignored.
    const QString key = SecretStore::instance().geminiKey();
    if (key.isEmpty()) return;
    QNetworkRequest req = authedRequest(QUrl(endpoint + "/v1beta/models/" + model), key);
    req.setTransferTimeout(5000);
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

// ------------------------------------------------------------------ audio

QByteArray readPCMFromWav(const QString &wavPath)
{
    QFile f(wavPath);
    if (!f.open(QIODevice::ReadOnly)) return {};
    if (f.size() <= FileLayout::wavHeaderBytes) return {};
    f.seek(FileLayout::wavHeaderBytes);
    QByteArray pcm = f.readAll();
    if (pcm.size() % 2) pcm.chop(1);
    return pcm;
}

QByteArray encodeFLACFromWav(const QString &wavPath, QString *error)
{
    const QByteArray pcm = readPCMFromWav(wavPath);
    if (pcm.isEmpty()) {
        if (error) *error = "no audio";
        return {};
    }
    QElapsedTimer t;
    t.start();
    FLAC__StreamEncoder *enc = FLAC__stream_encoder_new();
    FLAC__stream_encoder_set_channels(enc, 1);
    FLAC__stream_encoder_set_bits_per_sample(enc, 16);
    FLAC__stream_encoder_set_sample_rate(enc, FileLayout::sampleRate);
    FLAC__stream_encoder_set_compression_level(enc, 5);
    FLAC__stream_encoder_set_total_samples_estimate(enc, pcm.size() / 2);

    struct Sink {
        QByteArray out;
        qint64 pos = 0;
    } sink;
    auto write = [](const FLAC__StreamEncoder *, const FLAC__byte buffer[], size_t bytes, uint32_t, uint32_t,
                    void *client) -> FLAC__StreamEncoderWriteStatus {
        auto *s = static_cast<Sink *>(client);
        const qint64 end = s->pos + qint64(bytes);
        if (end > s->out.size()) s->out.resize(end);
        memcpy(s->out.data() + s->pos, buffer, bytes);
        s->pos = end;
        return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
    };
    auto seek = [](const FLAC__StreamEncoder *, FLAC__uint64 offset, void *client) -> FLAC__StreamEncoderSeekStatus {
        static_cast<Sink *>(client)->pos = qint64(offset);
        return FLAC__STREAM_ENCODER_SEEK_STATUS_OK;
    };
    auto tell = [](const FLAC__StreamEncoder *, FLAC__uint64 *offset, void *client) -> FLAC__StreamEncoderTellStatus {
        *offset = FLAC__uint64(static_cast<Sink *>(client)->pos);
        return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
    };
    if (FLAC__stream_encoder_init_stream(enc, write, seek, tell, nullptr, &sink) != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
        FLAC__stream_encoder_delete(enc);
        if (error) *error = "flac init failed";
        return {};
    }
    const auto *samples = reinterpret_cast<const qint16 *>(pcm.constData());
    const qsizetype count = pcm.size() / 2;
    std::vector<FLAC__int32> buf(4096);
    for (qsizetype off = 0; off < count; off += 4096) {
        const qsizetype n = std::min<qsizetype>(4096, count - off);
        for (qsizetype i = 0; i < n; ++i) buf[size_t(i)] = samples[off + i];
        if (!FLAC__stream_encoder_process_interleaved(enc, buf.data(), uint32_t(n))) {
            FLAC__stream_encoder_delete(enc);
            if (error) *error = "flac encode failed";
            return {};
        }
    }
    FLAC__stream_encoder_finish(enc);
    FLAC__stream_encoder_delete(enc);
    qCInfo(lcTranscription) << "FLAC" << sink.out.size() << "bytes in" << t.elapsed() << "ms";
    return sink.out;
}

} // namespace jot
