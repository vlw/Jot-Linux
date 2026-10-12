// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) to Linux/Qt.
#include "HistoryStore.h"
#include "FileLayout.h"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QRegularExpression>
#include <QFileInfo>

Q_LOGGING_CATEGORY(lcHistory, "jot.history")

namespace jot {

DictationRecord DictationRecord::fromMeta(const SessionMeta &meta, const QString &folder)
{
    DictationRecord r;
    r.id = meta.id.toString(QUuid::WithoutBraces).toUpper();
    r.folder = folder;
    r.startedAt = meta.startedAt;
    r.status = SessionMeta::statusKey(meta.status);
    r.targetAppName = meta.targetAppName;
    r.targetAppBundleID = meta.targetAppBundleID;
    r.durationSeconds = meta.audioDurationSeconds;
    r.rawTranscript = meta.rawTranscript;
    r.cleanedTranscript = meta.cleanedTranscript;
    r.errorCode = meta.errorCode;
    r.errorMessage = meta.errorMessage;
    r.pipelineSeconds = meta.pipelineSeconds;
    return r;
}

HistoryStore *HistoryStore::standard()
{
    static HistoryStore *store = new HistoryStore(FileLayout::appDataRoot() + "/history.sqlite");
    return store;
}

HistoryStore::HistoryStore(const QString &databasePath, QObject *parent)
    : QObject(parent)
    , m_connection("jot-history")
{
    FileLayout::ensureDir(QFileInfo(databasePath).absolutePath());
    auto open = [&] {
        QSqlDatabase db = QSqlDatabase::contains(m_connection) ? QSqlDatabase::database(m_connection, false)
                                                               : QSqlDatabase::addDatabase("QSQLITE", m_connection);
        db.setDatabaseName(databasePath);
        if (!db.open()) return false;
        QSqlQuery q(db);
        if (!q.exec("PRAGMA schema_version")) return false;
        q.exec("PRAGMA journal_mode = WAL");
        q.exec("PRAGMA synchronous = NORMAL");
        return true;
    };
    m_ok = open();
    if (!m_ok) {
        qCWarning(lcHistory) << "HistoryStore: open failed — quarantining and recreating index";
        QSqlDatabase::database(m_connection, false).close();
        QFile::rename(databasePath, databasePath + QString(".corrupt-%1.sqlite").arg(QDateTime::currentSecsSinceEpoch()));
        m_ok = open();
    }
    if (m_ok) m_ok = migrate();
}

bool HistoryStore::migrate()
{
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    QSqlQuery q(db);
    q.exec("CREATE TABLE IF NOT EXISTS migrations (name TEXT PRIMARY KEY)");
    auto applied = [&](const QString &name) {
        QSqlQuery c(db);
        c.prepare("SELECT 1 FROM migrations WHERE name = ?");
        c.addBindValue(name);
        return c.exec() && c.next();
    };
    auto mark = [&](const QString &name) {
        QSqlQuery c(db);
        c.prepare("INSERT INTO migrations(name) VALUES (?)");
        c.addBindValue(name);
        c.exec();
    };
    if (!applied("v1")) {
        if (!q.exec("CREATE TABLE IF NOT EXISTS dictation ("
                    " id TEXT PRIMARY KEY, folder TEXT NOT NULL, startedAt TEXT NOT NULL,"
                    " status TEXT NOT NULL, targetAppName TEXT, targetAppBundleID TEXT,"
                    " durationSeconds REAL, rawTranscript TEXT, cleanedTranscript TEXT,"
                    " errorCode TEXT, pipelineSeconds REAL)")) {
            qCWarning(lcHistory) << q.lastError().text();
            return false;
        }
        q.exec("CREATE INDEX IF NOT EXISTS dictation_startedAt ON dictation(startedAt)");
        q.exec("CREATE INDEX IF NOT EXISTS dictation_status ON dictation(status)");
        mark("v1");
    }
    if (!applied("v2-errorMessage")) {
        q.exec("ALTER TABLE dictation ADD COLUMN errorMessage TEXT");
        mark("v2-errorMessage");
    }
    return true;
}

static QVariant opt(const std::optional<QString> &s) { return s ? QVariant(*s) : QVariant(QMetaType(QMetaType::QString)); }
static QVariant opt(const std::optional<double> &d) { return d ? QVariant(*d) : QVariant(QMetaType(QMetaType::Double)); }
static QString isoUTC(const QDateTime &d) { return d.toUTC().toString("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'"); }

void HistoryStore::upsert(const SessionMeta &meta, const QString &folder)
{
    if (!m_ok) return;
    const DictationRecord r = DictationRecord::fromMeta(meta, folder);
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare("INSERT OR REPLACE INTO dictation (id, folder, startedAt, status, targetAppName, targetAppBundleID,"
              " durationSeconds, rawTranscript, cleanedTranscript, errorCode, pipelineSeconds, errorMessage)"
              " VALUES (?,?,?,?,?,?,?,?,?,?,?,?)");
    q.addBindValue(r.id);
    q.addBindValue(r.folder);
    q.addBindValue(isoUTC(r.startedAt));
    q.addBindValue(r.status);
    q.addBindValue(r.targetAppName.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(r.targetAppName));
    q.addBindValue(r.targetAppBundleID.isEmpty() ? QVariant(QMetaType(QMetaType::QString)) : QVariant(r.targetAppBundleID));
    q.addBindValue(opt(r.durationSeconds));
    q.addBindValue(opt(r.rawTranscript));
    q.addBindValue(opt(r.cleanedTranscript));
    q.addBindValue(opt(r.errorCode));
    q.addBindValue(opt(r.pipelineSeconds));
    q.addBindValue(opt(r.errorMessage));
    if (!q.exec()) qCWarning(lcHistory) << "upsert failed" << q.lastError().text();
    emit changed();
}

std::optional<DictationRecord> HistoryStore::record(const QString &id) const
{
    const auto list = fetch("SELECT * FROM dictation WHERE id = ?", {id});
    if (list.isEmpty()) return std::nullopt;
    return list.first();
}

void HistoryStore::remove(const QString &id, bool removeFolder)
{
    if (!m_ok) return;
    const auto rec = record(id);
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare("DELETE FROM dictation WHERE id = ?");
    q.addBindValue(id);
    q.exec();
    if (removeFolder && rec) {
        QDir(rec->folder).removeRecursively();
        if (onUserDelete) onUserDelete(*rec);
    }
    emit changed();
}

void HistoryStore::removeAll(bool removeFolders, const QString &sparingFolder)
{
    if (m_ok) {
        QSqlQuery q(QSqlDatabase::database(m_connection));
        q.exec("DELETE FROM dictation");
        emit changed();
    }
    if (removeFolders) {
        const QDir root(FileLayout::recordingsRoot());
        for (const QString &name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString path = root.absoluteFilePath(name);
            if (!sparingFolder.isEmpty() && QDir(path).canonicalPath() == QDir(sparingFolder).canonicalPath()) continue;
            QDir(path).removeRecursively();
        }
    }
}

void HistoryStore::reindex()
{
    if (!m_ok) return;
    const QDir root(FileLayout::recordingsRoot());
    for (const QString &name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString folder = root.absoluteFilePath(name);
        if (auto meta = SessionMeta::read(folder)) upsert(*meta, folder);
    }
    int orphans = 0;
    for (const auto &r : fetch("SELECT * FROM dictation", {})) {
        if (!QFileInfo::exists(r.folder)) {
            remove(r.id, false);
            ++orphans;
        }
    }
    if (orphans) qCInfo(lcHistory) << "reindex pruned" << orphans << "orphaned row(s)";
}

QList<DictationRecord> HistoryStore::fetch(const QString &sql, const QVariantList &args) const
{
    QList<DictationRecord> out;
    if (!m_ok) return out;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.prepare(sql);
    for (const auto &a : args) q.addBindValue(a);
    if (!q.exec()) {
        qCWarning(lcHistory) << "query failed" << q.lastError().text();
        return out;
    }
    auto optS = [&](const char *k) -> std::optional<QString> {
        const QVariant v = q.value(k);
        return v.isNull() ? std::nullopt : std::optional<QString>(v.toString());
    };
    auto optD = [&](const char *k) -> std::optional<double> {
        const QVariant v = q.value(k);
        return v.isNull() ? std::nullopt : std::optional<double>(v.toDouble());
    };
    while (q.next()) {
        DictationRecord r;
        r.id = q.value("id").toString();
        r.folder = q.value("folder").toString();
        r.startedAt = QDateTime::fromString(q.value("startedAt").toString(), Qt::ISODateWithMs);
        r.status = q.value("status").toString();
        r.targetAppName = q.value("targetAppName").toString();
        r.targetAppBundleID = q.value("targetAppBundleID").toString();
        r.durationSeconds = optD("durationSeconds");
        r.rawTranscript = optS("rawTranscript");
        r.cleanedTranscript = optS("cleanedTranscript");
        r.errorCode = optS("errorCode");
        r.errorMessage = optS("errorMessage");
        r.pipelineSeconds = optD("pipelineSeconds");
        out << r;
    }
    return out;
}

QList<DictationRecord> HistoryStore::records(const QString &query, int limit, bool attentionOnly) const
{
    QString visible = "(rawTranscript IS NOT NULL OR cleanedTranscript IS NOT NULL"
                      " OR status IN ('failed','queuedForRetry')"
                      " OR (status = 'cancelled' AND durationSeconds >= 10))"
                      " AND status != 'silent'";
    if (attentionOnly)
        visible += " AND (status IN ('failed','queuedForRetry') OR COALESCE(cleanedTranscript, rawTranscript, '') = '')";
    const int lim = attentionOnly ? -1 : limit;
    if (!query.trimmed().isEmpty()) {
        QString escaped = query;
        escaped.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_");
        const QString pattern = "%" + escaped + "%";
        return fetch("SELECT * FROM dictation WHERE (rawTranscript LIKE ? ESCAPE '\\' OR cleanedTranscript LIKE ? ESCAPE '\\'"
                     " OR targetAppName LIKE ? ESCAPE '\\') AND " + visible + " ORDER BY startedAt DESC LIMIT ?",
                     {pattern, pattern, pattern, lim});
    }
    return fetch("SELECT * FROM dictation WHERE " + visible + " ORDER BY startedAt DESC LIMIT ?", {lim});
}

QList<DictationRecord> HistoryStore::retryableRecords() const
{
    return fetch("SELECT * FROM dictation WHERE status = ? OR (status = ? AND errorCode IN (?,?,?,?,?)) ORDER BY startedAt ASC",
                 {"queuedForRetry", "failed", "network", "timeout", "rate_limit", "auth", "quota"});
}

QList<DictationRecord> HistoryStore::interruptedRecords(const QDateTime &cutoff) const
{
    return fetch("SELECT * FROM dictation WHERE status IN (?,?,?) AND startedAt < ? ORDER BY startedAt DESC",
                 {"recording", "recorded", "transcribing", isoUTC(cutoff)});
}

HistoryStore::Stats HistoryStore::stats() const
{
    Stats s;
    if (!m_ok) return s;
    QSqlQuery q(QSqlDatabase::database(m_connection));
    q.exec("SELECT cleanedTranscript, durationSeconds FROM dictation WHERE cleanedTranscript IS NOT NULL");
    int timedWords = 0;
    double speech = 0;
    while (q.next()) {
        const int count = int(q.value(0).toString().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).size());
        s.totalWords += count;
        ++s.totalDictations;
        const QVariant d = q.value(1);
        if (!d.isNull() && d.toDouble() > 0) {
            timedWords += count;
            speech += d.toDouble();
        }
    }
    s.averageWPM = speech > 10 ? int(double(timedWords) / (speech / 60)) : 0;
    return s;
}

} // namespace jot
