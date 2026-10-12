// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) — DictationController.
#pragma once

#include "../core/DictationState.h"
#include "../core/HistoryStore.h"
#include "../core/HotkeyProcessor.h"
#include "RetryQueue.h"
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <optional>

namespace jot {

class DictationCoordinator;
class PillModel;
class PillHUD;
class TrayController;

class DictationController : public QObject {
    Q_OBJECT
public:
    explicit DictationController(QObject *parent = nullptr);
    void start();
    void prepareForTermination();

    DictationCoordinator *coordinator() const { return m_coordinator; }
    HistoryStore *history() const { return m_history; }
    RetryQueue *retryQueue() const { return m_queue; }
    PillModel *pillModel() const { return m_pill; }
    bool engineActive() const { return m_engineActive; }
    bool needsOnboarding() const;

    void startHandsFree();
    void stopDictation();
    void pasteLastTranscript();
    void retryFromHistory(const DictationRecord &record);
    void onboardingClosed();
    void clearPasteLast() { m_clearPasteLast = true; }
    QString readyLine() const;
    void debugPill(const QString &state, const QString &path);

signals:
    void openMainWindow(const QString &section);
    void presentOnboarding();

private slots:
    void onPrepareForSleep(bool sleeping);

private:
    void activateEngine();
    void reportSetupIncomplete();
    void applySettingChange(const QString &key);
    void onIntent(HotkeyIntent intent);
    void transition(const DictationState &state, const DictationState &previous);
    void handleOutcome(DictationOutcome outcome);
    QString copyFor(DictationFailure failure) const;
    QString providerName(std::optional<TranscriptionProvider> provider = std::nullopt) const;
    void showNotice(const QString &message, double seconds, int sound = -1);
    void showBackgroundNotice(const QString &message, double seconds, int sound = -1);
    void flushPendingNotice();
    void dismissAfter(double seconds);
    void setRestingPill();
    void announceBusyIfTranscribing();
    void startHistoryServices();
    void setTray(int visual);

    DictationCoordinator *m_coordinator;
    HistoryStore *m_history;
    RetryQueue *m_queue;
    RecoveryScanner *m_scanner;
    PillModel *m_pill;
    PillHUD *m_hud;
    TrayController *m_tray;

    bool m_engineActive = false;
    bool m_clearPasteLast = false;
    int m_consecutiveSilent = 0;
    QElapsedTimer m_sessionClock;
    QTimer m_elapsedTimer;
    QTimer m_slowTimer;
    QTimer m_dismissTimer;
    QTimer m_retentionTimer;
    QTimer m_setupPoll;
    struct PendingNotice { QString message; double seconds; int sound; };
    std::optional<PendingNotice> m_pending;
    bool m_flushScheduled = false;
    bool m_terminating = false;
};

} // namespace jot
