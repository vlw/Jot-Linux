// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) to Linux/Qt.
#pragma once

#include <optional>
#include <vector>

namespace jot {

/// What the hotkey layer asks the session coordinator to do.
enum class HotkeyIntent { Begin, LockIn, Finalize, Cancel, ShortTapHint, AbortAccidental };

namespace HotkeyTuning {
constexpr double holdThreshold = 0.30;
constexpr double doubleTapWindow = 0.50;
constexpr double interruptionWindow = 1.0;
}

/// The pure hotkey grammar — line-for-line port of upstream HotkeyProcessor:
///   hold >= 0.3s            -> push-to-talk: key-up finalizes
///   hold + tap Space        -> hands-free lock: press again finalizes
///   tap, tap (opt-in)       -> hands-free lock
///   single tap              -> coaching hint, session quietly cancelled
///   Esc                     -> cancel
///   other key < 1s in       -> accidental chord, silent abort
class HotkeyProcessor {
public:
    enum class Event { HotkeyDown, HotkeyUp, EscDown, OtherKeyDown, SpaceLock, DoubleTapTimeout };

    struct Effects {
        std::vector<HotkeyIntent> intents;
        std::optional<double> armTimer;
        bool disarmTimer = false;
    };

    enum class Phase { Idle, Pressed, PendingSecondTap, Locked };

    Effects handle(Event event, double now);
    void reset() { m_phase = Phase::Idle; m_swallowNextUp = false; }

    bool doubleTapLockEnabled = false;
    bool isKeyHeld() const { return m_phase == Phase::Pressed; }
    bool isSessionActive() const { return m_phase != Phase::Idle; }
    Phase phase() const { return m_phase; }

private:
    Phase m_phase = Phase::Idle;
    double m_downAt = 0;
    double m_sessionStartAt = 0;
    bool m_swallowNextUp = false;
};

} // namespace jot
