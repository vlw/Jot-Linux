// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) to Linux/Qt.
#include "TranscriptionService.h"
#include "GeminiClient.h"
#include "../core/DictionaryStore.h"
#include "../core/SecretStore.h"
#include "../core/Settings.h"
#include "../core/TextPipeline.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QTimer>

Q_DECLARE_LOGGING_CATEGORY(lcTranscription)

namespace jot {

TranscriptionService &TranscriptionService::instance()
{
    static TranscriptionService s;
    return s;
}

void TranscriptionService::warmConnection()
{
    const auto config = Settings::instance().geminiConfig();
    GeminiClient::instance().warmConnection(config.transcribeModel, config.endpoint);
}

void TranscriptionService::transcribe(const QString &audioPath, double durationSeconds, const DictationContext &context,
                                      CancelToken token, ResultCallback cb)
{
    const TranscriptionOptions options = context.options.value_or(Settings::instance().transcriptionOptions());
    attempt(audioPath, durationSeconds, context, options, token, 1, 0, cb);
}

void TranscriptionService::attempt(const QString &audioPath, double duration, const DictationContext &context,
                                   const TranscriptionOptions &options, CancelToken token, int attemptNo,
                                   int emptyAnswers, ResultCallback cb)
{
    transcribeOnce(audioPath, duration, context, options, token,
                   [=, this](const TranscriptionResult &result, const TranscriptionError &error) {
        if (error.ok()) { cb(result, error); return; }
        if (error.kind == TranscriptionErrorKind::Cancelled || (token && token->cancelled)) {
            cb({}, TranscriptionError::make(TranscriptionErrorKind::Cancelled));
            return;
        }
        if (attemptNo >= attempts) { cb({}, error); return; }
        int empties = emptyAnswers;
        if (error.kind == TranscriptionErrorKind::EmptyTranscript) {
            ++empties;
            if (empties >= emptyTranscriptAnswers || duration < emptyTranscriptMinSeconds) { cb({}, error); return; }
        }
        qCInfo(lcTranscription) << "transcribe attempt" << attemptNo << "failed (" << error.describe() << ") — retrying in 1s";
        QTimer::singleShot(1000, this, [=, this] {
            if (token && token->cancelled) { cb({}, TranscriptionError::make(TranscriptionErrorKind::Cancelled)); return; }
            attempt(audioPath, duration, context, options, token, attemptNo + 1, empties, cb);
        });
    });
}

void TranscriptionService::transcribeOnce(const QString &audioPath, double duration, const DictationContext &context,
                                          const TranscriptionOptions &options, CancelToken token, ResultCallback cb)
{
    if (options.provider == TranscriptionProvider::Gemini)
        geminiTranscribe(audioPath, duration, context, options, token, cb);
    else
        maiTranscribe(audioPath, duration, options.maiStyle, token, cb);
}

void TranscriptionService::geminiTranscribe(const QString &audioPath, double duration, const DictationContext &context,
                                            const TranscriptionOptions &options, CancelToken token, ResultCallback cb)
{
    const GeminiConfig config = options.geminiConfig;
    const FormattingPolicy policy = options.formatting;
    QString flacError;
    const QByteArray flac = encodeFLACFromWav(audioPath, &flacError);
    if (flac.isEmpty()) {
        cb({}, TranscriptionError::make(TranscriptionErrorKind::BadRequest, "audio unreadable: " + flacError));
        return;
    }
    // Read once per dictation: a toggle flipped mid-flight must not change the rules.
    QStringList vocabulary = DictionaryStore::instance().sanitizedVocabulary();
    const QString vocabKey = vocabulary.join('\x1f');
    if (m_hasBlockedVocabulary && m_blockedVocabulary == vocabKey) vocabulary.clear();
    const double deadline = TimeoutPolicy::overallDeadline(duration);
    const bool legacy = options.legacy;

    auto send = [=](const QStringList &terms, GeminiClient::TextCallback done) {
        if (legacy)
            GeminiClient::instance().transcribe(flac, config.transcribeModel, config.endpoint, deadline, terms, token, done);
        else
            GeminiClient::instance().transcribeInteraction(flac, "audio/flac", config.transcribeModel, config.endpoint,
                                                           policy.mode(), terms, deadline, token, done);
    };

    auto finish = [=, this](const QString &raw) {
        const QString trimmed = raw.trimmed();
        if (trimmed.isEmpty()) { cb({}, TranscriptionError::make(TranscriptionErrorKind::EmptyTranscript)); return; }
        finishTranscript(trimmed, context, config.transcribeModel + "/" + modeKey(policy.mode()), policy, config, token,
                         [cb](TranscriptionResult r) { cb(r, {}); });
    };

    send(vocabulary, [=, this](const QString &text, const TranscriptionError &err) {
        if (err.ok()) { finish(text); return; }
        if (err.kind == TranscriptionErrorKind::BadRequest && !vocabulary.isEmpty()) {
            // Fail open: one strange dictionary entry must never break dictation.
            qCWarning(lcTranscription) << "transcribe rejected with vocabulary — retrying without it";
            send({}, [=, this](const QString &text2, const TranscriptionError &err2) {
                if (!err2.ok()) { cb({}, err2); return; }
                // Latch only once the vocabulary-free retry SUCCEEDS.
                m_blockedVocabulary = vocabKey;
                m_hasBlockedVocabulary = true;
                finish(text2);
            });
            return;
        }
        cb({}, err);
    });
}

void TranscriptionService::finishTranscript(const QString &text, const DictationContext &context, const QString &modelID,
                                            const FormattingPolicy &policy, const GeminiConfig &config,
                                            CancelToken token, std::function<void(TranscriptionResult)> cb)
{
    const QString raw = text.trimmed();
    const auto rules = DictionaryStore::instance().replacementRules();
    if (!policy.cleanupPass) {
        // Dictionary rules are a HARD guarantee on every path; the gate needs a second model to compare.
        cb(TranscriptionResult{raw, ReplacementEngine::apply(rules, raw), modelID});
        return;
    }
    cleanupOrFallback(raw, context, config, token, [=](QString cleaned) {
        cb(TranscriptionResult{raw, ReplacementEngine::apply(rules, cleaned), modelID + "+" + config.cleanupModel});
    });
}

void TranscriptionService::cleanupOrFallback(const QString &raw, const DictationContext &context,
                                             const GeminiConfig &config, CancelToken token,
                                             std::function<void(QString)> cb)
{
    auto &dict = DictionaryStore::instance();
    const auto tone = PromptV1::toneCategory(context.targetAppClass);
    const QString prompt = PromptV1::cleanupPrompt(raw, tone, dict.sanitizedVocabulary(), dict.spellings());
    // A separate token so a cleanup miss never cancels the dictation itself.
    CancelToken cleanupToken = makeToken();
    if (token && token->cancelled) cleanupToken->cancelled = true;
    GeminiClient::instance().cleanup(prompt, config.cleanupModel, config.endpoint,
                                     TimeoutPolicy::cleanupDeadline(int(raw.size())), cleanupToken,
                                     [=](const QString &response, const TranscriptionError &err) {
        if (!err.ok()) {
            qCInfo(lcTranscription) << "cleanup unavailable (" << err.describe() << ") — inserting raw";
            cb(raw);
            return;
        }
        const QString cleaned = ValidationGate::stripArtifacts(response);
        const auto verdict = ValidationGate::validate(raw, cleaned);
        if (!verdict.accepted) {
            auto &s = Settings::instance();
            const int trips = s.recordGateTrip();
            qCWarning(lcTranscription) << "cleanup gate REJECTED (" << verdict.reason << ", trip #" << trips << "in 24h) — inserting raw";
            if (trips >= 3 && s.smartCleanupPassEnabled()) {
                s.setSmartCleanupPass(false);
                emit s.smartFormattingAutoDegraded();
            }
            cb(raw);
            return;
        }
        cb(cleaned);
    });
}

// ------------------------------------------------------------------ MAI

// MAI-Transcribe-2 via OpenRouter: the alternative transcription provider.
static const char *kMAIModel = "microsoft/mai-transcribe-2";

static void openRouterRequest(const QString &path, const QString &key, const QByteArray &body, double deadline,
                              CancelToken token, std::function<void(QByteArray, TranscriptionError)> cb)
{
    QNetworkRequest req(QUrl("https://openrouter.ai/api/v1/" + path));
    req.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    QNetworkAccessManager *nam = GeminiClient::instance().network();
    QNetworkReply *reply;
    if (!body.isEmpty()) {
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        reply = nam->post(req, body);
    } else {
        reply = nam->get(req);
    }
    if (token) token->current = reply;
    auto timedOut = std::make_shared<bool>(false);
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, reply, [reply, timedOut] { *timedOut = true; reply->abort(); });
    timer->start(int(deadline * 1000));
    QObject::connect(reply, &QNetworkReply::finished, reply, [=] {
        reply->deleteLater();
        if (token && token->cancelled) { cb({}, TranscriptionError::make(TranscriptionErrorKind::Cancelled)); return; }
        if (*timedOut) { cb({}, TranscriptionError::make(TranscriptionErrorKind::Timeout)); return; }
        const QVariant st = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        if (!st.isValid()) {
            if (reply->error() == QNetworkReply::HostNotFoundError || reply->error() == QNetworkReply::TemporaryNetworkFailureError)
                cb({}, TranscriptionError::make(TranscriptionErrorKind::Offline));
            else
                cb({}, TranscriptionError::make(TranscriptionErrorKind::Network,
                                                QString("OpenRouter connection failed (%1)").arg(int(reply->error()))));
            return;
        }
        const int status = st.toInt();
        const QByteArray data = reply->readAll();
        if (status >= 200 && status < 300) { cb(data, {}); return; }
        switch (status) {
        case 401: cb({}, TranscriptionError::make(TranscriptionErrorKind::Auth)); return;
        case 402: cb({}, TranscriptionError::make(TranscriptionErrorKind::RateLimitedDaily)); return;
        case 403:
        case 404: {
            auto e = TranscriptionError::make(TranscriptionErrorKind::ModelUnavailable, QString("OpenRouter HTTP %1").arg(status));
            e.model = kMAIModel;
            cb({}, e);
            return;
        }
        case 429: cb({}, TranscriptionError::make(TranscriptionErrorKind::RateLimitedTransient)); return;
        case 400:
        case 413:
        case 422: cb({}, TranscriptionError::make(TranscriptionErrorKind::BadRequest, QString("OpenRouter HTTP %1").arg(status))); return;
        default: cb({}, TranscriptionError::make(TranscriptionErrorKind::Network, QString("OpenRouter HTTP %1").arg(status)));
        }
    });
}

