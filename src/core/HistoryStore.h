// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) to Linux/Qt.
#pragma once

#include "SessionMeta.h"
#include <QObject>
#include <QSqlDatabase>
#include <functional>

namespace jot {

struct DictationRecord {
    QString id;
    QString folder;
    QDateTime startedAt;
    QString status;
    QString targetAppName;
    QString targetAppBundleID;
    std::optional<double> durationSeconds;
    std::optional<QString> rawTranscript;
    std::optional<QString> cleanedTranscript;
    std::optional<QString> errorCode;
    std::optional<QString> errorMessage;
    std::optional<double> pipelineSeconds;

    QString displayText() const { return cleanedTranscript.value_or(rawTranscript.value_or(QString())); }
    static DictationRecord fromMeta(const SessionMeta &meta, const QString &folder);
};

/// SQLite index over the per-session folders (which stay the source of truth).
class HistoryStore : public QObject {
    Q_OBJECT
public:
    explicit HistoryStore(const QString &databasePath, QObject *parent = nullptr);
    static HistoryStore *standard();
    bool isOpen() const { return m_ok; }

    void upsert(const SessionMeta &meta, const QString &folder);
    void remove(const QString &id, bool removeFolder);
    void removeAll(bool removeFolders, const QString &sparingFolder = {});
    void reindex();

    QList<DictationRecord> records(const QString &query = {}, int limit = 500, bool attentionOnly = false) const;
    QList<DictationRecord> retryableRecords() const;
    QList<DictationRecord> interruptedRecords(const QDateTime &cutoff) const;
    QList<DictationRecord> recordsStartedBefore(const QDateTime &cutoff) const;
    std::optional<DictationRecord> record(const QString &id) const;

    struct Stats { int totalWords = 0; int totalDictations = 0; int averageWPM = 0; };
    Stats stats() const;

    std::function<void(const DictationRecord &)> onUserDelete;

signals:
    void changed();

private:
    QList<DictationRecord> fetch(const QString &sql, const QVariantList &args) const;
    bool migrate();
    QString m_connection;
    bool m_ok = false;
};

} // namespace jot
