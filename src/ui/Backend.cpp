// The QML-facing bridge for the main window and onboarding.
#include "Backend.h"
#include "../app/DictationController.h"
#include "../app/DictationCoordinator.h"
#include "../app/RetryQueue.h"
#include "../app/Language.h"
#include "../audio/AudioCapture.h"
#include "../core/DictionaryStore.h"
#include "../core/FileLayout.h"
#include "../core/HistoryStore.h"
#include "../core/SecretStore.h"
#include "../core/Settings.h"
#include "../net/GeminiClient.h"
#include "../net/LiveSession.h"
#include "../net/TranscriptionService.h"
#include "../platform/HotkeyEngine.h"
#include "../platform/Inserter.h"
#include "../platform/WaylandIntegration.h"
#include "IconProvider.h"

#include <QAudioOutput>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMediaPlayer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTimer>
#include <QWindow>

namespace jot {

Backend::Backend(DictationController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
{
    connect(&Settings::instance(), &Settings::changed, this, [this] { ++m_rev; emit revChanged(); });
    connect(&SecretStore::instance(), &SecretStore::keyChanged, this, [this] { ++m_rev; emit revChanged(); });
    connect(&MicrophoneManager::instance(), &MicrophoneManager::availabilityChanged, this, [this] { ++m_rev; emit revChanged(); });
    connect(&HotkeyEngine::instance(), &HotkeyEngine::backendChanged, this, [this] { ++m_rev; emit revChanged(); });
    auto *debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(250);
    connect(debounce, &QTimer::timeout, this, [this] { ++m_historyRev; emit historyRevChanged(); });
    connect(controller->history(), &HistoryStore::changed, debounce, qOverload<>(&QTimer::start));
    connect(&DictionaryStore::instance(), &DictionaryStore::changed, this, [this] { ++m_dictRev; emit dictRevChanged(); });
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &Backend::darkModeChanged);
    connect(&HotkeyEngine::instance(), &HotkeyEngine::keyCaptured, this, [this](int code) {
        emit hotkeyCaptured(code, code < 0 ? QString() : HotkeyEngine::keyDisplayName(code));
    });
    connect(&Language::instance(), &Language::changed, this, [this] {
        if (m_mainEngine) m_mainEngine->retranslate();
        if (m_onboardingEngine) m_onboardingEngine->retranslate();
        ++m_rev; ++m_historyRev; ++m_dictRev;
        emit revChanged(); emit historyRevChanged(); emit dictRevChanged();
    });
    connect(controller, &DictationController::openMainWindow, this, [this](const QString &s) { openMain(s); });
    connect(controller, &DictationController::presentOnboarding, this, [this] { presentOnboarding(); });
}

QString Backend::localeName() const { return Language::instance().code() == "ru" ? "ru_RU" : "en_US"; }

QString Backend::version() const { return QCoreApplication::applicationVersion(); }

bool Backend::darkMode() const { return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark; }

// ------------------------------------------------------------------ windows

void Backend::openMain(const QString &section)
{
    if (!m_mainEngine) {
        m_mainEngine = new QQmlApplicationEngine(this);
        m_mainEngine->rootContext()->setContextProperty("jot", this);
        m_mainEngine->rootContext()->setContextProperty("fonts", &Language::instance());
        m_mainEngine->addImageProvider("icon", new IconProvider);
        m_mainEngine->load(QUrl("qrc:/qml/MainWindow.qml"));
        if (!m_mainEngine->rootObjects().isEmpty())
            m_mainWindow = qobject_cast<QWindow *>(m_mainEngine->rootObjects().first());
    }
    if (!section.isEmpty()) emit showSection(section);
    if (m_mainWindow) {
        m_mainWindow->show();
        m_mainWindow->raise();
        m_mainWindow->requestActivate();
    }
}

static void snapshotIfRequested(QWindow *w, const QString &name)
{
    // Developer aid: JOT_SNAPSHOT_DIR=/path renders windows to PNG for review.
    const QString dir = qEnvironmentVariable("JOT_SNAPSHOT_DIR");
    if (dir.isEmpty() || !w) return;
    QTimer::singleShot(1500, w, [w, dir, name] {
        if (auto *qw = qobject_cast<QQuickWindow *>(w)) qw->grabWindow().save(dir + "/" + name + ".png");
    });
}

void Backend::snapshot(const QString &name)
{
    if (m_mainWindow && m_mainWindow->isVisible()) snapshotIfRequested(m_mainWindow, name);
    else if (m_onboardingWindow) snapshotIfRequested(m_onboardingWindow, name);
}

void Backend::presentOnboarding(int screen)
{
    if (!m_onboardingEngine) {
        m_onboardingEngine = new QQmlApplicationEngine(this);
        m_onboardingEngine->rootContext()->setContextProperty("jot", this);
        m_onboardingEngine->rootContext()->setContextProperty("fonts", &Language::instance());
        m_onboardingEngine->addImageProvider("icon", new IconProvider);
        m_onboardingEngine->load(QUrl("qrc:/qml/Onboarding.qml"));
        if (!m_onboardingEngine->rootObjects().isEmpty())
            m_onboardingWindow = qobject_cast<QWindow *>(m_onboardingEngine->rootObjects().first());
    }
    if (m_onboardingWindow) {
        m_onboardingWindow->show();
        m_onboardingWindow->raise();
        m_onboardingWindow->requestActivate();
    }
    if (screen >= 0) QTimer::singleShot(350, this, [this, screen] { emit onboardingJump(screen); });
}

void Backend::closeOnboarding()
{
    stopMicTest();
    if (m_onboardingEngine) {
        m_onboardingEngine->deleteLater();
        m_onboardingEngine = nullptr;
    }
    m_controller->onboardingClosed();
}

void Backend::finishOnboarding(bool launch)
{
    if (launch != launchAtLogin()) setLaunchAtLogin(launch);
    Settings::instance().setHasCompletedOnboarding(true);
    if (m_onboardingWindow) m_onboardingWindow->close(); // onClosing → closeOnboarding()
    else closeOnboarding();
}

void Backend::openUrl(const QString &url) { QDesktopServices::openUrl(QUrl(url)); }

// ------------------------------------------------------------------ settings

QVariant Backend::setting(const QString &key) const
{
    auto &s = Settings::instance();
    if (key == "transcriptionProvider") return providerKey(s.transcriptionProvider());
    if (key == "maiStyle") return maiStyleKey(s.maiStyle());
    if (key == "doubleTapLock") return s.doubleTapLockEnabled();
    if (key == "showIdleIndicator") return s.showIdleIndicator();
    if (key == "soundsEnabled") return s.soundsEnabled();
    if (key == "pauseMediaWhileRecording") return s.pauseMediaWhileRecording();
    if (key == "smartTranscription") return s.smartTranscriptionEnabled();
    if (key == "smartCleanupPass") return s.smartCleanupPassEnabled();
    if (key == "legacyTranscribeEndpoint") return s.usesLegacyTranscribeEndpoint();
    if (key == "experimentalNoiseHandling") return s.experimentalNoiseHandling();
    if (key == "liveTranscription") return s.liveTranscription();
    if (key == "audioRetentionDays") return s.audioRetentionDays();
    if (key == "transcriptRetentionHours") return s.transcriptRetentionHours();
    if (key == "hotkeyCode") return s.hotkeyCode();
    if (key == "hotkeyBackend") return s.hotkeyBackend();
    if (key == "pasteShortcut") return s.pasteShortcut();
    if (key == "hasCompletedOnboarding") return s.hasCompletedOnboarding();
    if (key == "micGainDb") return s.micGainDb();
    if (key == "transcriptionTimeout") return s.transcriptionTimeoutSeconds();
    if (key == "minimumRecording") return s.minimumRecordingSeconds();
    if (key == "language") return s.value("language", "system").toString();
    if (key == "microphoneFollowsSystem") return s.microphoneFollowsSystem();
    return s.value(key);
}

void Backend::setSetting(const QString &key, const QVariant &value)
{
    auto &s = Settings::instance();
    if (key == "smartCleanupPass") { if (value.toBool() != s.smartCleanupPassEnabled()) s.setSmartCleanupPass(value.toBool()); return; }
    if (key == "liveTranscription" && value.toBool()) LiveStats().clearStreak();
    if (key == "endpointOverride" || key == "transcribeModelOverride" || key == "cleanupModelOverride") {
        const QString v = value.toString().trimmed();
        if (v.isEmpty()) s.remove(key); else s.set(key, v);
        return;
    }
    s.set(key, value);
}

bool Backend::usableEndpoint(const QString &raw) const { return !Settings::usableEndpointURL(raw).isEmpty(); }

QString Backend::liveSummary() const { return LiveStats().summary(); }

static QString autostartPath()
{
    return QDir::homePath() + "/.config/autostart/jot-linux.desktop";
}

bool Backend::launchAtLogin() const { return QFile::exists(autostartPath()); }

bool Backend::setLaunchAtLogin(bool enabled)
{
    if (!enabled) return QFile::remove(autostartPath()) || !QFile::exists(autostartPath());
    QDir().mkpath(QFileInfo(autostartPath()).absolutePath());
    QSaveFile f(autostartPath());
    if (!f.open(QIODevice::WriteOnly)) return false;
    const QString exe = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    f.write(QString("[Desktop Entry]\nType=Application\nName=Jot\nComment=Hold a key. Speak. It types.\n"
                    "Exec=%1\nIcon=jot-linux\nX-KDE-autostart-phase=2\nX-GNOME-Autostart-enabled=true\n"
                    "X-KDE-Wayland-Interfaces=org_kde_kwin_fake_input\n").arg(exe).toUtf8());
    const bool ok = f.commit();
    ++m_rev;
    emit revChanged();
    return ok;
}

void Backend::purgeRetention()
{
    QTimer::singleShot(10, m_controller, &DictationController::purgeRetention);
}

// ------------------------------------------------------------------ keyboard

QString Backend::hotkeyName() const { return HotkeyEngine::keyDisplayName(Settings::instance().hotkeyCode()); }
QString Backend::keyName(int code) const { return HotkeyEngine::keyDisplayName(code); }
void Backend::captureHotkey() { HotkeyEngine::instance().captureNextKey(); }
void Backend::cancelHotkeyCapture() { HotkeyEngine::instance().cancelCapture(); }

QString Backend::hotkeyBackend() const
{
    switch (HotkeyEngine::instance().backend()) {
    case HotkeyEngine::Backend::Evdev: return "evdev";
    case HotkeyEngine::Backend::Portal: return "portal";
    default: return "none";
    }
}

bool Backend::evdevAvailable() const { return HotkeyEngine::instance().evdevAvailable(); }
bool Backend::pasteAvailable() const { return Inserter::instance().canPaste(); }
bool Backend::clipboardControlAvailable() const { return WaylandIntegration::instance().hasClipboardControl(); }

QString Backend::permissionCommand() const
{
    const QDir exe(QCoreApplication::applicationDirPath());
    for (const QString &candidate : {exe.absoluteFilePath("scripts/setup-permissions.sh"),
                                     exe.absoluteFilePath("../scripts/setup-permissions.sh")})
        if (QFile::exists(candidate)) return QString("sudo ") + QDir::cleanPath(candidate);
    return QString("sudo ") + QDir::cleanPath(exe.absoluteFilePath("../scripts/setup-permissions.sh"));
}

void Backend::recheckKeyboardAccess()
{
    HotkeyEngine::instance().retryEvdev();
    ++m_rev;
    emit revChanged();
}

void Backend::configurePortalShortcuts() { HotkeyEngine::instance().start(); }

// ------------------------------------------------------------------ microphones

QVariantList Backend::microphones() const
{
    QVariantList out;
    auto &m = MicrophoneManager::instance();
    const auto current = m.recordingDevice();
    for (const auto &d : m.inputs()) {
        out << QVariantMap{{"id", QString::fromUtf8(d.id())},
                           {"name", d.description()},
                           {"selected", !m.followsSystemDefault() && current && current->id() == d.id()}};
    }
    return out;
}

void Backend::selectMicrophone(const QString &id)
{
    for (const auto &d : MicrophoneManager::instance().inputs())
        if (QString::fromUtf8(d.id()) == id) {
            MicrophoneManager::instance().select(d);
            if (m_micTest) { stopMicTest(); startMicTest(); }
            return;
        }
}

void Backend::selectSystemMicrophone() { MicrophoneManager::instance().selectSystemDefault(); }
QString Backend::selectedMicrophoneName() const { return MicrophoneManager::instance().selectedDisplayName(); }
bool Backend::microphoneAvailable() const { return MicrophoneManager::instance().recordingDevice().has_value(); }

int Backend::micSystemVolume() const { return MicrophoneManager::instance().systemVolume(); }
void Backend::setMicSystemVolume(int percent) { MicrophoneManager::instance().setSystemVolume(percent); }

void Backend::startMicTest()
{
    stopMicTest();
    m_micTestFile = QDir::tempPath() + QString("/jot-onboarding-mic-%1.wav").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    auto *cap = new AudioCapture(this);
    connect(cap, &AudioCapture::level, this, [this](float l) { m_micLevel = l; emit micTestLevelChanged(); });
    connect(cap, &AudioCapture::engineDied, this, [this](const QString &) { emit micTestStatus("dead"); });
    const auto err = cap->start(m_micTestFile);
    if (err != AudioCapture::StartError::None) {
        cap->deleteLater();
        emit micTestStatus(err == AudioCapture::StartError::NoInputDevice ? "missing" : "dead");
        return;
    }
    m_micTest = cap;
    emit micTestStatus("listening");
}

void Backend::stopMicTest()
{
    if (!m_micTest) return;
    QPointer<AudioCapture> cap = m_micTest;
    m_micTest = nullptr;
    const QString file = m_micTestFile;
    cap->stop([cap, file](auto) {
        QFile::remove(file);
        if (cap) cap->deleteLater();
    });
    m_micLevel = 0;
    emit micTestLevelChanged();
}

// ------------------------------------------------------------------ keys

bool Backend::geminiKeyStored() const { return !SecretStore::instance().geminiKey().isEmpty(); }
bool Backend::openRouterKeyStored() const { return !SecretStore::instance().openRouterKey().isEmpty(); }

void Backend::validateGeminiKey(const QString &key, bool checkModel)
{
    const QString k = key.trimmed();
    const auto config = Settings::instance().geminiConfig();
    GeminiClient::instance().validateKey(config.endpoint, k, [this, k, checkModel, config](GeminiClient::KeyCheck check, QString message) {
        if (check == GeminiClient::KeyCheck::Rejected) {
            emit geminiKeyResult("rejected", message);
            return;
        }
        const bool unreachable = check == GeminiClient::KeyCheck::Unreachable;
        auto save = [this, k, unreachable](bool modelOK) {
            if (!SecretStore::instance().setGeminiKey(k)) {
                emit geminiKeyResult("saveFailed", SecretStore::instance().lastError());
                return;
            }
            if (unreachable) emit geminiKeyResult("savedOffline", {});
            else if (!modelOK) emit geminiKeyResult("noModelAccess", {});
            else emit geminiKeyResult("valid", {});
        };
        if (checkModel && !unreachable) {
            GeminiClient::instance().resolveAvailableModel({config.transcribeModel}, config.endpoint, k,
                                                           [save](QString model) { save(!model.isEmpty()); });
        } else {
            save(true);
        }
    });
}

bool Backend::removeGeminiKey() { return SecretStore::instance().setGeminiKey({}); }

void Backend::validateOpenRouterKey(const QString &key)
{
    const QString typed = key.trimmed();
    const QString k = typed.isEmpty() ? SecretStore::instance().openRouterKey() : typed;
    TranscriptionService::validateOpenRouterKey(k, [this, typed](TranscriptionError e) {
        if (!e.ok()) {
            emit openRouterResult(false, QString("OpenRouter validation failed: %1. Saved key unchanged.").arg(e.describe()));
            return;
        }
        if (!typed.isEmpty() && !SecretStore::instance().setOpenRouterKey(typed)) {
            emit openRouterResult(false, QCoreApplication::translate("Jot", "Could not save the OpenRouter key to Keychain"));
            return;
        }
        emit openRouterResult(true, QCoreApplication::translate("Jot", "OpenRouter key is valid"));
    });
}

bool Backend::removeOpenRouterKey() { return SecretStore::instance().setOpenRouterKey({}); }

// ------------------------------------------------------------------ history

QVariantMap Backend::recordToMap(const DictationRecord &r) const
{
    const QString audio = FileLayout::audioFile(r.folder);
    return QVariantMap{
        {"id", r.id},
        {"startedAt", r.startedAt.toLocalTime()},
        {"status", r.status},
        {"app", r.targetAppName},
        {"duration", r.durationSeconds ? QVariant(*r.durationSeconds) : QVariant()},
        {"raw", r.rawTranscript ? QVariant(*r.rawTranscript) : QVariant()},
        {"cleaned", r.cleanedTranscript ? QVariant(*r.cleanedTranscript) : QVariant()},
        {"text", r.displayText()},
        {"errorCode", r.errorCode.value_or(QString())},
        {"errorMessage", r.errorMessage.value_or(QString())},
        {"pipeline", r.pipelineSeconds ? QVariant(*r.pipelineSeconds) : QVariant()},
        {"audioExists", QFileInfo::exists(audio)},
        {"ageSeconds", r.startedAt.secsTo(QDateTime::currentDateTime())},
    };
}

bool Backend::historyAvailable() const { return m_controller->history()->isOpen(); }

QVariantMap Backend::historyStats() const
{
    const auto s = m_controller->history()->stats();
    return {{"words", s.totalWords}, {"dictations", s.totalDictations}, {"wpm", s.averageWPM}};
}

QVariantList Backend::historyRecords(const QString &query) const
{
    QVariantList out;
    for (const auto &r : m_controller->history()->records(query)) out << recordToMap(r);
    return out;
}

QVariantList Backend::attentionRecords(const QString &query) const
{
    QVariantList out;
    for (const auto &r : m_controller->history()->records(query, 500, true)) out << recordToMap(r);
    return out;
}

QVariantMap Backend::latestRecord() const
{
    const auto list = m_controller->history()->records({}, 1);
    return list.isEmpty() ? QVariantMap{} : recordToMap(list.first());
}

void Backend::deleteRecord(const QString &id)
{
    if (m_playingId == id) stopAudio();
    m_controller->history()->remove(id, true);
}

void Backend::deleteAllHistory()
{
    stopAudio();
    m_controller->history()->removeAll(true, m_controller->coordinator()->activeSessionFolder());
    m_controller->coordinator()->clearLastResult();
}

void Backend::retryRecord(const QString &id)
{
    if (const auto r = m_controller->history()->record(id)) m_controller->retryFromHistory(*r);
}

void Backend::copyText(const QString &text) { Inserter::instance().copyOnly(text); }

void Backend::playAudio(const QString &id)
{
    const auto r = m_controller->history()->record(id);
    if (!r) return;
    if (!m_player) {
        m_player = new QMediaPlayer(this);
        m_audioOut = new QAudioOutput(this);
        m_player->setAudioOutput(m_audioOut);
        connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState st) {
            if (st == QMediaPlayer::StoppedState) { m_playingId.clear(); emit playingChanged(); }
        });
    }
    m_player->setSource(QUrl::fromLocalFile(FileLayout::audioFile(r->folder)));
    m_player->play();
    m_playingId = id;
    emit playingChanged();
}

