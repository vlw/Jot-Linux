// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) to Linux/Qt.
#include "DictionaryStore.h"
#include "FileLayout.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

namespace jot {

static QString dictionaryPath() { return FileLayout::appDataRoot() + "/dictionary.json"; }

QStringList DictionaryEntry::normalizedForms(const QStringList &forms, const QString &term)
{
    QSet<QString> seen;
    QStringList out;
    for (QString form : forms) {
        form.replace('\n', ' ').replace('\r', ' ');
        QString clean = form.trimmed().left(60).trimmed();
        if (clean.isEmpty() || clean == term) continue;
        const QString key = clean.toLower();
        if (seen.contains(key)) continue;
        seen.insert(key);
        out << clean;
    }
    return out;
}

DictionaryStore &DictionaryStore::instance()
{
    static DictionaryStore s;
    return s;
}

QList<DictionaryEntry> DictionaryStore::entries() const
{
    if (m_loaded) return m_cache;
    m_loaded = true;
    m_cache.clear();
    QFile f(dictionaryPath());
    if (!f.open(QIODevice::ReadOnly)) return m_cache;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const auto &v : arr) {
        const QJsonObject o = v.toObject();
        DictionaryEntry e;
        e.id = QUuid::fromString(o.value("id").toString());
        if (e.id.isNull()) e.id = QUuid::createUuid();
        e.term = o.value("term").toString();
        for (const auto &m : o.value("misspellings").toArray()) e.misspellings << m.toString();
        if (e.misspellings.isEmpty() && !o.value("misspelling").toString().isEmpty())
            e.misspellings << o.value("misspelling").toString();
        e.starred = o.value("starred").toBool();
        e.createdAt = QDateTime::fromString(o.value("createdAt").toString(), Qt::ISODateWithMs);
        if (!e.createdAt.isValid()) e.createdAt = QDateTime::currentDateTimeUtc();
        if (!e.term.isEmpty()) m_cache << e;
    }
    return m_cache;
}

void DictionaryStore::save(const QList<DictionaryEntry> &list)
{
    QJsonArray arr;
    for (const auto &e : list) {
        QJsonObject o;
        o["id"] = e.id.toString(QUuid::WithoutBraces);
        o["term"] = e.term;
        o["misspellings"] = QJsonArray::fromStringList(e.misspellings);
        if (!e.misspellings.isEmpty()) o["misspelling"] = e.misspellings.first();
        o["starred"] = e.starred;
        o["createdAt"] = e.createdAt.toUTC().toString(Qt::ISODateWithMs);
        arr << o;
    }
    FileLayout::ensureDir(FileLayout::appDataRoot());
    QSaveFile f(dictionaryPath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
        f.commit();
    }
    m_cache = list;
    m_loaded = true;
    emit const_cast<DictionaryStore *>(this)->changed();
}

bool DictionaryStore::merge(const QString &term, const QStringList &forms, QList<DictionaryEntry> &list)
{
    for (auto &e : list) {
        if (e.term.toLower() == term.toLower()) {
            const QStringList merged = DictionaryEntry::normalizedForms(e.misspellings + forms, e.term);
            if (merged == e.misspellings) return false;
            e.misspellings = merged;
            return true;
        }
    }
    DictionaryEntry e;
    e.id = QUuid::createUuid();
    e.term = term;
    e.misspellings = DictionaryEntry::normalizedForms(forms, term);
    e.createdAt = QDateTime::currentDateTimeUtc();
    list << e;
    return true;
}

bool DictionaryStore::add(const QString &term, const QStringList &misspellings)
{
    const QString trimmed = term.trimmed();
    if (trimmed.size() < 1 || trimmed.size() > 60) return false;
    auto current = entries();
    if (!merge(trimmed, misspellings, current)) return false;
    save(current);
    return true;
}

void DictionaryStore::setMisspellings(const QUuid &id, const QStringList &forms)
{
    auto current = entries();
    for (auto &e : current) {
        if (e.id == id) {
            e.misspellings = DictionaryEntry::normalizedForms(forms, e.term);
            save(current);
            return;
        }
    }
}

void DictionaryStore::remove(const QUuid &id)
{
    auto current = entries();
    current.erase(std::remove_if(current.begin(), current.end(), [&](const auto &e) { return e.id == id; }), current.end());
    save(current);
}

