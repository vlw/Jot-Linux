// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) to Linux/Qt.
#include "HotkeyProcessor.h"

namespace jot {

HotkeyProcessor::Effects HotkeyProcessor::handle(Event event, double now)
{
    Effects fx;
    switch (m_phase) {
    case Phase::Idle:
        if (event == Event::HotkeyDown) {
            m_phase = Phase::Pressed;
            m_downAt = now;
            m_sessionStartAt = now;
            m_swallowNextUp = false;
            fx.intents = {HotkeyIntent::Begin};
        } else if (event == Event::HotkeyUp) {
            m_swallowNextUp = false; // residual up from an already-classified press
        }
        break;

    case Phase::Pressed:
        switch (event) {
        case Event::HotkeyUp:
            if (m_swallowNextUp) { m_swallowNextUp = false; break; }
            if (now - m_downAt >= HotkeyTuning::holdThreshold) {
                m_phase = Phase::Idle;
                fx.intents = {HotkeyIntent::Finalize};
            } else if (doubleTapLockEnabled) {
                m_phase = Phase::PendingSecondTap;
                fx.armTimer = HotkeyTuning::doubleTapWindow;
            } else {
                m_phase = Phase::Idle;
                fx.intents = {HotkeyIntent::ShortTapHint};
            }
            break;
        case Event::EscDown:
            m_phase = Phase::Idle;
            m_swallowNextUp = true;
            fx.intents = {HotkeyIntent::Cancel};
            break;
        case Event::OtherKeyDown:
            if (now - m_sessionStartAt < HotkeyTuning::interruptionWindow) {
                m_phase = Phase::Idle;
                m_swallowNextUp = true;
                fx.intents = {HotkeyIntent::AbortAccidental};
            }
            break;
        case Event::SpaceLock:
            m_phase = Phase::Locked;
            m_swallowNextUp = true;
            fx.intents = {HotkeyIntent::LockIn};
            break;
        default:
            break;
        }
        break;

    case Phase::PendingSecondTap:
        switch (event) {
        case Event::HotkeyDown:
            m_phase = Phase::Locked;
            m_swallowNextUp = true;
            fx.intents = {HotkeyIntent::LockIn};
            fx.disarmTimer = true;
            break;
        case Event::DoubleTapTimeout:
            m_phase = Phase::Idle;
            fx.intents = {HotkeyIntent::ShortTapHint};
            break;
        case Event::EscDown:
            m_phase = Phase::Idle;
            fx.intents = {HotkeyIntent::Cancel};
            fx.disarmTimer = true;
            break;
        case Event::OtherKeyDown:
            m_phase = Phase::Idle;
            fx.disarmTimer = true;
            fx.intents = {now - m_sessionStartAt < HotkeyTuning::interruptionWindow ? HotkeyIntent::AbortAccidental
                                                                                     : HotkeyIntent::Cancel};
            break;
        case Event::HotkeyUp:
            m_swallowNextUp = false;
            break;
        default:
            break;
        }
        break;

    case Phase::Locked:
        switch (event) {
        case Event::HotkeyDown:
            m_phase = Phase::Idle;
            m_swallowNextUp = true;
            fx.intents = {HotkeyIntent::Finalize};
            break;
        case Event::EscDown:
            m_phase = Phase::Idle;
            fx.intents = {HotkeyIntent::Cancel};
            break;
        case Event::HotkeyUp:
            m_swallowNextUp = false;
            break;
        default:
            break;
        }
        break;
    }
    return fx;
}

} // namespace jot
