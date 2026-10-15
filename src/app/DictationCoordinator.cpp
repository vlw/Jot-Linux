// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) — DictationCoordinator.
#include "DictationCoordinator.h"
#include "../audio/AudioCapture.h"
#include "../core/FileLayout.h"
#include "../core/Settings.h"
#include "../net/LiveSession.h"
#include "../net/TranscriptionService.h"
#include "../platform/Inserter.h"
#include "../platform/MediaPause.h"

#include <QCoreApplication>
#include <QDateTime>
#include <algorithm>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMutexLocker>

Q_LOGGING_CATEGORY(lcSession, "jot.session")

namespace jot {

using EK = DictationEvent::Kind;
using SK = DictationState::Kind;

void DictationCoordinator::SinkHolder::push(const QByteArray &pcm)
{
    QMutexLocker l(&lock);
    if (target) target->enqueue(pcm);
}

DictationCoordinator::DictationCoordinator(QObject *parent)
    : QObject(parent)
{
    m_capWarn.setSingleShot(true);
    m_capStop.setSingleShot(true);
    connect(&m_capWarn, &QTimer::timeout, this, [this] {
        if (m_state.isRecording()) setHint(QCoreApplication::translate("Jot", "One minute left — 10-minute limit"));
    });
    m_budget.setSingleShot(true);
    connect(&m_budget, &QTimer::timeout, this, [this] {
        // Waited long enough: stop every request and retry, keep the audio in History.
        if (!m_session || m_session->id != m_budgetSession || m_state.kind != SK::Transcribing) return;
        qCWarning(lcSession) << "transcription budget exceeded — giving up, audio kept in History";
        if (m_inFlight) { m_inFlight->cancel(); m_inFlight.reset(); }
        failTranscription(m_budgetSession, TranscriptionError::make(TranscriptionErrorKind::Timeout, "budget"));
    });
    connect(&m_capStop, &QTimer::timeout, this, [this] {
        if (!m_state.isRecording()) return;
        qCInfo(lcSession) << "recording cap reached — finalizing";
        finalizeSession();
    });
}

void DictationCoordinator::setHint(const QString &h)
{
    m_coachingHint = h;
    emit coachingHintChanged(h);
}

bool DictationCoordinator::handle(HotkeyIntent intent)
{
    switch (intent) {
    case HotkeyIntent::Begin:
        return beginSession();
    case HotkeyIntent::LockIn:
        if (m_state.kind == SK::Warming) {
            m_pendingLockIn = true;
            return true;
        }
        return apply(DictationEvent::of(EK::LockIn));
    case HotkeyIntent::Finalize:
        finalizeSession();
        return true;
    case HotkeyIntent::Cancel:
        cancelSession({});
        return true;
    case HotkeyIntent::ShortTapHint:
        handleShortTap();
        return true;
    case HotkeyIntent::AbortAccidental:
        handleAccidentalChord();
        return true;
    }
    return true;
}

void DictationCoordinator::handleAccidentalChord()
{
    if (m_state.kind == SK::Recording && m_state.locked) finalizeSession();
    else if (m_state.kind == SK::Warming || m_state.kind == SK::Recording) cancelSession({});
    else if (m_state.isInFlight()) qCInfo(lcSession) << "accidental chord ignored — session in flight";
}

void DictationCoordinator::handleShortTap()
{
    if (m_state.kind == SK::Recording && m_state.locked) finalizeSession();
    else if (m_state.kind == SK::Warming || m_state.kind == SK::Recording) cancelSession(coachTip());
    else if (m_state.isInFlight()) qCInfo(lcSession) << "short tap ignored — session in flight";
    else setHint(coachTip());
}

void DictationCoordinator::setSession(std::optional<Session> s)
{
    const bool changed = (m_session.has_value() != s.has_value()) || (m_session && s && m_session->id != s->id);
    m_session = std::move(s);
    if (!changed) return;
    m_lastInterim.clear();
    if (m_sinkHolder) {
        QMutexLocker l(&m_sinkHolder->lock);
        m_sinkHolder->target = nullptr;
    }
    m_sinkHolder.reset();
    if (m_live) {
        LiveTranscriber *live = m_live;
        m_live = nullptr;
        live->abort();
        live->deleteLater();
    }
}

bool DictationCoordinator::beginSession()
{
    if (!(m_state.kind == SK::Idle || m_state.isTerminal())) {
        qCInfo(lcSession) << "begin ignored: session already active (" << m_state.debugName() << ")";
        return false;
    }
    // Linux has no system-wide secure input; password fields are not detectable on Wayland.
    m_state = DictationState::idle();
    m_coachingHint.clear();
    m_pendingLockIn = false;
    apply(DictationEvent::of(EK::HotkeyBegin));

    const QUuid id = QUuid::createUuid();
    const QDateTime startedAt = QDateTime::currentDateTime();
    const QString folder = FileLayout::makeSessionFolder(id, startedAt);
    if (folder.isEmpty()) {
        qCWarning(lcSession) << "session setup failed: cannot create folder";
        apply(DictationEvent::fail(EK::EngineFailed, DictationFailure::Storage));
        return false;
    }
    SessionMeta meta;
    meta.id = id;
    meta.startedAt = startedAt;
    meta.status = SessionMeta::Status::Recording;
    const DictationContext context = contextProvider ? contextProvider() : DictationContext{};
    m_lastProvider = context.options ? context.options->provider : TranscriptionProvider::Gemini;
    meta.targetAppBundleID = context.targetAppClass;
    meta.targetAppName = context.targetAppName;
    meta.write(folder);
    setSession(Session{id, folder, startedAt, context, meta, 1.0f});

    m_noiseFloor = NoiseFloorEstimator();
    m_noiseHandlingActive = Settings::instance().experimentalNoiseHandling();

    auto *capture = new AudioCapture(this);
    m_capture = capture;
    connect(capture, &AudioCapture::level, this, [this, id](float level) {
        if (!m_session || m_session->id != id) return;
        m_micLevel = level;
        m_noiseFloor.ingest(level);
        emit micLevelChanged(level);
    });
    connect(capture, &AudioCapture::writeFailure, this, [this, id] {
        if (m_session && m_session->id == id) handleWriteFailure();
    });
    connect(capture, &AudioCapture::engineDied, this, [this, id](const QString &message) {
        if (m_session && m_session->id == id) handleEngineDeath(message);
    });
    connect(capture, &AudioCapture::captureActivityChanged, &MediaPause::instance(), &MediaPause::captureActivityChanged);

    // Warm TLS while the user speaks (batch path).
    TranscriptionService::instance().warmConnection();
    QMetaObject::invokeMethod(this, [this, id] { startCapture(id); }, Qt::QueuedConnection);
    return true;
}

void DictationCoordinator::startCapture(const QUuid &sessionID)
{
    if (!m_session || m_session->id != sessionID || !m_capture || m_state.kind != SK::Warming) {
        qCInfo(lcSession) << "engine start skipped — session moved on before the mic came up";
        return;
    }
    LiveTranscriber *live = LiveTranscriber::create(m_session->context, 8.0, this);
    m_live = live;
    m_liveActiveForSession = live != nullptr;
    std::function<void(const QByteArray &)> sink;
    if (live) {
        LiveSession *raw = live->session();
        // Called on the capture thread. The holder is cleared (under its lock)
        // before the live session is torn down, so a late chunk never lands on a
        // deleted session.
        m_sinkHolder = std::make_shared<SinkHolder>();
        m_sinkHolder->target = raw;
        auto holder = m_sinkHolder;
        sink = [holder](const QByteArray &pcm) { holder->push(pcm); };
        connect(raw, &LiveSession::partial, this, [this, sessionID](const QString &text) {
            if (m_session && m_session->id == sessionID) m_lastInterim = text;
        });
        live->begin([this, sessionID](bool ok) {
            if (!ok && m_session && m_session->id == sessionID) m_liveActiveForSession = false;
        });
    }
    const auto err = m_capture->start(FileLayout::audioFile(m_session->folder), sink);
    if (err != AudioCapture::StartError::None) {
        qCWarning(lcSession) << "audio engine failed to start";
        const DictationFailure failure = err == AudioCapture::StartError::NoInputDevice ? DictationFailure::NoMicrophone
                                                                                        : DictationFailure::Audio;
        apply(DictationEvent::fail(EK::EngineFailed, failure));
        if (m_capture) m_capture->deleteLater();
        m_capture = nullptr;
        discardSessionArtifacts();
        setSession(std::nullopt);
        return;
    }
    apply(DictationEvent::of(EK::EngineStarted));
    if (m_pendingLockIn) {
        m_pendingLockIn = false;
        apply(DictationEvent::of(EK::LockIn));
    }
    startCapTimers();
}

void DictationCoordinator::startCapTimers()
{
    m_capWarn.start(int(recordingWarnSeconds * 1000));
    m_capStop.start(int(recordingCapSeconds * 1000));
}

void DictationCoordinator::stopCapTimers()
{
    m_capWarn.stop();
    m_capStop.stop();
}

void DictationCoordinator::handleWriteFailure()
{
    if (!m_state.isRecording()) return;
    qCWarning(lcSession) << "sustained disk write failures — finalizing with what we have";
    updateMeta([](SessionMeta &m) { m.errorCode = QString("disk_write"); });
    finalizeSession();
}

void DictationCoordinator::handleEngineDeath(const QString &message)
{
    if (!m_state.isRecording()) return;
    qCWarning(lcSession) << "audio engine died mid-recording (" << message << ") — finalizing with what we have";
    updateMeta([](SessionMeta &m) { m.errorCode = QString("engine_died"); });
    setHint(QCoreApplication::translate("Jot", "%1 — dictating what was captured").arg(message));
    finalizeSession();
}

void DictationCoordinator::finalizeSession()
{
    if (!m_session) return;
    if (!apply(DictationEvent::of(EK::Finalize))) return;
    stopCapTimers();
    QPointer<AudioCapture> engine = m_capture;
    m_capture = nullptr;
    m_micLevel = 0;
    emit micLevelChanged(0);
    const QUuid id = m_session->id;
    m_finalizing = true;
    auto done = [this, id, engine](const AudioCaptureResult &result) {
        m_finalizing = false;
        if (engine) engine->deleteLater();
        if (m_onFinalized) {
            auto cb = std::exchange(m_onFinalized, nullptr);
            cb(result);
        }
        completeFinalize(id, result);
    };
    if (engine) engine->stop(done);
    else done(AudioCaptureResult{0, 0, {}, 0, false});
}

void DictationCoordinator::completeFinalize(const QUuid &sessionID, const AudioCaptureResult &result)
{
    if (!m_session || m_session->id != sessionID) return;
    Session &session = *m_session;
    const double heldFor = session.startedAt.msecsTo(QDateTime::currentDateTime()) / 1000.0;
    if (result.framesWritten <= 0) {
        if (heldFor < blipHoldThreshold) {
            m_lastSilenceReason = SilenceReason::NoSpeech;
            apply(DictationEvent::of(EK::SilenceOnly));
        } else {
            apply(DictationEvent::of(EK::NoAudioCaptured));
        }
        discardSessionArtifacts();
        setSession(std::nullopt);
        return;
    }
    session.peakLevel = result.peakLevel;
    const auto roomFloor = m_noiseFloor.floorDB();
    const std::optional<double> speechPeak = roomFloor ? std::optional<double>(m_noiseFloor.peakDB()) : std::nullopt;
    const auto separation = m_noiseFloor.measuredSNR();
    updateMeta([&](SessionMeta &m) {
        m.status = SessionMeta::Status::Recorded;
        m.audioDurationSeconds = result.durationSeconds;
        m.gapMarkers = result.gapMarkers;
        m.noiseFloorDBFS = roomFloor;
        m.speechPeakDBFS = speechPeak;
    });
    if (roomFloor && speechPeak && separation)
        qCInfo(lcSession) << "room" << *roomFloor << "dBFS, speech" << *speechPeak << "dBFS, separation" << *separation << "dB";

    if (result.durationSeconds < std::max(minimumSendableDuration, Settings::instance().minimumRecordingSeconds())) {
        // An accidental press: never worth an API round trip.
        m_lastSilenceReason = SilenceReason::TooShort;
        apply(DictationEvent::of(EK::SilenceOnly));
        discardSessionArtifacts();
        setSession(std::nullopt);
        return;
    }
    const bool roseAboveRoom = separation.value_or(std::numeric_limits<double>::infinity()) >= discardSNRThreshold;
    if (!result.peakIsTrustworthy) {
        qCInfo(lcSession) << "peak unmeasured — uploading rather than guessing silence";
    } else if (result.peakLevel < silencePeakThreshold && !roseAboveRoom) {
        m_lastSilenceReason = SilenceReason::NoSpeech;
        apply(DictationEvent::of(EK::SilenceOnly));
        discardSessionArtifacts();
        setSession(std::nullopt);
        return;
    }
    apply(DictationEvent::of(EK::AudioFinalized));
    m_budgetSession = sessionID;
    m_budget.start(int(transcriptionBudget(result.durationSeconds) * 1000));

    const qint64 startedAtMs = QDateTime::currentMSecsSinceEpoch();
    CancelToken token = makeToken();
    m_inFlight = token;
    const QString audioPath = FileLayout::audioFile(session.folder);
    const DictationContext context = session.context;
    const double duration = result.durationSeconds;

    auto upload = [this, sessionID, token, audioPath, context, duration, startedAtMs] {
        if (token->cancelled) return;
        TranscriptionService::instance().transcribe(audioPath, duration, context, token,
            [this, sessionID, token, startedAtMs](const TranscriptionResult &r, const TranscriptionError &e) {
                if (token->cancelled) return;
                if (e.ok()) completeTranscription(sessionID, r, startedAtMs);
                else failTranscription(sessionID, e);
            });
    };

    if (m_liveActiveForSession && m_live) {
        QPointer<LiveTranscriber> live = m_live;
        live->finish(TimeoutPolicy::liveFinal, result.framesWritten,
                     [this, sessionID, token, upload, startedAtMs](std::optional<TranscriptionResult> liveResult) {
            if (token->cancelled) return;
            if (liveResult) {
                completeTranscription(sessionID, *liveResult, startedAtMs);
                return;
            }
            qCInfo(lcSession) << "live result unusable — uploading the recording instead";
            upload();
        });
        return;
    }
    upload();
}

double DictationCoordinator::transcriptionBudget(double audioSeconds)
{
    return Settings::instance().transcriptionTimeoutSeconds() + std::max(0.0, audioSeconds - 30.0) / 10.0;
}

void DictationCoordinator::completeTranscription(const QUuid &sessionID, const TranscriptionResult &outcome, qint64 startedAtMs)
{
    if (!m_session || m_session->id != sessionID) return; // stale completion
    m_budget.stop();
    updateMeta([&](SessionMeta &m) {
        m.rawTranscript = outcome.rawTranscript;
        m.cleanedTranscript = outcome.cleanedTranscript;
        m.modelID = outcome.modelID;
        m.status = SessionMeta::Status::Transcribing;
    });
    apply(DictationEvent::of(EK::TranscriptReady));
    const DictationContext context = m_session->context;
    Inserter::instance().insert(outcome.cleanedTranscript, context,
                                [this, sessionID, outcome, startedAtMs](InsertionOutcome insertion) {
        if (!m_session || m_session->id != sessionID) return;
        const double pipeline = (QDateTime::currentMSecsSinceEpoch() - startedAtMs) / 1000.0;
        SessionMeta::Status status = SessionMeta::Status::Inserted;
        EK event = EK::Inserted;
        switch (insertion) {
        case InsertionOutcome::Inserted: status = SessionMeta::Status::Inserted; event = EK::Inserted; break;
        case InsertionOutcome::FrontmostChanged: status = SessionMeta::Status::AwaitingChip; event = EK::FrontmostChangedAwaitingChip; break;
        case InsertionOutcome::FellBackToClipboard: status = SessionMeta::Status::CopiedToClipboard; event = EK::InsertionFellBackToClipboard; break;
        case InsertionOutcome::BlockedSecureField: status = SessionMeta::Status::HeldSecure; event = EK::InsertionBlockedSecure; break;
        }
        updateMeta([&](SessionMeta &m) { m.status = status; m.pipelineSeconds = pipeline; });
        m_lastResult = outcome.cleanedTranscript;
        apply(DictationEvent::of(event));
        setSession(std::nullopt);
    });
}

void DictationCoordinator::failTranscription(const QUuid &sessionID, const TranscriptionError &error)
{
    if (!m_session || m_session->id != sessionID) return;
    m_budget.stop();
    if (error.kind == TranscriptionErrorKind::EmptyTranscript) {
        const float peak = m_session->peakLevel;
        const double duration = m_session->meta.audioDurationSeconds.value_or(0);
        if (peak < silencePeakThreshold || duration < 0.6) {
            m_lastSilenceReason = SilenceReason::NoSpeech;
            apply(DictationEvent::of(EK::SilenceOnly));
            discardSessionArtifacts();
            setSession(std::nullopt);
            return;
        }
        const auto snr = m_noiseFloor.measuredSNR();
        if (m_noiseHandlingActive && snr && *snr < emptyTranscriptSNRThreshold) {
            qCInfo(lcSession) << "empty transcript with only" << *snr << "dB above the room — no speech, not a failure";
            updateMeta([](SessionMeta &m) { m.status = SessionMeta::Status::Failed; m.errorCode = QString("tooNoisy"); });
            m_lastSilenceReason = SilenceReason::TooNoisy;
            apply(DictationEvent::of(EK::SilenceOnly));
            setSession(std::nullopt);
            return;
        }
    }
    DictationFailure failure = DictationFailure::Network;
    QString code = "unknown";
    std::optional<QString> detail;
    switch (error.kind) {
    case TranscriptionErrorKind::BadRequest: failure = DictationFailure::BadRequest; code = "bad_request"; detail = error.detail; break;
    case TranscriptionErrorKind::ModelUnavailable:
        failure = DictationFailure::ModelAccess; code = "model";
        detail = error.detail.isEmpty() ? QString("model %1 not accessible").arg(error.model) : error.detail;
        break;
    case TranscriptionErrorKind::Offline:
    case TranscriptionErrorKind::Network: failure = DictationFailure::Network; code = "network"; break;
    case TranscriptionErrorKind::Auth: failure = DictationFailure::Auth; code = "auth"; break;
    case TranscriptionErrorKind::RateLimitedDaily: failure = DictationFailure::QuotaExhausted; code = "quota"; break;
    case TranscriptionErrorKind::RateLimitedTransient: failure = DictationFailure::RateLimited; code = "rate_limit"; break;
    case TranscriptionErrorKind::Timeout:
        failure = DictationFailure::Timeout;
        // The user's wait budget ran out: kept for a manual Retry, never retried in the background.
        code = error.detail == "budget" ? "gave_up" : "timeout";
        break;
    case TranscriptionErrorKind::EmptyTranscript: failure = DictationFailure::Validation; code = "empty"; break;
    case TranscriptionErrorKind::SafetyBlocked: failure = DictationFailure::SafetyBlocked; code = "safety"; break;
    default: break;
    }
    updateMeta([&](SessionMeta &m) { m.status = SessionMeta::Status::Failed; m.errorCode = code; m.errorMessage = detail; });
    apply(DictationEvent::fail(EK::TranscriptFailed, failure));
    setSession(std::nullopt);
}

void DictationCoordinator::cancelSession(const QString &hint)
{
    const bool afterRecording = m_state.isInFlight();
    if (!apply(DictationEvent::of(EK::Cancel))) {
        setHint(hint);
        return;
    }
    if (m_inFlight) { m_inFlight->cancel(); m_inFlight.reset(); }
    m_budget.stop();
    m_micLevel = 0;
    emit micLevelChanged(0);
    stopCapTimers();
    setHint(hint);
    if (!m_session) return;
    const Session cancelled = *m_session;
    setSession(std::nullopt); // detach now; delayed cleanup owns only `cancelled`

    if (m_capture) {
        QPointer<AudioCapture> engine = m_capture;
        m_capture = nullptr;
        engine->stop([this, cancelled, afterRecording, engine](AudioCaptureResult r) {
            if (engine) engine->deleteLater();
            completeCancel(cancelled, r.durationSeconds, afterRecording);
        });
        return;
    }
    if (m_finalizing) {
        m_onFinalized = [this, cancelled, afterRecording](const AudioCaptureResult &r) {
            completeCancel(cancelled, r.durationSeconds, afterRecording);
        };
        return;
    }
    completeCancel(cancelled, std::nullopt, afterRecording);
}

void DictationCoordinator::completeCancel(const Session &cancelled, std::optional<double> duration, bool afterRecording)
{
    if (!QFileInfo::exists(cancelled.folder)) return;
    // Re-read: a finalize that raced the cancel may have updated the meta on disk.
    SessionMeta meta = SessionMeta::read(cancelled.folder).value_or(cancelled.meta);
    const double d = duration.value_or(meta.audioDurationSeconds.value_or(0));
    const bool hasTranscript = meta.rawTranscript.has_value();
    if (afterRecording && !hasTranscript) {
        meta.status = SessionMeta::Status::Failed;
        meta.errorCode = QString("cancelled");
        if (!meta.audioDurationSeconds) meta.audioDurationSeconds = d;
        meta.write(cancelled.folder);
        if (onSessionUpdate) onSessionUpdate(meta, cancelled.folder);
    } else if (hasTranscript || d >= cancelKeepThreshold) {
        meta.status = SessionMeta::Status::Cancelled;
        if (!meta.audioDurationSeconds) meta.audioDurationSeconds = d;
        meta.write(cancelled.folder);
        if (onSessionUpdate) onSessionUpdate(meta, cancelled.folder);
    } else {
        QDir(cancelled.folder).removeRecursively();
        if (onSessionDiscard) onSessionDiscard(cancelled.id);
    }
}

void DictationCoordinator::discardSessionArtifacts()
{
    if (!m_session) return;
    QDir(m_session->folder).removeRecursively();
    if (onSessionDiscard) onSessionDiscard(m_session->id);
}

bool DictationCoordinator::apply(const DictationEvent &event)
{
    const auto next = transition(m_state, event);
    if (!next) {
        qCDebug(lcSession) << "ignored event" << int(event.kind) << "in" << m_state.debugName();
        return false;
    }
    const DictationState previous = m_state;
    m_state = *next;
    qCInfo(lcSession) << "state →" << m_state.debugName();
    emit stateChanged(m_state, previous);
    return true;
}

void DictationCoordinator::updateMeta(const std::function<void(SessionMeta &)> &mutate)
{
    if (!m_session) return;
    mutate(m_session->meta);
    m_session->meta.write(m_session->folder);
    if (onSessionUpdate) onSessionUpdate(m_session->meta, m_session->folder);
}

} // namespace jot