void TranscriptionService::validateOpenRouterKey(const QString &key, std::function<void(TranscriptionError)> cb)
{
    openRouterRequest("key", key, {}, 15, nullptr, [cb](QByteArray, TranscriptionError e) { cb(e); });
}

void TranscriptionService::maiTranscribe(const QString &audioPath, double duration, MAIStyle style, CancelToken token,
                                         ResultCallback cb)
{
    const QString key = SecretStore::instance().openRouterKey();
    if (key.isEmpty()) { cb({}, TranscriptionError::make(TranscriptionErrorKind::Auth)); return; }
    QString flacError;
    const QByteArray flac = encodeFLACFromWav(audioPath, &flacError);
    if (flac.isEmpty()) { cb({}, TranscriptionError::make(TranscriptionErrorKind::BadRequest, "audio unreadable")); return; }
    auto &dict = DictionaryStore::instance();
    QJsonObject enhanced{{"enabled", true}, {"model", "MAI-Transcribe-2"},
                         {"modelOptions", QJsonObject{{"transcribeStyle", maiStyleKey(style)}}}};
    QJsonObject azure;
    azure["phraseList"] = QJsonObject{{"phrases", QJsonArray::fromStringList(dict.sanitizedVocabulary())}};
    azure["enhancedMode"] = enhanced;
    QJsonObject body;
    body["model"] = kMAIModel;
    body["input_audio"] = QJsonObject{{"data", QString::fromLatin1(flac.toBase64())}, {"format", "flac"}};
    body["provider"] = QJsonObject{{"options", QJsonObject{{"azure", azure}}}};
    openRouterRequest("audio/transcriptions", key, QJsonDocument(body).toJson(QJsonDocument::Compact),
                      TimeoutPolicy::overallDeadline(duration), token, [=](QByteArray data, TranscriptionError err) {
        if (!err.ok()) { cb({}, err); return; }
        const QString text = QJsonDocument::fromJson(data).object().value("text").toString().trimmed();
        if (text.isEmpty()) { cb({}, TranscriptionError::make(TranscriptionErrorKind::EmptyTranscript)); return; }
        cb(TranscriptionResult{text, ReplacementEngine::apply(DictionaryStore::instance().replacementRules(), text),
                               QString(kMAIModel) + "/" + maiStyleKey(style)}, {});
    });
}

} // namespace jot
