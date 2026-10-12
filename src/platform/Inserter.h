// Linux InsertionCoordinator: clipboard first (always), then a guarded paste.
#pragma once

#include "../core/Types.h"
#include <QObject>
#include <functional>

namespace jot {

class Inserter : public QObject {
    Q_OBJECT
public:
    static Inserter &instance();
    /// Every path leaves the text on the clipboard. Never blind-pastes into a
    /// window that took focus while the user was speaking.
    void insert(const QString &text, const DictationContext &context, std::function<void(InsertionOutcome)> cb);
    /// Copy only (Retry, recovered transcripts, focus changed).
    bool copyOnly(const QString &text);
    bool canPaste() const;

private:
    Inserter() = default;
};

} // namespace jot