void Backend::stopAudio()
{
    if (m_player) m_player->stop();
    m_playingId.clear();
    emit playingChanged();
}

// ------------------------------------------------------------------ dictionary

QVariantList Backend::dictionaryEntries(const QString &query) const
{
    auto list = DictionaryStore::instance().entries();
    std::stable_sort(list.begin(), list.end(), [](const auto &a, const auto &b) {
        if (a.starred != b.starred) return a.starred;
        return a.createdAt > b.createdAt;
    });
    QVariantList out;
    const QString q = query.trimmed().toLower();
    for (const auto &e : list) {
        if (!q.isEmpty()) {
            bool match = e.term.toLower().contains(q);
            for (const auto &m : e.misspellings) match = match || m.toLower().contains(q);
            if (!match) continue;
        }
        out << QVariantMap{{"id", e.id.toString(QUuid::WithoutBraces)}, {"term", e.term},
                           {"forms", e.misspellings}, {"starred", e.starred}};
    }
    return out;
}

int Backend::dictionaryCount() const { return int(DictionaryStore::instance().entries().size()); }

QString Backend::addTerm(const QString &term, const QString &forms)
{
    const QString trimmed = term.trimmed();
    QStringList list;
    for (const QString &f : forms.split(',')) if (!f.trimmed().isEmpty()) list << f.trimmed();
    bool existed = false;
    for (const auto &e : DictionaryStore::instance().entries())
        if (e.term.toLower() == trimmed.toLower()) existed = true;
    if (!DictionaryStore::instance().add(trimmed, list))
        return trimmed.size() > 60 ? QCoreApplication::translate("Jot", "Keep terms under 60 characters") : QCoreApplication::translate("Jot", "Already in your dictionary");
    return existed ? QCoreApplication::translate("Jot", "Added to %1").arg(trimmed) : QString();
}

void Backend::toggleStar(const QString &id) { DictionaryStore::instance().toggleStar(QUuid::fromString(id)); }
void Backend::removeTerm(const QString &id) { DictionaryStore::instance().remove(QUuid::fromString(id)); }
void Backend::setForms(const QString &id, const QString &lines)
{
    DictionaryStore::instance().setMisspellings(QUuid::fromString(id), lines.split('\n'));
}

int Backend::importCSV(const QUrl &url)
{
    QFile f(url.toLocalFile());
    if (!f.open(QIODevice::ReadOnly)) return 0;
    return DictionaryStore::instance().importCSV(QString::fromUtf8(f.readAll()));
}

int Backend::exportCSV(const QUrl &url)
{
    QSaveFile f(url.toLocalFile());
    if (!f.open(QIODevice::WriteOnly)) return -1;
    f.write(DictionaryStore::instance().exportCSV().toUtf8());
    if (!f.commit()) return -1;
    return dictionaryCount();
}

} // namespace jot
