// The QML-facing bridge for the main window (History, Dictionary, Settings,
// About) and onboarding. One context property, "jot".
#pragma once

#include <QObject>
#include <QPointer>
#include <QVariant>

class QQmlApplicationEngine;
class QMediaPlayer;
class QAudioOutput;
class QWindow;

namespace jot {

class DictationController;
class AudioCapture;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(int rev READ rev NOTIFY revChanged)
    Q_PROPERTY(int historyRev READ historyRev NOTIFY historyRevChanged)
    Q_PROPERTY(int dictRev READ dictRev NOTIFY dictRevChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(bool darkMode READ darkMode NOTIFY darkModeChanged)
    Q_PROPERTY(float micTestLevel READ micTestLevel NOTIFY micTestLevelChanged)
    Q_PROPERTY(QString playingId READ playingId NOTIFY playingChanged)
    Q_PROPERTY(QString localeName READ localeName NOTIFY revChanged)
public:
    Backend(DictationController *controller, QObject *parent = nullptr);

    int rev() const { return m_rev; }
    int historyRev() const { return m_historyRev; }
    int dictRev() const { return m_dictRev; }
    QString version() const;
    bool darkMode() const;
    float micTestLevel() const { return m_micLevel; }
    QString playingId() const { return m_playingId; }
    QString localeName() const;

    // windows
    Q_INVOKABLE void openMain(const QString &section = QString());
    Q_INVOKABLE void presentOnboarding(int screen = -1);
    Q_INVOKABLE void closeOnboarding();
    Q_INVOKABLE void finishOnboarding(bool launchAtLogin);
    Q_INVOKABLE void openUrl(const QString &url);
    Q_INVOKABLE void snapshot(const QString &name);

    // settings
    Q_INVOKABLE QVariant setting(const QString &key) const;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE bool usableEndpoint(const QString &raw) const;
    Q_INVOKABLE QString liveSummary() const;
    Q_INVOKABLE bool launchAtLogin() const;
    Q_INVOKABLE bool setLaunchAtLogin(bool enabled);
    Q_INVOKABLE void purgeRetention();

    // keyboard / insertion capabilities
    Q_INVOKABLE QString hotkeyName() const;
    Q_INVOKABLE QString keyName(int code) const;
    Q_INVOKABLE void captureHotkey();
    Q_INVOKABLE void cancelHotkeyCapture();
    Q_INVOKABLE QString hotkeyBackend() const;
    Q_INVOKABLE bool evdevAvailable() const;
    Q_INVOKABLE bool pasteAvailable() const;
    Q_INVOKABLE bool clipboardControlAvailable() const;
    Q_INVOKABLE QString permissionCommand() const;
    Q_INVOKABLE void recheckKeyboardAccess();
    Q_INVOKABLE void configurePortalShortcuts();

    // microphones
    Q_INVOKABLE QVariantList microphones() const;
    Q_INVOKABLE void selectMicrophone(const QString &id);
    Q_INVOKABLE void selectSystemMicrophone();
    Q_INVOKABLE QString selectedMicrophoneName() const;
    Q_INVOKABLE bool microphoneAvailable() const;
    Q_INVOKABLE int micSystemVolume() const;
    Q_INVOKABLE void setMicSystemVolume(int percent);
    Q_INVOKABLE bool micTestRunning() const { return !m_micTest.isNull(); }
    Q_INVOKABLE void startMicTest();
    Q_INVOKABLE void stopMicTest();

    // keys
    Q_INVOKABLE bool geminiKeyStored() const;
    Q_INVOKABLE bool openRouterKeyStored() const;
    Q_INVOKABLE void validateGeminiKey(const QString &key, bool checkModel);
    Q_INVOKABLE bool removeGeminiKey();
    Q_INVOKABLE void validateOpenRouterKey(const QString &key);
    Q_INVOKABLE bool removeOpenRouterKey();

    // history
    Q_INVOKABLE QVariantMap historyStats() const;
    Q_INVOKABLE QVariantList historyRecords(const QString &query) const;
    Q_INVOKABLE QVariantList attentionRecords(const QString &query) const;
    Q_INVOKABLE QVariantMap latestRecord() const;
    Q_INVOKABLE bool historyAvailable() const;
    Q_INVOKABLE void deleteRecord(const QString &id);
    Q_INVOKABLE void deleteAllHistory();
    Q_INVOKABLE void retryRecord(const QString &id);
    Q_INVOKABLE void copyText(const QString &text);
    Q_INVOKABLE void playAudio(const QString &id);
    Q_INVOKABLE void stopAudio();

    // dictionary
    Q_INVOKABLE QVariantList dictionaryEntries(const QString &query) const;
    Q_INVOKABLE QString addTerm(const QString &term, const QString &forms);
    Q_INVOKABLE void toggleStar(const QString &id);
    Q_INVOKABLE void removeTerm(const QString &id);
    Q_INVOKABLE void setForms(const QString &id, const QString &lines);
    Q_INVOKABLE int importCSV(const QUrl &url);
    Q_INVOKABLE int exportCSV(const QUrl &url);
    Q_INVOKABLE int dictionaryCount() const;

signals:
    void revChanged();
    void historyRevChanged();
    void dictRevChanged();
    void darkModeChanged();
    void micTestLevelChanged();
    void micTestStatus(const QString &status);
    void playingChanged();
    void showSection(const QString &section);
    void onboardingJump(int screen);
    void hotkeyCaptured(int code, const QString &name);
    void geminiKeyResult(const QString &status, const QString &message);
    void openRouterResult(bool ok, const QString &message);

private:
    QVariantMap recordToMap(const struct DictationRecord &r) const;
    DictationController *m_controller;
    QQmlApplicationEngine *m_mainEngine = nullptr;
    QQmlApplicationEngine *m_onboardingEngine = nullptr;
    QPointer<QWindow> m_mainWindow;
    QPointer<QWindow> m_onboardingWindow;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOut = nullptr;
    QPointer<AudioCapture> m_micTest;
    QString m_micTestFile;
    float m_micLevel = 0;
    QString m_playingId;
    int m_rev = 0;
    int m_historyRev = 0;
    int m_dictRev = 0;
};

} // namespace jot
