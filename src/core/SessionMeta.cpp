// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) to Linux/Qt.
#include "SessionMeta.h"
#include "FileLayout.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>

Q_LOGGING_CATEGORY(lcHistoryMeta, "jot.history")

namespace jot {

static const QList<QPair<SessionMeta::Status, QString>> kStatusKeys = {
    {SessionMeta::Status::Recording, "recording"},
    {SessionMeta::Status::Recorded, "recorded"},
    {SessionMeta::Status::Transcribing, "transcribing"},
    {SessionMeta::Status::Inserted, "inserted"},
    {SessionMeta::Status::CopiedToClipboard, "copiedToClipboard"},
    {SessionMeta::Status::AwaitingChip, "awaitingChip"},
    {SessionMeta::Status::HeldSecure, "heldSecure"},
    {SessionMeta::Status::QueuedForRetry, "queuedForRetry"},
    {SessionMeta::Status::Recovered, "recovered"},
    {SessionMeta::Status::Silent, "silent"},
    {SessionMeta::Status::Cancelled, "cancelled"},
    {SessionMeta::Status::Failed, "failed"},
};

QString SessionMeta::statusKey(Status s)
{
    for (const auto &[st, key] : kStatusKeys) if (st == s) return key;
    return "failed";
}

SessionMeta::Status SessionMeta::statusFromKey(const QString &k)
{
    for (const auto &[st, key] : kStatusKeys) if (key == k) return st;
    return Status::Failed;
}

void SessionMeta::write(const QString &folder) const
{
    QJsonObject o;
    o["id"] = id.toString(QUuid::WithoutBraces).toUpper();
    o["startedAt"] = startedAt.toUTC().toString(Qt::ISODate);
    o["status"] = statusKey(status);
    if (!targetAppBundleID.isEmpty()) o["targetAppBundleID"] = targetAppBundleID;
    if (!targetAppName.isEmpty()) o["targetAppName"] = targetAppName;
    if (audioDurationSeconds) o["audioDurationSeconds"] = *audioDurationSeconds;
    QJsonArray gaps;
    for (double g : gapMarkers) gaps << g;
    o["gapMarkers"] = gaps;
    if (rawTranscript) o["rawTranscript"] = *rawTranscript;
    if (cleanedTranscript) o["cleanedTranscript"] = *cleanedTranscript;
    if (errorCode) o["errorCode"] = *errorCode;
    if (errorMessage) o["errorMessage"] = *errorMessage;
    if (modelID) o["modelID"] = *modelID;
    if (pipelineSeconds) o["pipelineSeconds"] = *pipelineSeconds;
    if (noiseFloorDBFS) o["noiseFloorDBFS"] = *noiseFloorDBFS;
    if (speechPeakDBFS) o["speechPeakDBFS"] = *speechPeakDBFS;

    QSaveFile f(FileLayout::metaJSON(folder));
    if (!f.open(QIODevice::WriteOnly)) {
        qCWarning(lcHistoryMeta) << "SessionMeta: write failed for" << id;
        return;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    if (!f.commit()) qCWarning(lcHistoryMeta) << "SessionMeta: commit failed for" << id;
}

std::optional<SessionMeta> SessionMeta::read(const QString &folder)
{
    QFile f(FileLayout::metaJSON(folder));
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return std::nullopt;
    const QJsonObject o = doc.object();
    SessionMeta m;
    m.id = QUuid::fromString(o.value("id").toString());
    if (m.id.isNull()) return std::nullopt;
    m.startedAt = QDateTime::fromString(o.value("startedAt").toString(), Qt::ISODate);
    m.status = statusFromKey(o.value("status").toString());
    m.targetAppBundleID = o.value("targetAppBundleID").toString();
    m.targetAppName = o.value("targetAppName").toString();
    auto optD = [&](const char *k) -> std::optional<double> {
        return o.contains(k) && o.value(k).isDouble() ? std::optional<double>(o.value(k).toDouble()) : std::nullopt;
    };
    auto optS = [&](const char *k) -> std::optional<QString> {
        return o.contains(k) && o.value(k).isString() ? std::optional<QString>(o.value(k).toString()) : std::nullopt;
    };
    m.audioDurationSeconds = optD("audioDurationSeconds");
    for (const auto &g : o.value("gapMarkers").toArray()) m.gapMarkers << g.toDouble();
    m.rawTranscript = optS("rawTranscript");
    m.cleanedTranscript = optS("cleanedTranscript");
    m.errorCode = optS("errorCode");
    m.errorMessage = optS("errorMessage");
    m.modelID = optS("modelID");
    m.pipelineSeconds = optD("pipelineSeconds");
    m.noiseFloorDBFS = optD("noiseFloorDBFS");
    m.speechPeakDBFS = optD("speechPeakDBFS");
    return m;
}

} // namespace jot
