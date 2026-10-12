// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) to Linux/Qt.
#pragma once

#include "TextPipeline.h"
#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QUuid>

namespace jot {

struct DictionaryEntry {
    QUuid id;
    QString term;
    QStringList misspellings;
    bool starred = false;
    QDateTime createdAt;

    static QStringList normalizedForms(const QStringList &forms, const QString &term);
};

/// The personal dictionary: terms (sent to the recognizer as vocabulary) and
/// explicit wrong→right rules (enforced deterministically post-model).
/// Stored as JSON in ~/.local/share/Jot/dictionary.json.
class DictionaryStore : public QObject {
    Q_OBJECT
public:
    static DictionaryStore &instance();

    QList<DictionaryEntry> entries() const;
    void save(const QList<DictionaryEntry> &entries);

    bool add(const QString &term, const QStringList &misspellings = {});
    void setMisspellings(const QUuid &id, const QStringList &forms);
    void remove(const QUuid &id);
    void toggleStar(const QUuid &id);

    QStringList vocabulary() const;
    QStringList sanitizedVocabulary(int maxBytes = 2048) const;
    QList<QPair<QString, QString>> spellings() const;
    QList<ReplacementEngine::Rule> replacementRules() const;

    QString exportCSV() const;
    int importCSV(const QString &csv);

signals:
    void changed();

private:
    DictionaryStore() = default;
    static bool merge(const QString &term, const QStringList &forms, QList<DictionaryEntry> &entries);
    QList<DictionaryEntry> sortedByPriority() const;
    mutable bool m_loaded = false;
    mutable QList<DictionaryEntry> m_cache;
};

} // namespace jot
