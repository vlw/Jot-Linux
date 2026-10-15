// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) — RetryQueue,
// RecoveryScanner and RetentionPolicy.
#include "RetryQueue.h"
#include "../core/FileLayout.h"
#include "../core/Settings.h"
#include "../core/TextPipeline.h"
#include "../net/GeminiClient.h"
#include "../net/LiveSession.h"
#include "../net/TranscriptionService.h"
#include "../platform/Inserter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcRetry, "jot.retry")

namespace jot {

RetryQueue::RetryQueue(HistoryStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
}

bool RetryQueue::isCompleted(const QString &folder)
{
    const auto meta = SessionMeta::read(folder);
    if (!meta) return false;
    return meta->status == SessionMeta::Status::AwaitingChip || meta->status == SessionMeta::Status::Inserted
        || meta->status == SessionMeta::Status::Recovered;
}

void RetryQueue::acquireExclusive(std::function<void()> then)
{
    ++m_manualWaiting;
    auto *poll = new QTimer(this);
    auto attempt = [this, then, poll] {
        if (m_draining) return;
        poll->stop();
        poll->deleteLater();
        --m_manualWaiting;
        m_draining = true;
        then();
    };
    connect(poll, &QTimer::timeout, this, attempt);
    if (!m_draining) { attempt(); return; }
    poll->start(200);
}

void RetryQueue::finishExclusive()
{
    m_draining = false;
}

void RetryQueue::retrySingle(const DictationRecord &record, std::function<void(Outcome)> cb)
{
    if (m_manualIDs.contains(record.id)) { cb(Outcome::AlreadyRetrying); return; }
    m_manualIDs.insert(record.id);
    const bool completedAtPress = isCompleted(record.folder);
    acquireExclusive([=, this] {
        auto done = [=, this](Outcome o) {
            m_manualIDs.remove(record.id);
            finishExclusive();
            cb(o);
        };
        if (!completedAtPress && isCompleted(record.folder)) { done(Outcome::AlreadyDone); return; }
        process(record, true, [=, this](Process r, TranscriptionError error) {
            switch (r) {
            case Process::Recovered: done(Outcome::Recovered); break;
            case Process::StillOffline: done(Outcome::StillOffline); break;
            case Process::Blocked: done(Outcome::Blocked); break;
            case Process::Failed: done(Outcome::Failed); break;
            case Process::Skipped: done(Outcome::AlreadyDone); break;
            case Process::AudioUnavailable: done(Outcome::AudioUnavailable); break;
            case Process::KeptPreviousTranscript: done(Outcome::KeptPreviousTranscript); break;
            }
        });
    });
}

void RetryQueue::process(const DictationRecord &record, bool reprocess, std::function<void(Process, TranscriptionError)> cb)
{
    const TranscriptionOptions options = Settings::instance().transcriptionOptions();
    m_lastProvider = options.provider;
    m_lastFailure.reset();
    const QString folder = record.folder;
    auto metaOpt = SessionMeta::read(folder);
    if (!metaOpt) { cb(Process::Skipped, {}); return; }
    SessionMeta meta = *metaOpt;
    if (!reprocess && (meta.status == SessionMeta::Status::AwaitingChip || meta.status == SessionMeta::Status::Inserted
                       || meta.status == SessionMeta::Status::Recovered)) {
        cb(Process::Skipped, {});
        return;
    }
    if (!reprocess && meta.rawTranscript) {
        meta.status = SessionMeta::Status::Recovered;
        meta.errorCode.reset();
        meta.write(folder);
        m_store->upsert(meta, folder);
        cb(Process::Recovered, {});
        return;
    }
    const bool keepPrevious = reprocess && (meta.rawTranscript || meta.cleanedTranscript);
    const QString audio = FileLayout::audioFile(folder);
    if (!QFileInfo::exists(audio)) {
        if (keepPrevious) { cb(Process::AudioUnavailable, {}); return; }
        meta.status = SessionMeta::Status::Failed;
        meta.errorCode = QString("audio_purged");
        meta.write(folder);
        m_store->upsert(meta, folder);
        cb(Process::Failed, {});
        return;
    }
    DictationContext context;
    context.targetAppClass = meta.targetAppBundleID;
    context.targetAppName = meta.targetAppName;
    context.options = options;
    const double duration = meta.audioDurationSeconds.value_or(FileLayout::estimatedDuration(audio).value_or(60));
    const qint64 startedAt = QDateTime::currentMSecsSinceEpoch();

    auto onResult = [=, this](const TranscriptionResult &result) mutable {
        if (!QFileInfo::exists(folder)) { cb(Process::Skipped, {}); return; }
        meta.rawTranscript = result.rawTranscript;
        meta.cleanedTranscript = result.cleanedTranscript;
        meta.modelID = result.modelID;
        meta.audioDurationSeconds = duration;
        meta.pipelineSeconds = (QDateTime::currentMSecsSinceEpoch() - startedAt) / 1000.0;
        meta.errorCode.reset();
        meta.errorMessage.reset();
        Inserter::instance().copyOnly(result.cleanedTranscript);
        if (onTranscriptCopied) onTranscriptCopied(result.cleanedTranscript);
        meta.status = SessionMeta::Status::Recovered;
        meta.write(folder);
        m_store->upsert(meta, folder);
        cb(Process::Recovered, {});
    };

    auto onError = [=, this](const TranscriptionError &error) mutable {
        m_lastFailure = error;
        if (keepPrevious) {
            qCWarning(lcRetry) << "manual retranscription failed — previous transcript kept";
            cb(Process::KeptPreviousTranscript, error);
            return;
        }
        switch (error.kind) {
        case TranscriptionErrorKind::Offline:
        case TranscriptionErrorKind::Network:
        case TranscriptionErrorKind::Timeout:
        case TranscriptionErrorKind::RateLimitedTransient:
            persistFailure(meta, folder, error.kind == TranscriptionErrorKind::Timeout ? "timeout" : "network", std::nullopt);
            cb(Process::StillOffline, error);
            return;
        case TranscriptionErrorKind::Auth:
        case TranscriptionErrorKind::RateLimitedDaily:
            persistFailure(meta, folder, error.kind == TranscriptionErrorKind::Auth ? "auth" : "quota", std::nullopt);
            cb(Process::Blocked, error);
            return;
        case TranscriptionErrorKind::ModelUnavailable:
            persistFailure(meta, folder, "model", error.detail.isEmpty() ? QString("model %1 not accessible").arg(error.model) : error.detail);
            cb(Process::Failed, error);
            return;
        case TranscriptionErrorKind::BadRequest:
            persistFailure(meta, folder, "bad_request", error.detail);
            cb(Process::Failed, error);
            return;
        default:
            persistFailure(meta, folder, "retry_" + error.describe(), std::nullopt);
            cb(Process::Failed, error);
        }
    };

    auto upload = [=]() mutable {
        TranscriptionService::instance().transcribe(audio, duration, context, makeToken(),
            [=](const TranscriptionResult &r, const TranscriptionError &e) mutable {
                if (e.ok()) onResult(r);
                else onError(e);
            });
    };

    // History Retry goes through Live when Live is enabled for dictation.
    if (reprocess && options.provider == TranscriptionProvider::Gemini && options.live) {
        if (auto *live = LiveTranscriber::create(context, duration + 2, this)) {
            live->begin([=](bool ok) mutable {
                if (!ok) { live->deleteLater(); upload(); return; }
                const QByteArray pcm = readPCMFromWav(audio);
                for (qsizetype off = 0; off < pcm.size(); off += 3200) live->enqueue(pcm.mid(off, 3200));
                live->finish(TimeoutPolicy::liveFinal + duration / 4, pcm.size() / 2,
                             [=](std::optional<TranscriptionResult> r) mutable {
                    live->deleteLater();
                    if (r) onResult(*r);
                    else upload();
                });
            });
            return;
        }
    }
    upload();
}

void RetryQueue::persistFailure(SessionMeta &meta, const QString &folder, const QString &code, const std::optional<QString> &message)
{
    if (!QFileInfo::exists(folder)) return;
    meta.status = SessionMeta::Status::Failed;
    meta.errorCode = code;
    meta.errorMessage = message;
    meta.write(folder);
    m_store->upsert(meta, folder);
}

// ================================================================ Recovery

RecoveryScanner::RecoveryScanner(HistoryStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
}

void RecoveryScanner::scanAndRecover(const QDateTime &cutoff, std::function<void()> done)
{
    m_store->reindex();
    const auto interrupted = m_store->interruptedRecords(cutoff);
    if (interrupted.isEmpty()) { done(); return; }
    qCInfo(lcRetry) << interrupted.size() << "interrupted session(s) found";
    for (int i = 0; i < interrupted.size(); ++i) {
        const auto &record = interrupted[i];
        auto metaOpt = SessionMeta::read(record.folder);
        if (!metaOpt) continue;
        SessionMeta meta = *metaOpt;
        if (meta.rawTranscript) {
            meta.status = SessionMeta::Status::Recovered;
            meta.write(record.folder);
            m_store->upsert(meta, record.folder);
            if (i == 0 && onRecovered) onRecovered(QCoreApplication::translate("Jot", "Recovered your last dictation — it's in History"));
            continue;
        }
        const QString audio = FileLayout::audioFile(record.folder);
        if (!QFileInfo::exists(audio)) {
            meta.status = SessionMeta::Status::Failed;
            meta.errorCode = QString("no_audio_file");
            meta.write(record.folder);
            m_store->upsert(meta, record.folder);
            continue;
        }
        // Interrupted mid-dictation (crash, power loss): never sent again automatically.
        // The audio stays in History for a look and an optional manual Retry.
        if (!meta.audioDurationSeconds) meta.audioDurationSeconds = FileLayout::estimatedDuration(audio);
        meta.status = SessionMeta::Status::Failed;
        meta.errorCode = QString("interrupted");
        meta.write(record.folder);
        m_store->upsert(meta, record.folder);
    }
    done();
}

// ================================================================ Retention

int RetentionPolicy::purgeExpiredTranscripts(HistoryStore *store, int hours, const QString &activeFolder)
{
    if (hours <= 0 || !store) return 0;
    const QDateTime cutoff = QDateTime::currentDateTime().addSecs(-qint64(hours) * 3600);
    int removed = 0;
    for (const auto &r : store->recordsStartedBefore(cutoff)) {
        if (!activeFolder.isEmpty() && QDir(r.folder).canonicalPath() == QDir(activeFolder).canonicalPath()) continue;
        store->remove(r.id, true);
        ++removed;
    }
    if (removed) qCInfo(lcRetry) << "RetentionPolicy: removed" << removed << "History entr(ies) older than" << hours << "h";
    return removed;
}

void RetentionPolicy::purgeExpiredAudio(int days)
{
    if (days == 0) return;
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime cutoff = days < 0 ? now.addSecs(60) : now.addSecs(-qint64(days) * 86400);
    const QDir root(FileLayout::recordingsRoot());
    int purged = 0;
    for (const QString &name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString folder = root.absoluteFilePath(name);
        const auto meta = SessionMeta::read(folder);
        if (!meta || meta->startedAt >= cutoff) continue;
        if (!meta->rawTranscript && meta->status != SessionMeta::Status::Silent) continue;
        for (const QString &f : {FileLayout::audioFile(folder), FileLayout::audioFLAC(folder)})
            if (QFile::exists(f) && QFile::remove(f)) ++purged;
    }
    if (purged) qCInfo(lcRetry) << "RetentionPolicy: purged" << purged << "audio file(s) older than" << days << "d";
}

} // namespace jot
