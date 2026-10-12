// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) to Linux/Qt.
#include "DictationState.h"

namespace jot {

using K = DictationState::Kind;
using E = DictationEvent::Kind;

std::optional<DictationState> transition(const DictationState &s, const DictationEvent &e)
{
    switch (s.kind) {
    case K::Idle:
        if (e.kind == E::HotkeyBegin) return DictationState::make(K::Warming);
        break;
    case K::Warming:
        if (e.kind == E::EngineStarted) return DictationState::recording(false);
        if (e.kind == E::EngineFailed) return DictationState::failed(e.failure);
        if (e.kind == E::Cancel || e.kind == E::AbortAccidental) return DictationState::make(K::Cancelled);
        // Released before the engine reported started: still a real dictation.
        if (e.kind == E::Finalize) return DictationState::make(K::Finalizing);
        break;
    case K::Recording:
        if (e.kind == E::LockIn && !s.locked) return DictationState::recording(true);
        if (e.kind == E::Finalize) return DictationState::make(K::Finalizing);
        if (e.kind == E::Cancel || e.kind == E::AbortAccidental) return DictationState::make(K::Cancelled);
        // Mid-recording engine death: whatever hit the disk is preserved.
        if (e.kind == E::EngineFailed) return DictationState::make(K::Finalizing);
        break;
    case K::Finalizing:
        if (e.kind == E::AudioFinalized) return DictationState::make(K::Transcribing);
        if (e.kind == E::NoAudioCaptured) return DictationState::failed(DictationFailure::NoAudio);
        if (e.kind == E::SilenceOnly) return DictationState::done(DictationOutcome::Silent);
        if (e.kind == E::Cancel) return DictationState::make(K::Cancelled);
        break;
    case K::Transcribing:
        if (e.kind == E::TranscriptReady) return DictationState::make(K::Inserting);
        if (e.kind == E::SilenceOnly) return DictationState::done(DictationOutcome::Silent);
        if (e.kind == E::TranscriptFailed) return DictationState::failed(e.failure);
        if (e.kind == E::QueuedForRetry) return DictationState::done(DictationOutcome::QueuedForRetry);
        if (e.kind == E::Cancel) return DictationState::make(K::Cancelled);
        break;
    case K::Inserting:
        // No cancel here: the text exists, History has it regardless.
        if (e.kind == E::Inserted) return DictationState::done(DictationOutcome::Inserted);
        if (e.kind == E::InsertionFellBackToClipboard) return DictationState::done(DictationOutcome::CopiedToClipboard);
        if (e.kind == E::FrontmostChangedAwaitingChip) return DictationState::done(DictationOutcome::AwaitingChip);
        if (e.kind == E::InsertionBlockedSecure) return DictationState::done(DictationOutcome::HeldForSecureField);
        break;
    case K::Done:
    case K::Cancelled:
    case K::Failed:
        break;
    }
    return std::nullopt;
}

QString DictationState::debugName() const
{
    switch (kind) {
    case K::Idle: return "idle";
    case K::Warming: return "warming";
    case K::Recording: return locked ? "recording(locked)" : "recording";
    case K::Finalizing: return "finalizing";
    case K::Transcribing: return "transcribing";
    case K::Inserting: return "inserting";
    case K::Done: return QString("done(%1)").arg(int(outcome));
    case K::Cancelled: return "cancelled";
    case K::Failed: return QString("failed(%1)").arg(int(failure));
    }
    return "?";
}

QString TranscriptionError::describe() const
{
    switch (kind) {
    case TranscriptionErrorKind::None: return "ok";
    case TranscriptionErrorKind::Offline: return "offline";
    case TranscriptionErrorKind::Network: return "network(" + detail + ")";
    case TranscriptionErrorKind::BadRequest: return "badRequest(" + detail + ")";
    case TranscriptionErrorKind::Auth: return "auth";
    case TranscriptionErrorKind::ModelUnavailable: return "modelUnavailable(" + model + ")";
    case TranscriptionErrorKind::RateLimitedDaily: return "rateLimitedDaily";
    case TranscriptionErrorKind::RateLimitedTransient: return "rateLimitedTransient";
    case TranscriptionErrorKind::Timeout: return "timeout";
    case TranscriptionErrorKind::EmptyTranscript: return "emptyTranscript";
    case TranscriptionErrorKind::SafetyBlocked: return "safetyBlocked";
    case TranscriptionErrorKind::Cancelled: return "cancelled";
    }
    return "?";
}

} // namespace jot
