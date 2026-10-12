// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) — StatusItemController.
#pragma once

#include <QObject>
#include <QTimer>
#include <QVector>

class QSystemTrayIcon;
class QMenu;
class QAction;

namespace jot {

class TrayController : public QObject {
    Q_OBJECT
public:
    enum class Visual { Idle, Attention, Listening, Processing };
    explicit TrayController(QObject *parent = nullptr);
    void setVisual(Visual v);
    void setStatusLine(const QString &text);
    void retranslate();

signals:
    void startHandsFree();
    void pasteLastTranscript();
    void openHistory();
    void openSettings();
    void openAbout();
    void quit();

private:
    void render();
    void rebuildMicrophoneMenu();
    QSystemTrayIcon *m_tray;
    QMenu *m_menu;
    QMenu *m_micMenu;
    QAction *m_status;
    QAction *m_handsFree, *m_paste, *m_history, *m_settings, *m_about, *m_quit;
    Visual m_visual = Visual::Idle;
    QTimer m_anim;
    int m_frame = 0;
};

} // namespace jot
