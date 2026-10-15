// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) — RetryQueue,
// RecoveryScanner and RetentionPolicy.
#pragma once

#include "../core/HistoryStore.h"
#include "../core/Types.h"
#include <QObject>
#include <QSet>
#include <QTimer>
#include <functional>
#include <optional>

namespace jot {

/// Manual "Retry Transcription" from History. There is no background queue:
/// a dictation is transcribed right away or not at all; History keeps the audio
/// briefly so it can be inspected and retried by hand.
class RetryQueue : public QObject {
    Q_OBJECT
public:
    enum class Outcome { Recovered, StillOffline, Blocked, Failed, AlreadyDone, AlreadyRetrying, AudioUnavailable, KeptPreviousTranscript };

    explicit RetryQueue(HistoryStore *store, QObject *parent = nullptr);
    void retrySingle(const DictationRecord &record, std::function<void(Outcome)> cb);
    bool isBusy() const { return m_draining; }
    std::optional<TranscriptionError> lastFailure() const { return m_lastFailure; }
    TranscriptionProvider lastTranscriptionProvider() const { return m_lastProvider; }

    std::function<void(const QString &)> onTranscriptCopied;

private:
    enum class Process { Recovered, StillOffline, Blocked, Failed, Skipped, AudioUnavailable, KeptPreviousTranscript };
    void process(const DictationRecord &record, bool reprocess, std::function<void(Process, TranscriptionError)> cb);
    void acquireExclusive(std::function<void()> then);
    void finishExclusive();
    void persistFailure(SessionMeta &meta, const QString &folder, const QString &code, const std::optional<QString> &message);
    static bool isCompleted(const QString &folder);

    HistoryStore *m_store;
    bool m_draining = false;
    int m_manualWaiting = 0;
    QSet<QString> m_manualIDs;
    std::optional<TranscriptionError> m_lastFailure;
    TranscriptionProvider m_lastProvider = TranscriptionProvider::Gemini;
};

class RecoveryScanner : public QObject {
    Q_OBJECT
public:
    RecoveryScanner(HistoryStore *store, QObject *parent = nullptr);
    void scanAndRecover(const QDateTime &cutoff, std::function<void()> done);
    std::function<void(const QString &)> onRecovered;

private:
    HistoryStore *m_store;
};

namespace RetentionPolicy {
void purgeExpiredAudio(int audioRetentionDays);
/// Deletes whole History entries (text, audio, folder) older than `hours`.
/// Everything expires, stuck or queued entries included; only the session being recorded right now is spared.
int purgeExpiredTranscripts(HistoryStore *store, int hours, const QString &activeFolder);
}

} // namespace jot
