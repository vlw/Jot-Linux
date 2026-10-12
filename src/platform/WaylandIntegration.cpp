// Wayland plumbing for text insertion on KDE Plasma (see header).
#include "WaylandIntegration.h"

#include "ext-data-control-v1-client-protocol.h"
#include "fake-input-client-protocol.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QThread>
#include <wayland-client.h>

#include <cerrno>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(lcWayland, "jot.wayland")

namespace jot {

namespace {

void registryGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    static_cast<WaylandIntegration *>(data)->onGlobal(registry, name, interface, version);
}
void registryRemove(void *, wl_registry *, uint32_t) {}
const wl_registry_listener kRegistryListener = {registryGlobal, registryRemove};

void sourceSend(void *data, ext_data_control_source_v1 *source, const char *mime, int32_t fd)
{
    static_cast<WaylandIntegration *>(data)->onSourceSend(source, mime, fd);
}
void sourceCancelled(void *data, ext_data_control_source_v1 *source)
{
    static_cast<WaylandIntegration *>(data)->onSourceCancelled(source);
}
const ext_data_control_source_v1_listener kSourceListener = {sourceSend, sourceCancelled};

// The device's offers are not needed — Jot only ever writes the clipboard — but
// every event needs a handler.
void deviceDataOffer(void *, ext_data_control_device_v1 *, ext_data_control_offer_v1 *offer)
{
    ext_data_control_offer_v1_destroy(offer);
}
void deviceSelection(void *, ext_data_control_device_v1 *, ext_data_control_offer_v1 *) {}
void deviceFinished(void *, ext_data_control_device_v1 *device) { ext_data_control_device_v1_destroy(device); }
void devicePrimary(void *, ext_data_control_device_v1 *, ext_data_control_offer_v1 *) {}
const ext_data_control_device_v1_listener kDeviceListener = {deviceDataOffer, deviceSelection, deviceFinished, devicePrimary};

} // namespace

WaylandIntegration &WaylandIntegration::instance()
{
    static WaylandIntegration w;
    return w;
}

bool WaylandIntegration::init()
{
    if (m_initialized) return m_display != nullptr;
    m_initialized = true;
    auto *native = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!native || !native->display()) {
        qCWarning(lcWayland) << "not running on Wayland";
        return false;
    }
    m_display = native->display();
    m_queue = wl_display_create_queue(m_display);
    auto *wrapper = static_cast<wl_display *>(wl_proxy_create_wrapper(m_display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(wrapper), m_queue);
    m_registry = wl_display_get_registry(wrapper);
    wl_proxy_wrapper_destroy(wrapper);
    wl_registry_add_listener(m_registry, &kRegistryListener, this);
    wl_display_roundtrip_queue(m_display, m_queue);

    if (m_dataControl && m_seat) {
        m_device = ext_data_control_manager_v1_get_data_device(m_dataControl, m_seat);
        ext_data_control_device_v1_add_listener(m_device, &kDeviceListener, this);
    }
    if (m_fakeInput) {
        org_kde_kwin_fake_input_authenticate(m_fakeInput, "Jot", "Insert dictated text at the cursor");
    }
    wl_display_roundtrip_queue(m_display, m_queue);
    qCInfo(lcWayland) << "clipboard control:" << hasClipboardControl() << "fake input:" << hasFakeInput();

    // Events on the private queue are read by Qt's reader thread; dispatch them here.
    m_pump.setInterval(25);
    connect(&m_pump, &QTimer::timeout, this, &WaylandIntegration::dispatch);
    m_pump.start();
    return true;
}

void WaylandIntegration::dispatch()
{
    if (!m_display || !m_queue) return;
    // Pull any events Qt's thread has not read yet without blocking.
    if (wl_display_prepare_read_queue(m_display, m_queue) == 0) {
        wl_display_flush(m_display);
        wl_display_cancel_read(m_display);
    }
    wl_display_dispatch_queue_pending(m_display, m_queue);
}

void WaylandIntegration::onGlobal(wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    const QByteArray iface(interface);
    if (iface == ext_data_control_manager_v1_interface.name) {
        m_dataControl = static_cast<ext_data_control_manager_v1 *>(
            wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, std::min<uint32_t>(version, 1)));
    } else if (iface == wl_seat_interface.name && !m_seat) {
        m_seat = static_cast<wl_seat *>(wl_registry_bind(registry, name, &wl_seat_interface, std::min<uint32_t>(version, 5)));
    } else if (iface == org_kde_kwin_fake_input_interface.name) {
        if (version < 4) {
            qCWarning(lcWayland) << "fake input too old for keyboard events:" << version;
            return;
        }
        m_fakeInput = static_cast<org_kde_kwin_fake_input *>(
            wl_registry_bind(registry, name, &org_kde_kwin_fake_input_interface, std::min<uint32_t>(version, 5)));
    }
}

void WaylandIntegration::onSourceSend(ext_data_control_source_v1 *source, const char *, int fd)
{
    if (source == m_source) {
        const char *p = m_clipboardBytes.constData();
        qsizetype left = m_clipboardBytes.size();
        // Bounded write: a reader that stalls must never hang Jot's main thread.
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        int waitedMs = 0;
        while (left > 0 && waitedMs < 1000) {
            const ssize_t n = ::write(fd, p, size_t(left));
            if (n > 0) { p += n; left -= n; continue; }
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && errno != EAGAIN) break;
            pollfd pfd{fd, POLLOUT, 0};
            ::poll(&pfd, 1, 50);
            waitedMs += 50;
        }
    }
    ::close(fd);
}

void WaylandIntegration::onSourceCancelled(ext_data_control_source_v1 *source)
{
    if (source == m_source) m_source = nullptr;
    ext_data_control_source_v1_destroy(source);
}

bool WaylandIntegration::setClipboard(const QString &text)
{
    if (!hasClipboardControl()) return false;
    if (m_source) {
        ext_data_control_source_v1_destroy(m_source);
        m_source = nullptr;
    }
    m_clipboardBytes = text.toUtf8();
    m_source = ext_data_control_manager_v1_create_data_source(m_dataControl);
    ext_data_control_source_v1_add_listener(m_source, &kSourceListener, this);
    for (const char *mime : {"text/plain;charset=utf-8", "text/plain", "UTF8_STRING", "STRING", "TEXT"})
        ext_data_control_source_v1_offer(m_source, mime);
    ext_data_control_device_v1_set_selection(m_device, m_source);
    // Flush only: requests on one connection are ordered, so the paste chord sent
    // afterwards cannot overtake the selection. A roundtrip here could wait on readers.
    wl_display_flush(m_display);
    return true;
}

bool WaylandIntegration::sendChord(const QList<int> &keys)
{
    if (!m_fakeInput) return false;
    for (int k : keys) org_kde_kwin_fake_input_keyboard_key(m_fakeInput, uint32_t(k), 1);
    wl_display_flush(m_display);
    QThread::msleep(12);
    for (auto it = keys.rbegin(); it != keys.rend(); ++it) org_kde_kwin_fake_input_keyboard_key(m_fakeInput, uint32_t(*it), 0);
    wl_display_flush(m_display);
    return true;
}

} // namespace jot
