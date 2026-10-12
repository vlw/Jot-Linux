// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) — PillModel + PillHUDController.
// The pill is a wlr-layer-shell overlay surface: always on top, bottom-centred
// on the active window's output, and it never takes keyboard focus, so the app
// the user is dictating into keeps it.
#pragma once

#include <QObject>
#include <QRect>

class QQuickView;

namespace jot {

class PillModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(bool locked READ locked NOTIFY changed)
    Q_PROPERTY(float level READ level NOTIFY levelChanged)
    Q_PROPERTY(int elapsed READ elapsed NOTIFY elapsedChanged)
    Q_PROPERTY(bool slow READ slow NOTIFY changed)
    Q_PROPERTY(int words READ words NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(int serial READ serial NOTIFY changed)
public:
    enum class State { Hidden, IdleDot, Listening, Processing, Success, Notice, Error };
    explicit PillModel(QObject *parent = nullptr) : QObject(parent) {}

    QString state() const;
    State stateEnum() const { return m_state; }
    bool locked() const { return m_locked; }
    float level() const { return m_level; }
    int elapsed() const { return m_elapsed; }
    bool slow() const { return m_slow; }
    int words() const { return m_words; }
    QString message() const { return m_message; }
    int serial() const { return m_serial; }

    void setIdle(bool showIdleIndicator);
    void setListening(bool locked);
    void setProcessing();
    void setSuccess(int words);
    void setNotice(const QString &m);
    void setError(const QString &m);
    void setLevel(float l) { if (l != m_level) { m_level = l; emit levelChanged(); } }
    void setElapsed(int s) { if (s != m_elapsed) { m_elapsed = s; emit elapsedChanged(); } }
    void setSlow(bool s) { if (s != m_slow) { m_slow = s; ++m_serial; emit changed(); } }

    Q_INVOKABLE void stopTapped() { emit stopRequested(); }
    Q_INVOKABLE void dotTapped() { emit dotRequested(); }
    Q_INVOKABLE void cancelTapped() { emit cancelRequested(); }
    /// QML reports the visible pill rect so the input region can follow it.
    Q_INVOKABLE void setInteractiveRect(int x, int y, int w, int h) { emit interactiveRectChanged(QRect(x, y, w, h)); }

signals:
    void changed();
    void levelChanged();
    void elapsedChanged();
    void stopRequested();
    void dotRequested();
    void cancelRequested();
    void interactiveRectChanged(const QRect &rect);

private:
    void set(State s);
    State m_state = State::Hidden;
    bool m_locked = false;
    float m_level = 0;
    int m_elapsed = 0;
    bool m_slow = false;
    int m_words = -1;
    QString m_message;
    int m_serial = 0;
};

class PillHUD : public QObject {
    Q_OBJECT
public:
    explicit PillHUD(PillModel *model, QObject *parent = nullptr);
    void show();
    /// Move to the output that holds the frontmost window (never mid-session).
    void repositionToActiveScreen();
    void grab(const QString &path);
    void retranslate();

private:
    void applyMask(const QRect &rect);
    PillModel *m_model;
    QQuickView *m_view = nullptr;
    bool m_active = false; // show() was called (engine active)
    void syncVisibility();
};

} // namespace jot
