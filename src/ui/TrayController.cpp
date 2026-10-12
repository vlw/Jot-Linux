// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) — StatusItemController.
#include "TrayController.h"
#include "../audio/AudioCapture.h"

#include <QCoreApplication>
#include <QActionGroup>
#include <QGuiApplication>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyleHints>
#include <QSystemTrayIcon>

namespace jot {

static const QVector<QVector<double>> kListening = {{3, 6.5, 4}, {5, 4, 6}, {6.5, 5.5, 3.5}, {4, 7, 5}, {3.5, 5, 6.5}};
static const QVector<QVector<double>> kProcessing = {{6, 4, 4}, {4, 6, 4}, {4, 4, 6}, {4, 6, 4}};
static const QVector<double> kIdle = {3.5, 6, 3.5};

TrayController::TrayController(QObject *parent)
    : QObject(parent)
    , m_tray(new QSystemTrayIcon(this))
    , m_menu(new QMenu)
{
    m_status = m_menu->addAction(QCoreApplication::translate("Jot", "Starting up…"));
    m_status->setEnabled(false);
    m_menu->addSeparator();
    m_handsFree = m_menu->addAction(QString());
    connect(m_handsFree, &QAction::triggered, this, &TrayController::startHandsFree);
    m_paste = m_menu->addAction(QString());
    connect(m_paste, &QAction::triggered, this, &TrayController::pasteLastTranscript);
    m_history = m_menu->addAction(QString());
    connect(m_history, &QAction::triggered, this, &TrayController::openHistory);
    m_menu->addSeparator();
    m_micMenu = m_menu->addMenu(QString());
    connect(m_micMenu, &QMenu::aboutToShow, this, &TrayController::rebuildMicrophoneMenu);
    rebuildMicrophoneMenu();
    m_settings = m_menu->addAction(QString());
    m_settings->setShortcut(QKeySequence("Ctrl+,"));
    connect(m_settings, &QAction::triggered, this, &TrayController::openSettings);
    m_menu->addSeparator();
    m_about = m_menu->addAction(QString());
    connect(m_about, &QAction::triggered, this, &TrayController::openAbout);
    m_quit = m_menu->addAction(QString());
    m_quit->setShortcut(QKeySequence("Ctrl+Q"));
    connect(m_quit, &QAction::triggered, this, &TrayController::quit);

    m_tray->setContextMenu(m_menu);
    m_tray->setToolTip("Jot");
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        // Wayland gives no cursor position for a popup; left click opens History,
        // right click shows the menu (the macOS menu-bar item's contents).
        if (r == QSystemTrayIcon::Trigger) emit openHistory();
    });
    connect(&m_anim, &QTimer::timeout, this, [this] { ++m_frame; render(); });
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &TrayController::render);
    retranslate();
    render();
    m_tray->show();
}

void TrayController::setStatusLine(const QString &text) { m_status->setText(text); }

void TrayController::retranslate()
{
    m_handsFree->setText(QCoreApplication::translate("Jot", "Start Hands-Free Dictation"));
    m_paste->setText(QCoreApplication::translate("Jot", "Paste Last Transcript"));
    m_history->setText(QCoreApplication::translate("Jot", "History…"));
    m_micMenu->setTitle(QCoreApplication::translate("Jot", "Microphone"));
    m_settings->setText(QCoreApplication::translate("Jot", "Settings…"));
    m_about->setText(QCoreApplication::translate("Jot", "About Jot"));
    m_quit->setText(QCoreApplication::translate("Jot", "Quit Jot"));
    rebuildMicrophoneMenu();
}

void TrayController::setVisual(Visual v)
{
    if (v == m_visual) return;
    m_visual = v;
    m_anim.stop();
    m_frame = 0;
    if (v == Visual::Listening) m_anim.start(120);
    else if (v == Visual::Processing) m_anim.start(250);
    if (m_anim.isActive()) ++m_frame; // first shown frame = index 1
    render();
}

void TrayController::render()
{
    const QColor ink = QGuiApplication::palette().color(QPalette::WindowText);
    QIcon icon;
    for (int scale : {1, 2, 3}) {
        const int px = 22 * scale;
        QPixmap pm(px, px);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        // 18×18 artwork centred in a 22×22 tray cell.
        p.scale(scale, scale);
        p.translate(2, 2);
        p.setOpacity(m_visual == Visual::Attention ? 0.4 : 1.0);
        QPen pen(ink, 1.5);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(1, 3.5, 16, 11), 5.5, 5.5);
        QVector<double> bars = kIdle;
        if (m_visual == Visual::Listening) bars = kListening[m_frame % kListening.size()];
        else if (m_visual == Visual::Processing) bars = kProcessing[m_frame % kProcessing.size()];
        p.setPen(Qt::NoPen);
        p.setBrush(ink);
        const double xs[3] = {5.1, 8.1, 11.1};
        for (int i = 0; i < 3; ++i) p.drawRoundedRect(QRectF(xs[i], 9 - bars[i] / 2, 1.8, bars[i]), 0.9, 0.9);
        p.end();
        icon.addPixmap(pm);
    }
    m_tray->setIcon(icon);
}

void TrayController::rebuildMicrophoneMenu()
{
    m_micMenu->clear();
    auto &mics = MicrophoneManager::instance();
    auto *group = new QActionGroup(m_micMenu);
    group->setExclusive(true);
    const auto current = mics.recordingDevice();
    const bool followsSystem = mics.followsSystemDefault();
    for (const QAudioDevice &d : mics.inputs()) {
        auto *a = m_micMenu->addAction(d.description());
        a->setCheckable(true);
        a->setChecked(!followsSystem && current && current->id() == d.id());
        group->addAction(a);
        connect(a, &QAction::triggered, this, [d] { MicrophoneManager::instance().select(d); });
    }
    m_micMenu->addSeparator();
    auto *sys = m_micMenu->addAction(QCoreApplication::translate("Jot", "Follow the system default input"));
    sys->setCheckable(true);
    sys->setChecked(followsSystem);
    group->addAction(sys);
    connect(sys, &QAction::triggered, this, [] { MicrophoneManager::instance().selectSystemDefault(); });
    m_micMenu->addSeparator();
    QString note;
    if (followsSystem) note = QCoreApplication::translate("Jot", "Switches when the system default input changes");
    else if (current) note = QCoreApplication::translate("Jot", "Always uses this microphone");
    else note = QCoreApplication::translate("Jot", "%1 unavailable — reconnect it").arg(mics.selectedDisplayName());
    m_micMenu->addAction(note)->setEnabled(false);
}

} // namespace jot
