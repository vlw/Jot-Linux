// Port of Jot (Apache-2.0, Copyright 2026 Google LLC) — PillModel + PillHUDController.
#include "PillHUD.h"
#include "../platform/ActiveWindow.h"
#include "../app/Language.h"
#include "IconProvider.h"

#include <LayerShellQt/Window>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QRegion>
#include <QScreen>
#include <QTimer>

namespace jot {

QString PillModel::state() const
{
    switch (m_state) {
    case State::Hidden: return "hidden";
    case State::IdleDot: return "idleDot";
    case State::Listening: return "listening";
    case State::Processing: return "processing";
    case State::Success: return "success";
    case State::Notice: return "notice";
    case State::Error: return "error";
    }
    return "hidden";
}

void PillModel::set(State s)
{
    m_state = s;
    ++m_serial;
    emit changed();
}

void PillModel::setIdle(bool show)
{
    m_slow = false;
    set(show ? State::IdleDot : State::Hidden);
}

void PillModel::setListening(bool locked)
{
    m_locked = locked;
    m_slow = false;
    set(State::Listening);
}

void PillModel::setProcessing() { set(State::Processing); }

void PillModel::setSuccess(int words)
{
    m_words = words;
    m_slow = false;
    set(State::Success);
}

void PillModel::setNotice(const QString &m)
{
    m_message = m;
    m_slow = false;
    set(State::Notice);
}

void PillModel::setError(const QString &m)
{
    m_message = m;
    m_slow = false;
    set(State::Error);
}

PillHUD::PillHUD(PillModel *model, QObject *parent)
    : QObject(parent)
    , m_model(model)
{
    m_view = new QQuickView;
    m_view->setColor(Qt::transparent);
    m_view->setFlags(Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus | Qt::WindowStaysOnTopHint);
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    m_view->resize(600, 96);
    m_view->setTitle("Jot");

    if (auto *layer = LayerShellQt::Window::get(m_view)) {
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        layer->setAnchors(LayerShellQt::Window::AnchorBottom);
        layer->setExclusiveZone(0); // respect panels: sit above the work area
        // Panel bottom sits 16 px above the work area; the 48-px pill keeps 8 px of padding below.
        layer->setMargins(QMargins(0, 0, 0, 16));
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layer->setScope("jot-pill");
        layer->setWantsToBeOnActiveScreen(false);
    }
    m_view->rootContext()->setContextProperty("pill", m_model);
    m_view->rootContext()->setContextProperty("fonts", &Language::instance());
    m_view->engine()->addImageProvider("icon", new IconProvider);
    m_view->setSource(QUrl("qrc:/qml/Pill.qml"));
    connect(m_model, &PillModel::interactiveRectChanged, this, &PillHUD::applyMask);
    connect(m_model, &PillModel::changed, this, &PillHUD::syncVisibility);
    applyMask(QRect());
}

void PillHUD::applyMask(const QRect &rect)
{
    // Input region = the visible pill only; everything else is click-through.
    if (rect.isEmpty()) m_view->setMask(QRegion(0, 0, 1, 1));
    else m_view->setMask(QRegion(rect.adjusted(-6, -6, 6, 6)));
}

void PillHUD::repositionToActiveScreen()
{
    const QString output = ActiveWindow::instance().outputName();
    QScreen *target = nullptr;
    for (QScreen *s : QGuiApplication::screens())
        if (s->name() == output) target = s;
    if (!target) target = QGuiApplication::primaryScreen();
    if (target && m_view->screen() != target) {
        const bool visible = m_view->isVisible();
        if (visible) m_view->hide();
        m_view->setScreen(target);
        if (visible) m_view->show();
    }
}

void PillHUD::show()
{
    m_active = true;
    syncVisibility();
}

void PillHUD::syncVisibility()
{
    if (!m_active) return;
    if (m_model->stateEnum() != PillModel::State::Hidden) {
        if (!m_view->isVisible()) {
            repositionToActiveScreen();
            m_view->show();
        }
        return;
    }
    // Nothing to draw: remove the overlay surface entirely once the fade-out finishes.
    QTimer::singleShot(250, m_view, [this] {
        if (m_model->stateEnum() == PillModel::State::Hidden) m_view->hide();
    });
}

} // namespace jot

namespace jot {
void PillHUD::retranslate() { m_view->engine()->retranslate(); }

void PillHUD::grab(const QString &path)
{
    m_view->show();
    QTimer::singleShot(900, m_view, [this, path] { m_view->grabWindow().save(path); });
}
} // namespace jot