void DictionaryStore::toggleStar(const QUuid &id)
{
    auto current = entries();
    for (auto &e : current) {
        if (e.id == id) {
            e.starred = !e.starred;
            save(current);
            return;
        }
    }
}

QList<DictionaryEntry> DictionaryStore::sortedByPriority() const
{
    auto list = entries();
    std::stable_sort(list.begin(), list.end(), [](const auto &a, const auto &b) {
        if (a.starred != b.starred) return a.starred;
        return a.createdAt > b.createdAt;
    });
    return list;
}

QStringList DictionaryStore::vocabulary() const
{
    QStringList out;
    for (const auto &e : sortedByPriority()) {
        if (out.size() >= 100) break;
        out << e.term;
    }
    return out;
}

QStringList DictionaryStore::sanitizedVocabulary(int maxBytes) const
{
    int used = 0;
    QStringList out;
    for (QString term : vocabulary()) {
        term.replace('\n', ' ').replace('\r', ' ');
        const QString clean = term.left(60).trimmed();
        if (clean.isEmpty()) continue;
        const int cost = int(clean.toUtf8().size()) + 1;
        if (used + cost > maxBytes) break; // truncate from the END: starred survive
        used += cost;
        out << clean;
    }
    return out;
}

QList<QPair<QString, QString>> DictionaryStore::spellings() const
{
    QList<QPair<QString, QString>> out;
    for (const auto &e : sortedByPriority())
        for (const auto &m : e.misspellings) {
            if (out.size() >= 10) return out;
            out << qMakePair(m, e.term);
        }
    return out;
}

QList<ReplacementEngine::Rule> DictionaryStore::replacementRules() const
{
    QList<ReplacementEngine::Rule> rules;
    for (const auto &e : entries())
        for (const auto &m : e.misspellings) rules << ReplacementEngine::Rule{m, e.term};
    return rules;
}

QString DictionaryStore::exportCSV() const
{
    QStringList lines{"term,misspelling"};
    for (const auto &e : entries()) {
        QString term = e.term;
        term.replace("\"", "\"\"");
        const QStringList forms = e.misspellings.isEmpty() ? QStringList{QString()} : e.misspellings;
        for (QString form : forms) {
            form.replace("\"", "\"\"");
            lines << QString("\"%1\",\"%2\"").arg(term, form);
        }
    }
    return lines.join('\n');
}

static QStringList splitRecords(const QString &csv)
{
    QStringList records;
    QString current;
    bool inQuotes = false;
    for (QChar c : csv) {
        if (c == '"') { inQuotes = !inQuotes; current += c; }
        else if (!inQuotes && (c == '\n' || c == '\r')) {
            if (!current.isEmpty()) records << current;
            current.clear();
        } else current += c;
    }
    if (!current.isEmpty()) records << current;
    return records;
}

static QStringList parseCSVLine(const QString &line)
{
    QStringList columns;
    QString current;
    bool inQuotes = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line[i];
        if (c == '"' && inQuotes && i + 1 < line.size() && line[i + 1] == '"') { current += '"'; ++i; }
        else if (c == '"') inQuotes = !inQuotes;
        else if (c == ',' && !inQuotes) { columns << current; current.clear(); }
        else current += c;
    }
    columns << current;
    for (auto &col : columns) col = col.trimmed();
    return columns;
}

int DictionaryStore::importCSV(const QString &csv)
{
    QStringList lines = splitRecords(csv);
    if (!lines.isEmpty()) {
        const QString first = parseCSVLine(lines.first()).value(0).toLower();
        if (first == "term" || first == "word" || first == "phrase") lines.removeFirst();
    }
    auto current = entries();
    int imported = 0;
    for (const QString &line : lines) {
        if (imported >= 1000) break;
        const QStringList cols = parseCSVLine(line);
        if (cols.isEmpty()) continue;
        const QString term = cols.first().trimmed();
        if (term.size() < 1 || term.size() > 60) continue;
        const QStringList forms = cols.size() > 1 ? QStringList{cols[1]} : QStringList{};
        if (merge(term, forms, current)) ++imported;
    }
    if (imported > 0) save(current);
    return imported;
}

} // namespace jot
