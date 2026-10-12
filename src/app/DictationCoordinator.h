// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) — DictationCoordinator.
#pragma once

#include "../core/DictationState.h"
#include "../core/HotkeyProcessor.h"
#include "../core/SessionMeta.h"
#include "../core/TextPipeline.h"
#include "../net/Cancel.h"
#include <QCoreApplication>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <functional>
#include <optional>

namespace jot {

class AudioCapture;
class LiveTranscriber;
class LiveSession;
struct AudioCaptureResult;

class DictationCoordinator : public QObject {
    Q_OBJECT
public:
    explicit DictationCoordinator(QObject *parent = nullptr);

    DictationState state() const { return m_state; }
    float micLevel() const { return m_micLevel; }
    QString lastResult() const { return m_lastResult; }
    QString coachingHint() const { return m_coachingHint; }
    SilenceReason lastSilenceReason() const { return m_lastSilenceReason; }
    TranscriptionProvider lastTranscriptionProvider() const { return m_lastProvider; }
    QString activeSessionFolder() const { return m_session ? m_session->folder : QString(); }

    void clearLastResult() { m_lastResult.clear(); }
    void noteLastResult(const QString &t) { m_lastResult = t; }
    void seedLastResult(const QString &t) { if (m_lastResult.isEmpty()) m_lastResult = t; }
    void forgetLastResult(const QString &t) { if (!t.isEmpty() && m_lastResult == t) m_lastResult.clear(); }

    /// Returns false when a begin was refused (busy / secure input).
    bool handle(HotkeyIntent intent);

    std::function<DictationContext()> contextProvider;
    std::function<void(const SessionMeta &, const QString &)> onSessionUpdate;
    std::function<void(const QUuid &)> onSessionDiscard;

    static constexpr float silencePeakThreshold = 0.06f;
    static constexpr double minimumSendableDuration = 0.4;
    /// Budget for one transcription: the setting, plus 1 s per 10 s of audio beyond 30 s.
    static double transcriptionBudget(double audioSeconds);
    static constexpr double blipHoldThreshold = 0.8;
    static constexpr double emptyTranscriptSNRThreshold = 8;
    static constexpr double discardSNRThreshold = 6;
    static constexpr double recordingWarnSeconds = 540;
    static constexpr double recordingCapSeconds = 600;
    static constexpr double cancelKeepThreshold = 10;
    static QString coachTip() { return QCoreApplication::translate("Jot", "Hold to talk · tap Space while holding for hands-free"); }

signals:
    void stateChanged(const jot::DictationState &state, const jot::DictationState &previous);
    void micLevelChanged(float level);
    void coachingHintChanged(const QString &hint);

private:
    struct Session {
        QUuid id;
        QString folder;
        QDateTime startedAt;
        DictationContext context;
        SessionMeta meta;
        float peakLevel = 1.0f;
    };

    bool beginSession();
    void startCapture(const QUuid &sessionID);
    void finalizeSession();
    void completeFinalize(const QUuid &sessionID, const AudioCaptureResult &result);
    void completeTranscription(const QUuid &sessionID, const TranscriptionResult &outcome, qint64 startedAtMs);
    void failTranscription(const QUuid &sessionID, const TranscriptionError &error);
    void cancelSession(const QString &hint);
    void completeCancel(const Session &cancelled, std::optional<double> duration, bool afterRecording);
    void handleShortTap();
    void handleAccidentalChord();
    void handleWriteFailure();
    void handleEngineDeath(const QString &message);
    void discardSessionArtifacts();
    void setSession(std::optional<Session> s);
    void stopCapTimers();
    void startCapTimers();
    void setHint(const QString &h);
    bool apply(const DictationEvent &event);
    void updateMeta(const std::function<void(SessionMeta &)> &mutate);

    DictationState m_state;
    float m_micLevel = 0;
    QString m_lastResult;
    QString m_coachingHint;
    SilenceReason m_lastSilenceReason = SilenceReason::NoSpeech;
    TranscriptionProvider m_lastProvider = TranscriptionProvider::Gemini;

    std::optional<Session> m_session;
    QPointer<AudioCapture> m_capture;
    QPointer<LiveTranscriber> m_live;
    struct SinkHolder {
        QMutex lock;
        LiveSession *target = nullptr;
        void push(const QByteArray &pcm);
    };
    std::shared_ptr<SinkHolder> m_sinkHolder;
    bool m_liveActiveForSession = false;
    QString m_lastInterim;
    NoiseFloorEstimator m_noiseFloor;
    bool m_noiseHandlingActive = false;
    bool m_pendingLockIn = false;
    bool m_finalizing = false;              // capture stop in progress
    std::function<void(const AudioCaptureResult &)> m_onFinalized; // cancel waiting on finalize
    CancelToken m_inFlight;
    QTimer m_capWarn;
    QTimer m_capStop;
    QTimer m_budget;
    QUuid m_budgetSession;
};

} // namespace jot
