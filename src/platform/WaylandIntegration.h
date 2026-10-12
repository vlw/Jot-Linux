// Wayland plumbing for text insertion on KDE Plasma:
//  * ext_data_control_v1  — set the clipboard without having keyboard focus
//                           (QClipboard silently fails for unfocused clients).
//  * org_kde_kwin_fake_input — synthesize the paste chord. KWin only exposes it to
//                           clients whose .desktop file lists it in
//                           X-KDE-Wayland-Interfaces (scripts/install.sh does this).
//
// Runs on a private wl_event_queue on Qt's own wl_display so it never races
// Qt's dispatching.
#pragma once

#include <QObject>
#include <QTimer>

struct wl_display;
struct wl_event_queue;
struct wl_registry;
struct wl_seat;
struct ext_data_control_manager_v1;
struct ext_data_control_device_v1;
struct ext_data_control_source_v1;
struct org_kde_kwin_fake_input;

namespace jot {

class WaylandIntegration : public QObject {
    Q_OBJECT
public:
    static WaylandIntegration &instance();
    bool init();

    bool hasClipboardControl() const { return m_dataControl && m_seat; }
    bool hasFakeInput() const { return m_fakeInput != nullptr; }

    /// Puts text on the regular clipboard. Returns false when no path worked.
    bool setClipboard(const QString &text);
    /// Sends a key chord (evdev codes), e.g. {KEY_LEFTCTRL, KEY_V}.
    bool sendChord(const QList<int> &keys);

    // wayland callbacks
    void onGlobal(wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
    void onSourceSend(ext_data_control_source_v1 *source, const char *mime, int fd);
    void onSourceCancelled(ext_data_control_source_v1 *source);

private:
    WaylandIntegration() = default;
    void dispatch();

    wl_display *m_display = nullptr;
    wl_event_queue *m_queue = nullptr;
    wl_registry *m_registry = nullptr;
    wl_seat *m_seat = nullptr;
    ext_data_control_manager_v1 *m_dataControl = nullptr;
    ext_data_control_device_v1 *m_device = nullptr;
    ext_data_control_source_v1 *m_source = nullptr;
    org_kde_kwin_fake_input *m_fakeInput = nullptr;
    QByteArray m_clipboardBytes;
    QTimer m_pump;
    bool m_initialized = false;
};

} // namespace jot
