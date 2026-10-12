// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) to Linux/Qt.
#pragma once

#include "../core/Types.h"
#include "Cancel.h"
#include <QNetworkAccessManager>
#include <QObject>
#include <functional>

namespace jot {

/// Low-level Gemini API client: the same three surfaces and the same HTTP →
/// TranscriptionError mapping as upstream (that mapping must never fork per
/// endpoint — RetryQueue branches on it).
class GeminiClient : public QObject {
    Q_OBJECT
public:
    using TextCallback = std::function<void(const QString &text, const TranscriptionError &error)>;

    static GeminiClient &instance();
    QNetworkAccessManager *network() { return &m_nam; }

    /// Legacy :generateContent transcription (verbatim only).
    void transcribe(const QByteArray &flac, const QString &model, const QString &endpoint, double deadline,
                    const QStringList &customVocabulary, CancelToken token, TextCallback cb);
    /// /v1beta/interactions — the only place mode "smart" works.
    void transcribeInteraction(const QByteArray &audio, const QString &mimeType, const QString &model,
                               const QString &endpoint, TranscriptionMode mode, const QStringList &customVocabulary,
                               double deadline, CancelToken token, TextCallback cb);
    void cleanup(const QString &prompt, const QString &model, const QString &endpoint, double deadline,
                 CancelToken token, TextCallback cb);

    enum class KeyCheck { Valid, Rejected, Unreachable };
    void validateKey(const QString &endpoint, const QString &key, std::function<void(KeyCheck, QString)> cb);
    void resolveAvailableModel(const QStringList &candidates, const QString &endpoint, const QString &key,
                               std::function<void(QString)> cb);
    void warmConnection(const QString &model, const QString &endpoint);

    static QString errorMessage(const QByteArray &data);
    static QStringList errorReasons(const QByteArray &data);
    static QJsonObject errorObject(const QByteArray &data);

private:
    GeminiClient() = default;
    void post(const QString &path, const QByteArray &body, const QString &endpoint, double deadline,
              const QString &modelLabel, bool modelIsInPath, CancelToken token,
              std::function<void(QByteArray, TranscriptionError)> cb);
    QNetworkAccessManager m_nam;
};

QByteArray encodeFLACFromWav(const QString &wavPath, QString *error = nullptr);
QByteArray readPCMFromWav(const QString &wavPath);

} // namespace jot
