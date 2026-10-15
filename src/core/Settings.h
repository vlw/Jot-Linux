// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) to Linux/Qt.
#pragma once

#include "Types.h"
#include <QDateTime>
#include <QObject>
#include <QSettings>

namespace jot {

/// QSettings-backed settings (~/.config/jot-linux/Jot.conf). Same keys and
/// defaults as upstream SettingsStore; every write emits changed(key) so live
/// surfaces (pill, tray, hotkey engine) update the moment a toggle flips.
class Settings : public QObject {
    Q_OBJECT
public:
    static Settings &instance();

    // Generic access
    QVariant value(const QString &key, const QVariant &def = {}) const;
    void set(const QString &key, const QVariant &value);
    void remove(const QString &key);

    // Onboarding
    bool hasCompletedOnboarding() const { return value("hasCompletedOnboarding", false).toBool(); }
    void setHasCompletedOnboarding(bool v) { set("hasCompletedOnboarding", v); }

    // Provider
    TranscriptionProvider transcriptionProvider() const;
    void setTranscriptionProvider(TranscriptionProvider p) { set("transcriptionProvider", providerKey(p)); }
    MAIStyle maiStyle() const { return value("maiStyle", "verbatim").toString() == "clean" ? MAIStyle::Clean : MAIStyle::Verbatim; }
    void setMAIStyle(MAIStyle s) { set("maiStyle", maiStyleKey(s)); }
    TranscriptionOptions transcriptionOptions() const;
    bool hasSelectedAPIKey() const;

    GeminiConfig geminiConfig() const;
    static QString usableEndpointURL(const QString &raw);

    bool doubleTapLockEnabled() const { return value("doubleTapLock", false).toBool(); }
    void setDoubleTapLock(bool v) { set("doubleTapLock", v); }
    // The pill appears only while dictating; the resting dot is opt-in.
    bool showIdleIndicator() const { return value("showIdleIndicator", false).toBool(); }
    void setShowIdleIndicator(bool v) { set("showIdleIndicator", v); }
    bool soundsEnabled() const { return value("soundsEnabled", true).toBool(); }
    void setSoundsEnabled(bool v) { set("soundsEnabled", v); }
    bool pauseMediaWhileRecording() const { return value("pauseMediaWhileRecording", true).toBool(); }
    void setPauseMediaWhileRecording(bool v) { set("pauseMediaWhileRecording", v); }

    // Formatting
    FormattingPolicy formattingPolicy() const { return {smartTranscriptionEnabled(), smartCleanupPassEnabled()}; }
    bool smartTranscriptionEnabled() const { return value("smartTranscription", true).toBool(); }
    void setSmartTranscription(bool v) { set("smartTranscription", v); }
    bool smartCleanupPassEnabled() const { return value("smartCleanupPass", false).toBool(); }
    void setSmartCleanupPass(bool v);
    bool usesLegacyTranscribeEndpoint() const { return value("legacyTranscribeEndpoint", false).toBool(); }
    void setLegacyTranscribeEndpoint(bool v) { set("legacyTranscribeEndpoint", v); }
    bool experimentalNoiseHandling() const { return value("experimentalNoiseHandling", false).toBool(); }
    void setExperimentalNoiseHandling(bool v) { set("experimentalNoiseHandling", v); }
    // Live streaming is on by default; the batch upload stays the fallback.
    bool liveTranscription() const { return value("liveTranscription", true).toBool(); }
    void setLiveTranscription(bool v) { set("liveTranscription", v); }
    bool liveTranscriptionActive() const { return liveTranscription() && !usesLegacyTranscribeEndpoint(); }

    QString endpointOverride() const { return value("endpointOverride").toString(); }
    QString transcribeModelOverride() const { return value("transcribeModelOverride").toString(); }
    QString cleanupModelOverride() const { return value("cleanupModelOverride").toString(); }
    void setEndpointOverride(const QString &v) { set("endpointOverride", v); }
    void setTranscribeModelOverride(const QString &v) { set("transcribeModelOverride", v); }
    void setCleanupModelOverride(const QString &v) { set("cleanupModelOverride", v); }

    int audioRetentionDays() const { return value("audioRetentionDays", 7).toInt(); }
    void setAudioRetentionDays(int d) { set("audioRetentionDays", d); }
    /// Hours to keep transcripts (whole History entries). 0 = forever. Default 15 days.
    int transcriptRetentionHours() const { return value("transcriptRetentionHours", 15 * 24).toInt(); }

    int recordGateTrip(const QDateTime &now = QDateTime::currentDateTimeUtc());

    // Linux-specific
    /// evdev key code of the dictation key (default KEY_RIGHTCTRL = 97).
    int hotkeyCode() const { return value("hotkeyCode", 97).toInt(); }
    void setHotkeyCode(int code) { set("hotkeyCode", code); }
    /// "auto" | "evdev" | "portal"
    QString hotkeyBackend() const { return value("hotkeyBackend", "auto").toString(); }
    void setHotkeyBackend(const QString &v) { set("hotkeyBackend", v); }
    /// Selected microphone: Qt device id (PulseAudio/PipeWire source name) and its
    /// description, used to re-resolve after the USB port changes. Empty id with
    /// "systemDefault" = follow the system default input.
    QString microphoneId() const { return value("microphoneId").toString(); }
    QString microphoneName() const { return value("microphoneName").toString(); }
    void setMicrophone(const QString &id, const QString &name) { set("microphoneId", id); set("microphoneName", name); }
    bool microphoneFollowsSystem() const { return value("microphoneFollowsSystem", false).toBool(); }
    void setMicrophoneFollowsSystem(bool v) { set("microphoneFollowsSystem", v); }
    /// Paste shortcut used after the transcript is on the clipboard.
    /// "auto" (Ctrl+Shift+V for terminals, Ctrl+V otherwise) | "ctrlv" | "ctrlshiftv" | "shiftinsert" | "none"
    /// Software gain Jot applies to captured audio, in dB (0…24). Separate from
    /// the system input volume; a soft limiter keeps loud speech from clipping.
    double micGainDb() const { return value("micGainDb", 0.0).toDouble(); }
    void setMicGainDb(double db) { set("micGainDb", db); }
    /// Wall-clock budget from key release to a transcript, in seconds. Long recordings
    /// get extra time (see DictationCoordinator::transcriptionBudget).
    double transcriptionTimeoutSeconds() const { return value("transcriptionTimeout", 8.0).toDouble(); }
    /// Recordings shorter than this are treated as accidental presses and never sent.
    double minimumRecordingSeconds() const { return value("minimumRecording", 1.0).toDouble(); }
    QString pasteShortcut() const { return value("pasteShortcut", "auto").toString(); }
    void setPasteShortcut(const QString &v) { set("pasteShortcut", v); }

signals:
    void changed(const QString &key);
    void smartFormattingAutoDegraded();

private:
    explicit Settings(QObject *parent = nullptr);
    mutable QSettings m_settings;
};

} // namespace jot
