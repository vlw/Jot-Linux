// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) to Linux/Qt.
#pragma once

#include <QString>
#include <QStringList>
#include <QMetaType>
#include <optional>

namespace jot {

enum class TranscriptionProvider { Gemini, MAI };
inline QString providerKey(TranscriptionProvider p) { return p == TranscriptionProvider::Gemini ? "gemini" : "mai"; }
inline QString providerDisplayName(TranscriptionProvider p) {
    return p == TranscriptionProvider::Gemini ? "Gemini" : "MAI-Transcribe-2";
}

enum class MAIStyle { Verbatim, Clean };
inline QString maiStyleKey(MAIStyle s) { return s == MAIStyle::Verbatim ? "verbatim" : "clean"; }
inline QString maiStyleDisplayName(MAIStyle s) {
    return s == MAIStyle::Verbatim ? QStringLiteral("Дословно") : QStringLiteral("Со встроенной очисткой");
}

enum class TranscriptionMode { Verbatim, Smart };
inline QString modeKey(TranscriptionMode m) { return m == TranscriptionMode::Smart ? "smart" : "verbatim"; }

struct GeminiConfig {
    QString endpoint = QStringLiteral("https://generativelanguage.googleapis.com");
    // PRODUCT DECISION (upstream): Jot ships on gemini-3.5-transcribe.
    QString transcribeModel = QStringLiteral("gemini-3.5-transcribe");
    QString cleanupModel = QStringLiteral("gemini-3.5-flash-lite");
    bool operator==(const GeminiConfig &) const = default;
};

struct FormattingPolicy {
    bool nativeSmart = true;
    bool cleanupPass = false;
    TranscriptionMode mode() const { return nativeSmart ? TranscriptionMode::Smart : TranscriptionMode::Verbatim; }
    bool operator==(const FormattingPolicy &) const = default;
};

/// Captured at hotkey-down, or when a History operation starts.
struct TranscriptionOptions {
    TranscriptionProvider provider = TranscriptionProvider::Gemini;
    MAIStyle maiStyle = MAIStyle::Verbatim;
    GeminiConfig geminiConfig;
    FormattingPolicy formatting;
    bool legacy = false;
    bool live = false;
};

struct DictationContext {
    std::optional<TranscriptionOptions> options;
    QString targetAppClass;   // Wayland app id / resource class (bundle-id analogue)
    QString targetAppName;    // window caption / app name
    qint64 targetPID = 0;
    QString targetWindowId;   // KWin internal id, used for "focus changed" checks
};

struct TranscriptionResult {
    QString rawTranscript;
    QString cleanedTranscript;
    QString modelID;
};

enum class TranscriptionErrorKind {
    None,
    Offline,
    Network,
    BadRequest,
    Auth,
    ModelUnavailable,
    RateLimitedDaily,
    RateLimitedTransient,
    Timeout,
    EmptyTranscript,
    SafetyBlocked,
    Cancelled,
};

struct TranscriptionError {
    TranscriptionErrorKind kind = TranscriptionErrorKind::None;
    QString detail;   // message / model name
    QString model;
    bool ok() const { return kind == TranscriptionErrorKind::None; }
    QString describe() const;
    static TranscriptionError make(TranscriptionErrorKind k, QString d = {}) { return {k, std::move(d), {}}; }
};

enum class DictationFailure {
    Audio, NoMicrophone, NoAudio, Network, Auth, ModelAccess, BadRequest,
    RateLimited, QuotaExhausted, Timeout, Validation, SafetyBlocked, Storage,
};

enum class DictationOutcome { Inserted, AwaitingChip, CopiedToClipboard, HeldForSecureField, QueuedForRetry, Silent };

enum class InsertionOutcome { Inserted, FrontmostChanged, FellBackToClipboard, BlockedSecureField };

enum class SilenceReason { NoSpeech, TooNoisy, TooShort };

} // namespace jot

Q_DECLARE_METATYPE(jot::TranscriptionResult)
