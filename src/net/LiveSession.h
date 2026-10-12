// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) to Linux/Qt.
// LiveProtocol + PCMRing + LiveTranscriptionSession + LiveTranscriber + LiveStats.
#pragma once

#include "../core/Types.h"
#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QWebSocket>
#include <functional>
#include <optional>

namespace jot {

struct LiveSetup {
    QString model = QStringLiteral("gemini-3.5-transcribe-live");
    bool smart = true;
    QStringList customVocabulary;
};

/// Bounded audio queue between the capture thread and the socket. Overflow drops
/// the OLDEST audio and is recorded, which makes the live result unusable.
class PCMRing {
public:
    static constexpr int bytesPerSecond = 32000;
    explicit PCMRing(double seconds = 8.0) : m_capacity(std::max(bytesPerSecond, int(seconds * bytesPerSecond))) {}
    void append(const QByteArray &chunk);
    QList<QByteArray> drain();
    void markAccepted(qint64 n) { QMutexLocker l(&m_lock); m_accepted += n; }
    qint64 acceptedBytes() const { QMutexLocker l(&m_lock); return m_accepted; }
    bool didDrop() const { QMutexLocker l(&m_lock); return m_dropped > 0; }
    int droppedChunks() const { QMutexLocker l(&m_lock); return m_droppedChunks; }

private:
    mutable QMutex m_lock;
    int m_capacity;
    QList<QByteArray> m_chunks;
    int m_queued = 0;
    qint64 m_accepted = 0;
    qint64 m_dropped = 0;
    int m_droppedChunks = 0;
};

class LiveStats {
public:
    enum class Fallback { NeverOpened, DroppedMidSession, Truncated, NoFinal };
    static constexpr int failureLimit = 3;
    static constexpr double retryAfter = 10 * 60;
    void recordSuccess();
    void recordFallback(Fallback reason);
    int attempts() const;
    int successes() const;
    int consecutiveFailures() const;
    bool shouldStopTrying() const;
    void clearStreak();
    QString summary() const;
    static Fallback classify(const QString &why);
};

class LiveSession : public QObject {
    Q_OBJECT
public:
    LiveSession(const QString &apiKey, LiveSetup setup, double ringSeconds = 8.0, QObject *parent = nullptr);
    ~LiveSession() override;

    /// Thread-safe: called from the capture thread.
    void enqueue(const QByteArray &pcm);

    /// Opens the socket, sends setup, waits (bounded) for setupComplete.
    void begin(std::function<void(bool ok, QString why)> cb, double setupTimeout = 5.0);

    /// Ends activity and waits for the final transcript.
    void finish(double deadline, std::function<void(std::optional<QString> text, QString why)> cb);
    void abort();

    qint64 acceptedBytes() const { return m_ring.acceptedBytes(); }
    bool didSetup() const { return m_didSetup; }

    static inline const QString setupIncompleteReason = QStringLiteral("setup never completed");
    static inline const QString cancelledReason = QStringLiteral("cancelled");
    static QString endpoint();

signals:
    void partial(const QString &text);

private slots:
    void pump();

private:
    void onText(const QString &message);
    void recordFailure(const QString &why);
    void close();
    void sendJson(const QByteArray &json);
    void completeFinish();

    QString m_apiKey;
    LiveSetup m_setup;
    PCMRing m_ring;
    QWebSocket m_socket;
    QTimer m_setupTimer;
    std::function<void(bool, QString)> m_beginCb;
    std::function<void(std::optional<QString>, QString)> m_finishCb;
    QTimer m_finalTimer;
    QStringList m_finals;
    QString m_failure;
    bool m_didSetup = false;
    bool m_streaming = false;
    bool m_endRequested = false;
    bool m_activityEndFlushed = false;
    bool m_closed = false;
};

/// LiveTranscriber: stats, the byte reconciliation, and the shared finishing stage.
class LiveTranscriber : public QObject {
    Q_OBJECT
public:
    /// Nil when live is not applicable for these options (provider, legacy, back-off, no key).
    static LiveTranscriber *create(const DictationContext &context, double ringSeconds = 8.0, QObject *parent = nullptr);

    void enqueue(const QByteArray &pcm) { m_session->enqueue(pcm); }
    void begin(std::function<void(bool)> cb);
    void finish(double deadline, qint64 framesWritten, std::function<void(std::optional<TranscriptionResult>)> cb);
    void abort() { m_session->abort(); }
    LiveSession *session() const { return m_session; }

private:
    LiveTranscriber(LiveSession *session, DictationContext context, QObject *parent);
    LiveSession *m_session;
    DictationContext m_context;
    LiveStats m_stats;
};

} // namespace jot
