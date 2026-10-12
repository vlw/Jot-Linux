// Frontmost-app tracking on KWin/Wayland.
#include "ActiveWindow.h"
#include "../core/FileLayout.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QFile>
#include <QLoggingCategory>
#include <QSettings>
#include <QDir>
#include <QGuiApplication>

Q_LOGGING_CATEGORY(lcActive, "jot.activewindow")

namespace jot {

static const char *kBusName = "com.ammaar.jot.linux";
static const char *kPluginName = "jot-active-window";

ActiveWindow &ActiveWindow::instance()
{
    static ActiveWindow a;
    return a;
}

bool ActiveWindow::start()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.registerService(kBusName);
    bus.registerObject("/ActiveWindow", this, QDBusConnection::ExportScriptableSlots);
    if (QGuiApplication::platformName() == "offscreen") return false; // headless UI review

    const QString script = QStringLiteral(R"JS(
function jotReport(w) {
    if (!w) { callDBus("%1", "/ActiveWindow", "com.ammaar.jot.ActiveWindow", "update", "", "", 0, "", ""); return; }
    callDBus("%1", "/ActiveWindow", "com.ammaar.jot.ActiveWindow", "update",
             String(w.resourceClass || ""), String(w.caption || ""), Number(w.pid || 0), String(w.internalId || ""),
             String(w.output ? w.output.name : ""));
}
workspace.windowActivated.connect(jotReport);
jotReport(workspace.activeWindow);
)JS").arg(kBusName);

    const QString dir = FileLayout::appDataRoot();
    FileLayout::ensureDir(dir);
    const QString path = dir + "/kwin-active-window.js";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(script.toUtf8());
    f.close();

    QDBusInterface scripting("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", bus);
    if (!scripting.isValid()) {
        qCWarning(lcActive) << "KWin scripting unavailable — focus checks disabled";
        return false;
    }
    scripting.call("unloadScript", QString(kPluginName));
    QDBusReply<int> id = scripting.call("loadScript", path, QString(kPluginName));
    if (!id.isValid() || id.value() < 0) {
        qCWarning(lcActive) << "loadScript failed" << id.error().message();
        return false;
    }
    m_scriptId = id.value();
    QDBusInterface s("org.kde.KWin", QString("/Scripting/Script%1").arg(m_scriptId), "org.kde.kwin.Script", bus);
    s.call("run");
    m_tracking = true;
    return true;
}

void ActiveWindow::stop()
{
    if (!m_tracking) return;
    QDBusInterface scripting("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", QDBusConnection::sessionBus());
    scripting.call("unloadScript", QString(kPluginName));
    m_tracking = false;
}

void ActiveWindow::update(const QString &resourceClass, const QString &caption, int pid, const QString &windowId,
                          const QString &outputName)
{
    m_output = outputName;
    m_class = resourceClass;
    m_caption = caption;
    m_pid = pid;
    m_id = windowId;
    emit changed();
}

QString ActiveWindow::appName() const
{
    if (m_class.isEmpty()) return {};
    // Prefer the .desktop Name for this app id.
    static QHash<QString, QString> cache;
    if (cache.contains(m_class)) return cache.value(m_class);
    QString name;
    const QStringList dirs = {QDir::homePath() + "/.local/share/applications", "/usr/share/applications",
                              "/var/lib/flatpak/exports/share/applications", "/var/lib/snapd/desktop/applications"};
    for (const QString &d : dirs) {
        const QString file = d + "/" + m_class + ".desktop";
        if (QFile::exists(file)) {
            QSettings desk(file, QSettings::IniFormat);
            name = desk.value("Desktop Entry/Name").toString();
            if (!name.isEmpty()) break;
        }
    }
    if (name.isEmpty()) {
        name = m_class.section('.', -1);
        if (!name.isEmpty()) name[0] = name[0].toUpper();
    }
    cache.insert(m_class, name);
    return name;
}

} // namespace jot
