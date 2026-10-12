// Linux InsertionCoordinator.
#include "Inserter.h"
#include "ActiveWindow.h"
#include "WaylandIntegration.h"
#include "../core/Settings.h"
#include "../core/TextPipeline.h"

#include <QClipboard>
#include <QDBusInterface>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QTimer>
#include <linux/input-event-codes.h>

Q_LOGGING_CATEGORY(lcInsertion, "jot.insertion")

namespace jot {

Inserter &Inserter::instance()
{
    static Inserter i;
    return i;
}

bool Inserter::canPaste() const { return WaylandIntegration::instance().hasFakeInput(); }

bool Inserter::copyOnly(const QString &text)
{
    // A Jot window has focus (e.g. History → Copy): Qt serves its own selection
    // in-process. Going through data-control here would make Qt read our offer
    // synchronously on the main thread that must also write it — a deadlock.
    if (QGuiApplication::focusWindow()) {
        QGuiApplication::clipboard()->setText(text);
        return true;
    }
    if (WaylandIntegration::instance().setClipboard(text)) return true;
    // Klipper owns a data-control source of its own; it can set the clipboard unfocused.
    QDBusInterface klipper("org.kde.klipper", "/klipper", "org.kde.klipper.klipper");
    if (klipper.isValid()) {
        klipper.call("setClipboardContents", text);
        return true;
    }
    QGuiApplication::clipboard()->setText(text);
    return false;
}

void Inserter::insert(const QString &text, const DictationContext &context, std::function<void(InsertionOutcome)> cb)
{
    copyOnly(text);
    auto &active = ActiveWindow::instance();
    if (active.isTracking() && !context.targetWindowId.isEmpty() && active.windowId() != context.targetWindowId) {
        qCInfo(lcInsertion) << "focus changed (" << context.targetAppName << "→" << active.appName() << ") — no blind paste";
        cb(InsertionOutcome::FrontmostChanged);
        return;
    }
    if (!canPaste()) {
        cb(InsertionOutcome::FellBackToClipboard);
        return;
    }
    QString mode = Settings::instance().pasteShortcut();
    if (mode == "none") { cb(InsertionOutcome::FellBackToClipboard); return; }
    if (mode == "auto") mode = PromptV1::isTerminal(context.targetAppClass) ? "ctrlshiftv" : "ctrlv";
    QList<int> chord;
    if (mode == "ctrlshiftv") chord = {KEY_LEFTCTRL, KEY_LEFTSHIFT, KEY_V};
    else if (mode == "shiftinsert") chord = {KEY_LEFTSHIFT, KEY_INSERT};
    else chord = {KEY_LEFTCTRL, KEY_V};

    const QString expectedWindow = context.targetWindowId;
    // Let the compositor register the new selection before the target asks for it.
    QTimer::singleShot(40, this, [chord, expectedWindow, cb] {
        auto &active = ActiveWindow::instance();
        if (active.isTracking() && !expectedWindow.isEmpty() && active.windowId() != expectedWindow) {
            cb(InsertionOutcome::FrontmostChanged);
            return;
        }
        if (WaylandIntegration::instance().sendChord(chord)) {
            qCInfo(lcInsertion) << "paste chord posted (delivery is best-effort)";
            cb(InsertionOutcome::Inserted);
        } else {
            cb(InsertionOutcome::FellBackToClipboard);
        }
    });
}

} // namespace jot
