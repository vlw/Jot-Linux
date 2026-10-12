// Jot for Linux — a Qt 6 port of Jot (Apache-2.0, Copyright 2026 Google LLC).
// Hold a key. Speak. It types.
#include "app/DictationController.h"
#include "app/DictationCoordinator.h"
#include "app/Earcons.h"
#include "app/Language.h"
#include "audio/AudioCapture.h"
#include "core/FileLayout.h"
#include "core/SecretStore.h"
#include "core/Settings.h"
#include "platform/ActiveWindow.h"
#include "platform/HotkeyEngine.h"
#include "platform/WaylandIntegration.h"
#include "ui/Backend.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLoggingCategory>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QUrl>

Q_LOGGING_CATEGORY(lcApp, "jot.app")

namespace jot {

/// D-Bus endpoint for jot:// routes and single-instance hand-off.
class AppRouter : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.ammaar.jot.App")
public:
    AppRouter(DictationController *controller, Backend *backend)
        : m_controller(controller), m_backend(backend) {}

public slots:
    Q_SCRIPTABLE void openUrl(const QString &raw)
    {
        const QUrl url(raw);
        const QString command = url.host().isEmpty() ? url.path().remove('/') : url.host();
        const QStringList parts = url.path().split('/', Qt::SkipEmptyParts);
        const QString section = parts.value(0);
        if (command == "settings") {
            static const QStringList known = {"general", "dictation", "privacy", "advanced", "history", "dictionary", "about"};
            m_backend->openMain(known.contains(section) ? section : "general");
        } else if (command == "history" || command == "dictionary") {
            m_backend->openMain(command);
        } else if (command == "onboarding") {
            bool ok = false;
            const int n = section.toInt(&ok);
            m_backend->presentOnboarding(ok ? n : -1);
        } else if (command == "start-hands-free") {
            m_controller->startHandsFree();
        } else if (command == "debug-pill" && !qEnvironmentVariableIsEmpty("JOT_SNAPSHOT_DIR")) {
            m_controller->debugPill(section, qEnvironmentVariable("JOT_SNAPSHOT_DIR") + "/pill-" + section + ".png");
        } else if (command == "snapshot") {
            m_backend->snapshot(section.isEmpty() ? QStringLiteral("window") : section);
        } else if (command == "stop") {
            m_controller->stopDictation();
        } else if (command.isEmpty() || command == "open") {
            m_backend->openMain("history");
        } else {
            qCWarning(lcApp) << "unknown jot:// route" << raw;
        }
    }

private:
    DictationController *m_controller;
    Backend *m_backend;
};

} // namespace jot

int main(int argc, char *argv[])
{
    // The layer-shell integration plugin is shipped next to the binary.
    const QString exeDir = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath();
    QCoreApplication::addLibraryPath(exeDir + "/plugins");
    QCoreApplication::addLibraryPath(QStringLiteral(JOT_PLUGIN_DIR));
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "wayland");
    QQuickStyle::setStyle("Basic");
    QQuickWindow::setDefaultAlphaBuffer(true);
    // FreeType-hinted text like the rest of Plasma; Qt Quick's default distance-field
    // rendering looks soft and uneven at 1:1 scale.
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);

    QApplication app(argc, argv);
    QApplication::setApplicationName("Jot");
    QApplication::setOrganizationName("jot-linux");
    QApplication::setApplicationVersion(QStringLiteral(JOT_VERSION));
    QApplication::setDesktopFileName("jot-linux");
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setWindowIcon(QIcon(":/icons/jot.png"));

    const QStringList args = app.arguments().mid(1);
    if (args.contains("--list-microphones")) {
        for (const auto &d : jot::MicrophoneManager::instance().inputs())
            printf("%s\t%s\n", d.id().constData(), qPrintable(d.description()));
        return 0;
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    // Single instance: if another Jot already owns the name, hand over and quit.
    if (bus.interface()->isServiceRegistered("com.ammaar.jot.linux")) {
        qCInfo(lcApp) << "another Jot instance is already running — quitting this one";
        QDBusInterface other("com.ammaar.jot.linux", "/App", "com.ammaar.jot.App", bus);
        other.call("openUrl", args.isEmpty() ? QStringLiteral("jot://history") : args.first());
        return 0;
    }

    jot::Language::instance().apply();

    for (const QString &font : {":/fonts/GoogleSansFlex-Regular.ttf", ":/fonts/GoogleSansCode-VF.ttf"})
        if (QFontDatabase::addApplicationFont(font) < 0) qCWarning(lcApp) << "font failed to load" << font;

    jot::FileLayout::ensureDir(jot::FileLayout::recordingsRoot());
    jot::WaylandIntegration::instance().init();
    jot::ActiveWindow::instance().start();
    jot::MicrophoneManager::instance().ensureInitialSelection();
    jot::Earcons::instance().preload();
    jot::SecretStore::instance().geminiKey(); // unlock the wallet once, up front

    auto *controller = new jot::DictationController(&app);
    auto *backend = new jot::Backend(controller, &app);
    auto *router = new jot::AppRouter(controller, backend);
    bus.registerObject("/App", router, QDBusConnection::ExportScriptableSlots);

    QObject::connect(&app, &QCoreApplication::aboutToQuit, controller, &jot::DictationController::prepareForTermination);
    controller->start();
    for (const QString &a : args)
        if (a.startsWith("jot://")) router->openUrl(a);

    return app.exec();
}

#include "main.moc"
