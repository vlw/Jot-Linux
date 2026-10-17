// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) — DictationController.
#include "DictationController.h"
#include "DictationCoordinator.h"
#include "Earcons.h"
#include "Language.h"
#include "../audio/AudioCapture.h"
#include "../core/DictionaryStore.h"
#include "../core/FileLayout.h"
#include "../core/SecretStore.h"
#include "../core/Settings.h"
#include "../platform/ActiveWindow.h"
#include "../platform/HotkeyEngine.h"
#include "../platform/Inserter.h"
#include "../platform/MediaPause.h"
#include "../ui/PillHUD.h"
#include "../ui/TrayController.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcController, "jot.controller")

namespace jot {

using SK = DictationState::Kind;
using Sound = Earcons::Sound;
using TV = TrayController::Visual;

static void playSound(int s)
{
    if (s >= 0) Earcons::instance().play(Sound(s));
}

DictationController::DictationController(QObject *parent)
    : QObject(parent)
    , m_coordinator(new DictationCoordinator(this))
    , m_history(HistoryStore::standard())
    , m_queue(new RetryQueue(m_history, this))
    , m_scanner(new RecoveryScanner(m_history, this))
    , m_pill(new PillModel(this))
    , m_hud(new PillHUD(m_pill, this))
    , m_tray(new TrayController(this))
{
    m_coordinator->contextProvider = [] {
        auto &active = ActiveWindow::instance();
        DictationContext c;
        c.options = Settings::instance().transcriptionOptions();
        c.targetAppClass = active.resourceClass();
        c.targetAppName = active.appName();
        c.targetPID = active.pid();
        c.targetWindowId = active.windowId();
        return c;
    };
    m_elapsedTimer.setInterval(500);
    connect(&m_elapsedTimer, &QTimer::timeout, this, [this] { m_pill->setElapsed(int(m_sessionClock.elapsed() / 1000)); });
    m_slowTimer.setSingleShot(true);
    connect(&m_slowTimer, &QTimer::timeout, this, [this] {
        if (m_coordinator->state().isInFlight()) m_pill->setSlow(true);
    });
    m_dismissTimer.setSingleShot(true);
    connect(&m_dismissTimer, &QTimer::timeout, this, &DictationController::setRestingPill);

    connect(m_tray, &TrayController::startHandsFree, this, &DictationController::startHandsFree);
    connect(m_tray, &TrayController::pasteLastTranscript, this, &DictationController::pasteLastTranscript);
    connect(m_tray, &TrayController::openHistory, this, [this] { emit openMainWindow("history"); });
    connect(m_tray, &TrayController::openSettings, this, [this] { emit openMainWindow("general"); });
    connect(m_tray, &TrayController::openAbout, this, [this] { emit openMainWindow("about"); });
    connect(m_tray, &TrayController::quit, qApp, &QCoreApplication::quit);
}

QString DictationController::providerName(std::optional<TranscriptionProvider> provider) const
{
    const auto p = provider.value_or(Settings::instance().transcriptionProvider());
    return p == TranscriptionProvider::MAI ? "MAI / OpenRouter" : "Gemini";
}

QString DictationController::readyLine() const
{
    auto &engine = HotkeyEngine::instance();
    if (engine.backend() == HotkeyEngine::Backend::Portal) return QCoreApplication::translate("Jot", "Ready — hold your Jot shortcut to dictate");
    return QCoreApplication::translate("Jot", "Ready — hold %1 to dictate").arg(HotkeyEngine::keyDisplayName(Settings::instance().hotkeyCode()));
}

bool DictationController::needsOnboarding() const
{
    // A deliberate "I'll add it later" is remembered — the wizard must not re-trap that user.
    // A missing microphone only matters before setup: at login PipeWire publishes devices a
    // moment after autostart, and an unplugged mic is reported in the tray, not by the wizard.
    auto &s = Settings::instance();
    if (s.hasCompletedOnboarding()) return false;
    return !s.hasSelectedAPIKey()
        || (!HotkeyEngine::instance().evdevAvailable() && s.hotkeyBackend() != "portal")
        || !MicrophoneManager::instance().recordingDevice();
}

void DictationController::start()
{
    auto &settings = Settings::instance();
    HotkeyEngine::instance().applySettings();
    connect(&HotkeyEngine::instance(), &HotkeyEngine::intent, this, &DictationController::onIntent);
    connect(&Language::instance(), &Language::changed, this, [this] {
        m_tray->retranslate();
        m_hud->retranslate();
        if (m_engineActive) activateEngine(); else reportSetupIncomplete();
    });
    connect(&HotkeyEngine::instance(), &HotkeyEngine::backendChanged, this, [this] {
        if (!m_engineActive) {
            if (!needsOnboarding()) activateEngine();
        } else if (Settings::instance().hasSelectedAPIKey()) {
            m_tray->setStatusLine(readyLine());
        }
    });
    connect(m_pill, &PillModel::stopRequested, this, [this] { m_coordinator->handle(HotkeyIntent::Finalize); });
    connect(m_pill, &PillModel::dotRequested, this, &DictationController::startHandsFree);
    connect(m_pill, &PillModel::cancelRequested, this, [this] { m_coordinator->handle(HotkeyIntent::Cancel); });
    connect(&settings, &Settings::changed, this, &DictationController::applySettingChange);
    connect(&SecretStore::instance(), &SecretStore::keyChanged, this, [this](const QString &account) {
        applySettingChange(account == "gemini" ? "apiKey" : "openRouterKey");
    });
    connect(&settings, &Settings::smartFormattingAutoDegraded, this, [this] {
        auto &s = Settings::instance();
        const QString tail = s.smartTranscriptionEnabled() && !s.usesLegacyTranscribeEndpoint()
            ? QCoreApplication::translate("Jot", "Smart transcription is still on.") : QCoreApplication::translate("Jot", "Re-enable it in Settings → Dictation.");
        showBackgroundNotice(QCoreApplication::translate("Jot", "Turned off text cleanup — the edits changed too much. %1").arg(tail), 5.0);
    });

    connect(m_coordinator, &DictationCoordinator::stateChanged, this, &DictationController::transition);
    connect(m_coordinator, &DictationCoordinator::micLevelChanged, m_pill, &PillModel::setLevel);
    connect(m_coordinator, &DictationCoordinator::coachingHintChanged, this, [this](const QString &hint) {
        if (hint.isEmpty()) return;
        const auto st = m_coordinator->state();
        if (st.kind == SK::Warming || st.kind == SK::Recording) showNotice(hint, 3.0);
        else showBackgroundNotice(hint, 3.0);
    });

    startHistoryServices();

    // System sleep while recording → finalize; media abandons automatic resume.
    QDBusConnection::systemBus().connect("org.freedesktop.login1", "/org/freedesktop/login1",
                                         "org.freedesktop.login1.Manager", QString("PrepareForSleep"), this,
                                         SLOT(onPrepareForSleep(bool)));

    // Every 10 minutes: transcript retention can be as short as one hour.
    m_retentionTimer.setInterval(10 * 60 * 1000);
    connect(&m_retentionTimer, &QTimer::timeout, this, &DictationController::purgeRetention);
    m_retentionTimer.start();

    m_setupPoll.setInterval(2000);
    connect(&m_setupPoll, &QTimer::timeout, this, [this] {
        if (!m_engineActive) activateEngine();
        if (m_engineActive) m_setupPoll.stop();
    });

    if (needsOnboarding()) {
        emit presentOnboarding();
        reportSetupIncomplete();
    } else {
        activateEngine();
    }
}

void DictationController::activateEngine()
{
    auto &engine = HotkeyEngine::instance();
    const bool ok = engine.backend() != HotkeyEngine::Backend::None ? true : engine.start();
    if (ok && engine.backend() != HotkeyEngine::Backend::None) {
        m_engineActive = true;
        m_setupPoll.stop();
        if (!Settings::instance().hasSelectedAPIKey()) {
            m_tray->setStatusLine(QCoreApplication::translate("Jot", "Add your %1 API key in Settings → Advanced")
                                      .arg(Settings::instance().transcriptionProvider() == TranscriptionProvider::MAI ? "OpenRouter" : "Gemini"));
            m_tray->setVisual(TV::Attention);
        } else {
            m_tray->setStatusLine(readyLine());
            m_tray->setVisual(TV::Idle);
        }
        m_hud->show();
        setRestingPill();
        if (Settings::instance().value("shouldAnnounceSmartRestored", false).toBool()) {
            Settings::instance().remove("shouldAnnounceSmartRestored");
            showBackgroundNotice(QCoreApplication::translate("Jot", "Smart transcription is back on — the model does the formatting itself now."), 5.0);
        }
    } else {
        m_tray->setStatusLine(QCoreApplication::translate("Jot", "Allow keyboard access to enable the dictation key"));
        m_tray->setVisual(TV::Attention);
        m_setupPoll.start();
    }
}

void DictationController::reportSetupIncomplete()
{
    auto &s = Settings::instance();
    if (HotkeyEngine::instance().backend() == HotkeyEngine::Backend::None && !HotkeyEngine::instance().evdevAvailable()
        && s.hotkeyBackend() == "evdev")
        m_tray->setStatusLine(QCoreApplication::translate("Jot", "Allow keyboard access to enable the dictation key"));
    else if (!MicrophoneManager::instance().recordingDevice())
        m_tray->setStatusLine(QCoreApplication::translate("Jot", "Connect your microphone to dictate"));
    else if (!s.hasSelectedAPIKey())
        m_tray->setStatusLine(QCoreApplication::translate("Jot", "Add your %1 API key in Settings → Advanced")
                                  .arg(s.transcriptionProvider() == TranscriptionProvider::MAI ? "OpenRouter" : "Gemini"));
    else
        m_tray->setStatusLine(QCoreApplication::translate("Jot", "Finish setup to start dictating"));
    m_tray->setVisual(TV::Attention);
}

void DictationController::onboardingClosed()
{
    activateEngine();
    if (!Settings::instance().hasSelectedAPIKey()) reportSetupIncomplete();
}

void DictationController::applySettingChange(const QString &key)
{
    if (key == "showIdleIndicator") {
        const auto st = m_pill->stateEnum();
        if (st == PillModel::State::IdleDot || st == PillModel::State::Hidden)
            m_pill->setIdle(Settings::instance().showIdleIndicator());
    } else if (key == "hotkeyCode" || key == "doubleTapLock" || key == "hotkeyBackend") {
        if (m_engineActive && Settings::instance().hasSelectedAPIKey()) m_tray->setStatusLine(readyLine());
    } else if (key == "apiKey" || key == "openRouterKey" || key == "transcriptionProvider") {
        if (Settings::instance().hasSelectedAPIKey()) {
            activateEngine();
        } else {
            m_tray->setStatusLine(QCoreApplication::translate("Jot", "Add your %1 API key in Settings → Advanced")
                                      .arg(Settings::instance().transcriptionProvider() == TranscriptionProvider::MAI ? "OpenRouter" : "Gemini"));
            m_tray->setVisual(TV::Attention);
        }
    }
}

void DictationController::onIntent(HotkeyIntent intent)
{
    if (intent == HotkeyIntent::Begin) {
        if (!m_coordinator->handle(intent)) {
            const auto st = m_coordinator->state();
            if (st.kind == SK::Recording && st.locked) m_coordinator->handle(HotkeyIntent::Finalize);
            else announceBusyIfTranscribing();
            HotkeyEngine::instance().resetGrammar();
        }
        return;
    }
    m_coordinator->handle(intent);
}

void DictationController::announceBusyIfTranscribing()
{
    if (m_coordinator->state().isInFlight())
        showNotice(QCoreApplication::translate("Jot", "Still transcribing the previous dictation — try again in a moment"), 2.5, int(Sound::Error));
}

void DictationController::startHandsFree()
{
    if (!m_engineActive) activateEngine();
    if (!m_engineActive) {
        if (needsOnboarding()) emit presentOnboarding();
        return;
    }
    if (!m_coordinator->handle(HotkeyIntent::Begin)) {
        announceBusyIfTranscribing();
        return;
    }
    m_coordinator->handle(HotkeyIntent::LockIn);
}

void DictationController::stopDictation() { m_coordinator->handle(HotkeyIntent::Finalize); }

void DictationController::setRestingPill()
{
    const auto st = m_coordinator->state();
    switch (st.kind) {
    case SK::Warming: m_pill->setListening(false); break;
    case SK::Recording: m_pill->setListening(st.locked); break;
    case SK::Finalizing:
    case SK::Transcribing:
    case SK::Inserting: m_pill->setProcessing(); break;
    default: m_pill->setIdle(Settings::instance().showIdleIndicator()); break;
    }
}

void DictationController::dismissAfter(double seconds)
{
    m_dismissTimer.start(int(seconds * 1000));
}

void DictationController::transition(const DictationState &state, const DictationState &previous)
{
    m_dismissTimer.stop();
    HotkeyEngine::instance().setExternalSessionActive(state.kind == SK::Recording || state.kind == SK::Finalizing
                                                      || state.kind == SK::Transcribing);
    switch (state.kind) {
    case SK::Idle:
        m_pill->setIdle(Settings::instance().showIdleIndicator());
        m_tray->setVisual(TV::Idle);
        break;
    case SK::Warming:
        m_sessionClock.start();
        Earcons::instance().play(Sound::Start);
        m_hud->repositionToActiveScreen();
        m_pill->setElapsed(0);
        m_pill->setListening(false);
        m_elapsedTimer.start();
        m_tray->setVisual(TV::Listening);
        break;
    case SK::Recording:
        if (previous.kind == SK::Recording && !previous.locked && state.locked) Earcons::instance().play(Sound::Lock);
        m_pill->setListening(state.locked);
        m_tray->setVisual(TV::Listening);
        break;
    case SK::Finalizing:
        Earcons::instance().play(Sound::Stop);
        m_elapsedTimer.stop();
        m_pill->setProcessing();
        m_slowTimer.start(3000);
        m_tray->setVisual(TV::Processing);
        break;
    case SK::Transcribing:
    case SK::Inserting:
        m_pill->setProcessing();
        m_tray->setVisual(TV::Processing);
        break;
    case SK::Done:
        m_slowTimer.stop();
        m_pill->setSlow(false);
        m_elapsedTimer.stop();
        m_tray->setVisual(TV::Idle);
        handleOutcome(state.outcome);
        break;
    case SK::Cancelled:
        m_slowTimer.stop();
        m_pill->setSlow(false);
        m_elapsedTimer.stop();
        m_tray->setVisual(TV::Idle);
        if (m_sessionClock.isValid() && m_sessionClock.elapsed() > 500) Earcons::instance().play(Sound::Cancel);
        m_pill->setIdle(Settings::instance().showIdleIndicator());
        break;
    case SK::Failed:
        m_slowTimer.stop();
        m_pill->setSlow(false);
        m_elapsedTimer.stop();
        Earcons::instance().play(Sound::Error);
        m_tray->setVisual(state.failure == DictationFailure::Auth || state.failure == DictationFailure::ModelAccess
                              ? TV::Attention : TV::Idle);
        m_pill->setError(copyFor(state.failure));
        dismissAfter(6.0);
        break;
    }
    if (state.kind == SK::Idle || state.isTerminal()) flushPendingNotice();
}

void DictationController::handleOutcome(DictationOutcome outcome)
{
    if (outcome != DictationOutcome::Silent) m_consecutiveSilent = 0;
    switch (outcome) {
    case DictationOutcome::Inserted: {
        Earcons::instance().play(Sound::Success);
        const int words = int(m_coordinator->lastResult().split(' ', Qt::SkipEmptyParts).size());
        m_pill->setSuccess(words);
        dismissAfter(0.7);
        break;
    }
    case DictationOutcome::CopiedToClipboard:
    case DictationOutcome::AwaitingChip:
    case DictationOutcome::HeldForSecureField:
        // The text is already on the clipboard — no notice needed.
        m_pill->setIdle(Settings::instance().showIdleIndicator());
        break;
    case DictationOutcome::QueuedForRetry:
        showNotice(QCoreApplication::translate("Jot", "You're offline — saved to History"), 4.0);
        break;
    case DictationOutcome::Silent:
        if (m_coordinator->lastSilenceReason() == SilenceReason::TooShort) {
            showNotice(QCoreApplication::translate("Jot", "Too short — not sent"), 1.2);
            break;
        }
        ++m_consecutiveSilent;
        if (!MicrophoneManager::instance().recordingDevice()) {
            showNotice(QCoreApplication::translate("Jot", "Microphone unavailable — check it in the Jot menu → Microphone"), 5.0);
            m_tray->setVisual(TV::Attention);
        } else if (m_coordinator->lastSilenceReason() == SilenceReason::TooNoisy) {
            showNotice(QCoreApplication::translate("Jot", "Too noisy to make out speech — saved to History"), 4.0);
        } else if (m_consecutiveSilent >= 2) {
            showNotice(QCoreApplication::translate("Jot", "Didn't catch any speech — check your mic's input volume in System Settings"), 5.0);
        } else {
            showNotice(QCoreApplication::translate("Jot", "Didn't catch any speech"), 2.0);
        }
        break;
    }
}

QString DictationController::copyFor(DictationFailure failure) const
{
    const auto provider = m_coordinator->lastTranscriptionProvider();
    const QString name = providerName(provider);
    switch (failure) {
    case DictationFailure::Network: return QCoreApplication::translate("Jot", "Couldn't reach %1 — saved to History").arg(name);
    case DictationFailure::Auth: return QCoreApplication::translate("Jot", "Check your %1 API key in Settings → Advanced — saved to History").arg(name);
    case DictationFailure::ModelAccess:
        if (!Settings::instance().transcribeModelOverride().trimmed().isEmpty())
            return QCoreApplication::translate("Jot", "%1: that model isn't available to your key — check Settings → Advanced. Saved to History").arg(name);
        return QCoreApplication::translate("Jot", "%1: your key can't use the transcription model yet — recording saved to History").arg(name);
    case DictationFailure::BadRequest: return QCoreApplication::translate("Jot", "%1 rejected the request — saved to History").arg(name);
    case DictationFailure::RateLimited: return QCoreApplication::translate("Jot", "%1 rate limited — saved to History").arg(name);
    case DictationFailure::NoMicrophone:
        return QCoreApplication::translate("Jot", "Microphone unavailable — connect %1").arg(MicrophoneManager::instance().selectedDisplayName());
    case DictationFailure::QuotaExhausted:
        return provider == TranscriptionProvider::MAI ? QCoreApplication::translate("Jot", "OpenRouter balance exhausted — add credits. Saved to History")
                                                      : QCoreApplication::translate("Jot", "Gemini quota or credits exhausted — check Google AI Studio. Saved to History");
    case DictationFailure::Timeout:
        return QCoreApplication::translate("Jot", "%1 didn't answer in %2 s — saved to History, use Retry there")
            .arg(name).arg(int(Settings::instance().transcriptionTimeoutSeconds()));
    case DictationFailure::Validation: return QCoreApplication::translate("Jot", "%1 couldn't transcribe — saved to History").arg(name);
    case DictationFailure::SafetyBlocked: return QCoreApplication::translate("Jot", "%1 declined this one — saved to History").arg(name);
    case DictationFailure::NoAudio: return QCoreApplication::translate("Jot", "Mic didn't start in time — try again");
    case DictationFailure::Audio: return QCoreApplication::translate("Jot", "Mic didn't start — try again");
    case DictationFailure::Storage: return QCoreApplication::translate("Jot", "Disk problem — couldn't save the audio");
    }
    return {};
}

void DictationController::showNotice(const QString &message, double seconds, int sound)
{
    playSound(sound);
    m_pill->setNotice(message);
    dismissAfter(seconds);
}

void DictationController::showBackgroundNotice(const QString &message, double seconds, int sound)
{
    const auto st = m_coordinator->state();
    if (st.kind == SK::Idle || st.isTerminal()) {
        // Do not trample a success/error chip that is still on screen.
        if (m_dismissTimer.isActive()) {
            m_pending = PendingNotice{message, seconds, sound};
            flushPendingNotice();
            return;
        }
        showNotice(message, seconds, sound);
        return;
    }
    m_pending = PendingNotice{message, seconds, sound};
}

void DictationController::flushPendingNotice()
{
    if (!m_pending || m_flushScheduled) return;
    m_flushScheduled = true;
    QTimer::singleShot(1800, this, [this] {
        m_flushScheduled = false;
        if (!m_pending) return;
        const auto st = m_coordinator->state();
        if (st.kind == SK::Idle || st.isTerminal()) {
            const auto n = *m_pending;
            m_pending.reset();
            showNotice(n.message, n.seconds, n.sound);
        }
    });
}

void DictationController::pasteLastTranscript()
{
    const QString last = m_coordinator->lastResult();
    if (last.isEmpty()) {
        showNotice(QCoreApplication::translate("Jot", "Nothing to paste yet — dictate something first"), 2.5);
        return;
    }
    DictationContext ctx;
    ctx.targetAppClass = ActiveWindow::instance().resourceClass();
    ctx.targetWindowId = ActiveWindow::instance().windowId();
    Inserter::instance().insert(last, ctx, [](InsertionOutcome) {});
}

void DictationController::startHistoryServices()
{
    m_coordinator->onSessionDiscard = [this](const QUuid &id) {
        m_history->remove(id.toString(QUuid::WithoutBraces).toUpper(), false);
    };
    m_coordinator->onSessionUpdate = [this](const SessionMeta &meta, const QString &folder) {
        m_history->upsert(meta, folder);
        if (Settings::instance().audioRetentionDays() == -1
            && (meta.rawTranscript || meta.status == SessionMeta::Status::Silent)
            && meta.status != SessionMeta::Status::Recording && meta.status != SessionMeta::Status::Recorded) {
            QFile::remove(FileLayout::audioFile(folder));
            QFile::remove(FileLayout::audioFLAC(folder));
        }
    };
    m_history->onUserDelete = [this](const DictationRecord &record) {
        m_coordinator->forgetLastResult(record.cleanedTranscript.value_or(QString()));
    };
    m_queue->onTranscriptCopied = [this](const QString &text) { m_coordinator->noteLastResult(text); };
    m_scanner->onRecovered = [this](const QString &message) { showBackgroundNotice(message, 4.0, int(Sound::Success)); };

    const QDateTime cutoff = QDateTime::currentDateTime();
    QTimer::singleShot(0, this, [this, cutoff] {
        // Expire old entries first. Nothing is ever re-sent automatically.
        m_history->reindex();
        purgeRetention();
        m_scanner->scanAndRecover(cutoff, [this] {
            // Seed paste-last (memory only — never the clipboard) from History.
            for (const auto &r : m_history->records({}, 20))
                if (r.cleanedTranscript && !r.cleanedTranscript->isEmpty()) {
                    m_coordinator->seedLastResult(*r.cleanedTranscript);
                    break;
                }
        });
    });
}

void DictationController::retryFromHistory(const DictationRecord &record)
{
    if (m_queue->isBusy()) showNotice(QCoreApplication::translate("Jot", "Retry queued — waiting for the current transcription"), 2.5);
    m_queue->retrySingle(record, [this](RetryQueue::Outcome outcome) {
        const QString name = providerName(m_queue->lastTranscriptionProvider());
        switch (outcome) {
        case RetryQueue::Outcome::StillOffline:
            showNotice(QCoreApplication::translate("Jot", "%1 unavailable — try again later").arg(name), 4.0);
            break;
        case RetryQueue::Outcome::Failed:
            showNotice(QCoreApplication::translate("Jot", "%1 retry didn't work — the row has the details").arg(name), 3.0);
            break;
        case RetryQueue::Outcome::AudioUnavailable:
            showNotice(QCoreApplication::translate("Jot", "Audio is no longer available — previous transcript kept"), 4.0);
            break;
        case RetryQueue::Outcome::KeptPreviousTranscript: {
            QString reason = QCoreApplication::translate("Jot", "%1 retry failed").arg(name);
            if (const auto f = m_queue->lastFailure()) {
                if (f->kind == TranscriptionErrorKind::Auth) reason = QCoreApplication::translate("Jot", "Check your %1 key in Settings → Advanced").arg(name);
                else if (f->kind == TranscriptionErrorKind::RateLimitedDaily)
                    reason = m_queue->lastTranscriptionProvider() == TranscriptionProvider::MAI ? QCoreApplication::translate("Jot", "OpenRouter balance exhausted")
                                                                                                 : QCoreApplication::translate("Jot", "Gemini quota or credits exhausted");
            }
            showNotice(QCoreApplication::translate("Jot", "%1 — previous transcript kept").arg(reason), 4.0);
            break;
        }
        case RetryQueue::Outcome::Recovered:
            showBackgroundNotice(QCoreApplication::translate("Jot", "Transcription is ready — copied to clipboard"), 4.0, int(Sound::Success));
            break;
        case RetryQueue::Outcome::Blocked: {
            const auto f = m_queue->lastFailure();
            if (f && f->kind == TranscriptionErrorKind::Auth)
                showNotice(QCoreApplication::translate("Jot", "Check your %1 key in Settings → Advanced").arg(name), 4.0);
            else
                showNotice(m_queue->lastTranscriptionProvider() == TranscriptionProvider::MAI
                               ? QCoreApplication::translate("Jot", "OpenRouter balance exhausted")
                               : QCoreApplication::translate("Jot", "Gemini quota or credits exhausted"), 4.0);
            break;
        }
        case RetryQueue::Outcome::AlreadyRetrying:
            showNotice(QCoreApplication::translate("Jot", "This dictation is already being retried"), 2.5);
            break;
        default:
            break;
        }
    });
}

void DictationController::purgeRetention()
{
    RetentionPolicy::purgeExpiredAudio(Settings::instance().audioRetentionDays());
    RetentionPolicy::purgeExpiredTranscripts(m_history, Settings::instance().transcriptRetentionHours(),
                                             m_coordinator->activeSessionFolder());
}

void DictationController::onPrepareForSleep(bool sleeping)
{
    if (!sleeping) return;
    if (m_coordinator->state().kind == SK::Recording) m_coordinator->handle(HotkeyIntent::Finalize);
    MediaPause::instance().systemSleeping();
}

void DictationController::prepareForTermination()
{
    if (m_terminating) return;
    m_terminating = true;
    if (m_coordinator->state().kind == SK::Recording) m_coordinator->handle(HotkeyIntent::Finalize);
    MediaPause::instance().finish();
    ActiveWindow::instance().stop();
}

} // namespace jot

namespace jot {
void DictationController::debugPill(const QString &state, const QString &path)
{
    m_dismissTimer.stop();
    if (state == "idle") m_pill->setIdle(true);
    else if (state == "listening") { m_pill->setElapsed(14); m_pill->setLevel(0.6f); m_pill->setListening(false); }
    else if (state == "locked") { m_pill->setElapsed(83); m_pill->setLevel(0.4f); m_pill->setListening(true); }
    else if (state == "processing") m_pill->setProcessing();
    else if (state == "slow") { m_pill->setProcessing(); m_pill->setSlow(true); }
    else if (state == "success") m_pill->setSuccess(42);
    else if (state == "notice") m_pill->setNotice(QCoreApplication::translate("Jot", "Didn't catch any speech"));
    else if (state == "error") m_pill->setError(copyFor(DictationFailure::Network));
    m_hud->grab(path);
}
} // namespace jot
