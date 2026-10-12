// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) to Linux/Qt.
#pragma once

#include "../core/Types.h"
#include "Cancel.h"
#include <QObject>
#include <functional>

namespace jot {

using ResultCallback = std::function<void(const TranscriptionResult &, const TranscriptionError &)>;

/// GeminiTranscriptionService + SelectedTranscriptionService + MAIClient.
///
///   WAV → FLAC → interactions (mode: smart, custom_vocabulary)
///       → [optional] cleanup → validation gate → ReplacementEngine.
///
/// One retry loop for both providers: failed transcriptions retry up to 10
/// attempts 1 s apart; an empty answer is re-sent once.
class TranscriptionService : public QObject {
    Q_OBJECT
public:
    static TranscriptionService &instance();

    void transcribe(const QString &audioPath, double durationSeconds, const DictationContext &context,
                    CancelToken token, ResultCallback cb);

    /// Shared finishing stage for uploads and reconciled live results.
    void finishTranscript(const QString &text, const DictationContext &context, const QString &modelID,
                          const FormattingPolicy &policy, const GeminiConfig &config, CancelToken token,
                          std::function<void(TranscriptionResult)> cb);

    void warmConnection();

    static void validateOpenRouterKey(const QString &key, std::function<void(TranscriptionError)> cb);

    static constexpr int attempts = 10;
    static constexpr int emptyTranscriptAnswers = 2;
    static constexpr double emptyTranscriptMinSeconds = 0.6;

private:
    TranscriptionService() = default;
    void attempt(const QString &audioPath, double duration, const DictationContext &context,
                 const TranscriptionOptions &options, CancelToken token, int attemptNo, int emptyAnswers,
                 ResultCallback cb);
    void transcribeOnce(const QString &audioPath, double duration, const DictationContext &context,
                        const TranscriptionOptions &options, CancelToken token, ResultCallback cb);
    void geminiTranscribe(const QString &audioPath, double duration, const DictationContext &context,
                          const TranscriptionOptions &options, CancelToken token, ResultCallback cb);
    void maiTranscribe(const QString &audioPath, double duration, MAIStyle style, CancelToken token, ResultCallback cb);
    void cleanupOrFallback(const QString &raw, const DictationContext &context, const GeminiConfig &config,
                           CancelToken token, std::function<void(QString)> cb);

    // Vocabulary is suppressed once it has PROVABLY broken a request (keyed on the vocabulary itself).
    QString m_blockedVocabulary;
    bool m_hasBlockedVocabulary = false;
};

} // namespace jot
