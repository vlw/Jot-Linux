// Frontmost-app tracking on KWin/Wayland (NSWorkspace.frontmostApplication
// analogue). A tiny KWin script reports every activation over D-Bus.
#pragma once

#include <QObject>

namespace jot {

class ActiveWindow : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.ammaar.jot.ActiveWindow")
public:
    static ActiveWindow &instance();
    bool start();
    void stop();

    QString resourceClass() const { return m_class; }
    QString caption() const { return m_caption; }
    qint64 pid() const { return m_pid; }
    QString windowId() const { return m_id; }
    QString outputName() const { return m_output; }
    bool isTracking() const { return m_tracking; }
    /// Human-friendly app name for History ("Konsole", "Firefox").
    QString appName() const;

public slots:
    Q_SCRIPTABLE void update(const QString &resourceClass, const QString &caption, int pid, const QString &windowId,
                             const QString &outputName);

signals:
    void changed();

private:
    ActiveWindow() = default;
    QString m_class, m_caption, m_id, m_output;
    qint64 m_pid = 0;
    bool m_tracking = false;
    int m_scriptId = -1;
};

} // namespace jot
