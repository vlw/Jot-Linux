// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) to Linux/Qt.
#pragma once

#include "Types.h"
#include <optional>

namespace jot {

struct DictationState {
    enum class Kind { Idle, Warming, Recording, Finalizing, Transcribing, Inserting, Done, Cancelled, Failed };
    Kind kind = Kind::Idle;
    bool locked = false;                     // Recording
    DictationOutcome outcome{};              // Done
    DictationFailure failure{};              // Failed

    static DictationState idle() { return {}; }
    static DictationState make(Kind k) { DictationState s; s.kind = k; return s; }
    static DictationState recording(bool locked) { DictationState s; s.kind = Kind::Recording; s.locked = locked; return s; }
    static DictationState done(DictationOutcome o) { DictationState s; s.kind = Kind::Done; s.outcome = o; return s; }
    static DictationState failed(DictationFailure f) { DictationState s; s.kind = Kind::Failed; s.failure = f; return s; }

    bool isTerminal() const { return kind == Kind::Done || kind == Kind::Cancelled || kind == Kind::Failed; }
    bool isRecording() const { return kind == Kind::Recording; }
    bool isInFlight() const { return kind == Kind::Finalizing || kind == Kind::Transcribing || kind == Kind::Inserting; }
    bool operator==(const DictationState &o) const {
        if (kind != o.kind) return false;
        switch (kind) {
        case Kind::Recording: return locked == o.locked;
        case Kind::Done: return outcome == o.outcome;
        case Kind::Failed: return failure == o.failure;
        default: return true;
        }
    }
    QString debugName() const;
};

struct DictationEvent {
    enum class Kind {
        HotkeyBegin, LockIn, Finalize, Cancel, AbortAccidental,
        EngineStarted, EngineFailed, AudioFinalized, NoAudioCaptured, SilenceOnly,
        TranscriptReady, TranscriptFailed, QueuedForRetry,
        Inserted, InsertionFellBackToClipboard, FrontmostChangedAwaitingChip, InsertionBlockedSecure,
    };
    Kind kind;
    DictationFailure failure{};
    static DictationEvent of(Kind k) { return {k, {}}; }
    static DictationEvent fail(Kind k, DictationFailure f) { return {k, f}; }
};

/// Pure transition table — identical to upstream DictationStateMachine.
std::optional<DictationState> transition(const DictationState &state, const DictationEvent &event);

} // namespace jot
