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

class QNetworkInformation;

namespace jot {

class RetryQueue : public QObject {
    Q_OBJECT
public:
    enum class Outcome { Recovered, StillOffline, Blocked, Failed, AlreadyDone, AlreadyRetrying, AudioUnavailable, KeptPreviousTranscript };

    explicit RetryQueue(HistoryStore *store, QObject *parent = nullptr);
    void start();
    void drain(bool announceBlocked = true);
    void retrySingle(const DictationRecord &record, std::function<void(Outcome)> cb);
    void recoverInterrupted(const DictationRecord &record, std::function<void(bool)> cb);
    bool isBusy() const { return m_draining; }
    std::optional<TranscriptionError> lastFailure() const { return m_lastFailure; }
    TranscriptionProvider lastTranscriptionProvider() const { return m_lastProvider; }

    std::function<void(int)> onDrained;
    std::function<void(const QString &)> onTranscriptCopied;
    std::function<void(const TranscriptionError &)> onDrainBlocked;

    static constexpr int periodicDrainIntervalMs = 10 * 60 * 1000;

private:
    enum class Process { Recovered, StillOffline, Blocked, Failed, Skipped, AudioUnavailable, KeptPreviousTranscript };
    void process(const DictationRecord &record, bool reprocess, std::function<void(Process, TranscriptionError)> cb);
    void drainNext(QList<DictationRecord> rest, int recovered, bool announceBlocked);
    void acquireExclusive(std::function<void()> then);
    void finishExclusive();
    void persistFailure(SessionMeta &meta, const QString &folder, const QString &code, const std::optional<QString> &message);
    void queueForDrain(SessionMeta &meta, const QString &folder, const QString &code);
    static bool isCompleted(const QString &folder);

    HistoryStore *m_store;
    bool m_draining = false;
    int m_manualWaiting = 0;
    QSet<QString> m_manualIDs;
    std::optional<bool> m_requestedDrainAnnounces;
    std::optional<TranscriptionError> m_lastFailure;
    TranscriptionProvider m_lastProvider = TranscriptionProvider::Gemini;
    QTimer m_periodic;
    bool m_lastOnline = true;
};

class RecoveryScanner : public QObject {
    Q_OBJECT
public:
    RecoveryScanner(HistoryStore *store, RetryQueue *queue, QObject *parent = nullptr);
    void scanAndRecover(const QDateTime &cutoff, std::function<void()> done);
    std::function<void(const QString &)> onRecovered;

private:
    HistoryStore *m_store;
    RetryQueue *m_queue;
};

namespace RetentionPolicy {
void purgeExpiredAudio(int audioRetentionDays);
}

} // namespace jot
