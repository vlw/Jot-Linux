// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) to Linux/Qt.
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QUuid>
#include <optional>

namespace jot {

/// Crash-safe per-session metadata (meta.json next to the audio). History's
/// SQLite index is rebuilt from these files, so they are the source of truth.
struct SessionMeta {
    enum class Status {
        Recording, Recorded, Transcribing, Inserted, CopiedToClipboard, AwaitingChip,
        HeldSecure, QueuedForRetry, Recovered, Silent, Cancelled, Failed,
    };

    QUuid id;
    QDateTime startedAt;
    Status status = Status::Recording;
    QString targetAppBundleID;   // Wayland app class
    QString targetAppName;
    std::optional<double> audioDurationSeconds;
    QList<double> gapMarkers;
    std::optional<QString> rawTranscript;
    std::optional<QString> cleanedTranscript;
    std::optional<QString> errorCode;
    std::optional<QString> errorMessage;
    std::optional<QString> modelID;
    std::optional<double> pipelineSeconds;
    std::optional<double> noiseFloorDBFS;
    std::optional<double> speechPeakDBFS;

    void write(const QString &folder) const;
    static std::optional<SessionMeta> read(const QString &folder);

    static QString statusKey(Status s);
    static Status statusFromKey(const QString &k);
};

} // namespace jot
