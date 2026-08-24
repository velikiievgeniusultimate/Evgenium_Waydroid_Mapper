#include "WaylandPointerConfiner.h"

#include <QGuiApplication>
#include <QVersionNumber>
#include <QWindow>

#include <pointer-constraints-unstable-v1-client-protocol.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#include <algorithm>
#include <cstdint>

class WaylandPointerConfiner::Impl
{
public:
    ~Impl() { release(); destroyManager(); }

    bool confine(QWindow *window, const QRect &region,
                 const StateCallback &stateCallback)
    {
        release();
        stateCallback_ = stateCallback;

        if (!window || region.isEmpty()) {
            report(false, QStringLiteral("invalid window or confinement region"));
            return false;
        }
        if (QGuiApplication::platformName() != QStringLiteral("wayland")) {
            report(false, QStringLiteral("host session is not Wayland"));
            return false;
        }

        // Since Qt 6.9 QWaylandWindow::winId() is the wl_surface pointer. Older
        // versions returned a synthetic integer, which must never be passed to
        // libwayland.
        if (QVersionNumber::fromString(QString::fromLatin1(qVersion()))
                < QVersionNumber(6, 9, 0)) {
            report(false, QStringLiteral("native confinement requires Qt 6.9 or newer"));
            return false;
        }

        auto *wayland = qGuiApp
            ->nativeInterface<QNativeInterface::QWaylandApplication>();
        if (!wayland) {
            report(false, QStringLiteral("Qt Wayland native interface is unavailable"));
            return false;
        }

        wl_display *display = wayland->display();
        wl_compositor *compositor = wayland->compositor();
        wl_pointer *pointer = wayland->pointer();
        wl_surface *surface = reinterpret_cast<wl_surface *>(window->winId());
        if (!display || !compositor || !pointer || !surface) {
            report(false, QStringLiteral("Wayland display, pointer, compositor, or surface is missing"));
            return false;
        }
        if (!ensureManager(display))
            return false;

        region_ = wl_compositor_create_region(compositor);
        if (!region_) {
            report(false, QStringLiteral("could not create a Wayland region"));
            return false;
        }
        addRegion(region_, region);

        confinement_ = zwp_pointer_constraints_v1_confine_pointer(
            constraints_, surface, pointer, region_,
            ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
        if (!confinement_) {
            release();
            report(false, QStringLiteral("KWin rejected the confinement request"));
            return false;
        }

        static const zwp_confined_pointer_v1_listener listener = {
            &Impl::handleConfined,
            &Impl::handleUnconfined,
        };
        zwp_confined_pointer_v1_add_listener(confinement_, &listener, this);
        surface_ = surface;
        requested_ = true;
        // Pointer-constraint state becomes effective with the next wl_surface
        // commit. Let Qt schedule that commit instead of committing its surface
        // behind the scene graph render thread's back.
        window->requestUpdate();
        if (wl_display_flush(display_) < 0) {
            release();
            report(false, QStringLiteral("failed to flush the Wayland confinement request"));
            return false;
        }
        report(false, QStringLiteral("native confinement requested; waiting for KWin activation"));
        return true;
    }

    bool updateRegion(QWindow *window, const QRect &region)
    {
        if (!requested_ || !confinement_ || !surface_ || !window
            || region.isEmpty()) {
            return false;
        }
        wl_surface *surface = reinterpret_cast<wl_surface *>(window->winId());
        if (surface != surface_)
            return false;

        auto *wayland = qGuiApp
            ->nativeInterface<QNativeInterface::QWaylandApplication>();
        wl_compositor *compositor = wayland ? wayland->compositor() : nullptr;
        if (!compositor)
            return false;

        wl_region *replacement = wl_compositor_create_region(compositor);
        if (!replacement)
            return false;
        addRegion(replacement, region);
        zwp_confined_pointer_v1_set_region(confinement_, replacement);
        window->requestUpdate();
        wl_region_destroy(region_);
        region_ = replacement;
        if (display_)
            wl_display_flush(display_);
        return true;
    }

    void release()
    {
        const bool hadRequest = requested_ || confinement_;
        if (confinement_) {
            zwp_confined_pointer_v1_destroy(confinement_);
            confinement_ = nullptr;
        }
        if (region_) {
            wl_region_destroy(region_);
            region_ = nullptr;
        }
        surface_ = nullptr;
        requested_ = false;
        active_ = false;
        if (display_)
            wl_display_flush(display_);
        if (hadRequest)
            report(false, QStringLiteral("native confinement released"));
        stateCallback_ = {};
    }

    bool requested() const { return requested_; }

private:
    static void addRegion(wl_region *region, const QRect &rect)
    {
        wl_region_add(region, rect.x(), rect.y(),
                      std::max(1, rect.width()), std::max(1, rect.height()));
    }

    bool ensureManager(wl_display *display)
    {
        if (constraints_ && display_ == display)
            return true;
        destroyManager();
        display_ = display;

        wl_event_queue *queue = wl_display_create_queue(display_);
        wl_registry *registry = wl_display_get_registry(display_);
        if (!queue || !registry) {
            if (registry)
                wl_registry_destroy(registry);
            if (queue)
                wl_event_queue_destroy(queue);
            report(false, QStringLiteral("could not inspect Wayland globals"));
            return false;
        }
        wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(registry), queue);

        static const wl_registry_listener registryListener = {
            &Impl::handleGlobal,
            &Impl::handleGlobalRemove,
        };
        wl_registry_add_listener(registry, &registryListener, this);
        const int roundtripResult = wl_display_roundtrip_queue(display_, queue);
        wl_registry_destroy(registry);
        if (constraints_) {
            // Confinement events must be handled by Qt's normal Wayland event
            // dispatch after the temporary registry queue is destroyed.
            wl_proxy_set_queue(
                reinterpret_cast<wl_proxy *>(constraints_), nullptr);
        }
        wl_event_queue_destroy(queue);

        if (roundtripResult < 0 || !constraints_) {
            destroyManager();
            report(false, QStringLiteral("KWin does not expose zwp_pointer_constraints_v1"));
            return false;
        }
        return true;
    }

    void destroyManager()
    {
        release();
        if (constraints_) {
            zwp_pointer_constraints_v1_destroy(constraints_);
            constraints_ = nullptr;
        }
        display_ = nullptr;
    }

    void report(bool active, const QString &message)
    {
        if (stateCallback_)
            stateCallback_(active, message);
    }

    static void handleGlobal(void *data, wl_registry *registry,
                             uint32_t name, const char *interface,
                             uint32_t version)
    {
        auto *self = static_cast<Impl *>(data);
        if (self->constraints_
            || qstrcmp(interface, zwp_pointer_constraints_v1_interface.name) != 0) {
            return;
        }
        self->constraints_ = static_cast<zwp_pointer_constraints_v1 *>(
            wl_registry_bind(registry, name,
                             &zwp_pointer_constraints_v1_interface,
                             std::min<uint32_t>(version, 1)));
    }

    static void handleGlobalRemove(void *, wl_registry *, uint32_t) {}

    static void handleConfined(void *data, zwp_confined_pointer_v1 *)
    {
        auto *self = static_cast<Impl *>(data);
        self->active_ = true;
        self->report(true, QStringLiteral("native confinement active"));
    }

    static void handleUnconfined(void *data, zwp_confined_pointer_v1 *)
    {
        auto *self = static_cast<Impl *>(data);
        self->active_ = false;
        self->report(false, QStringLiteral("native confinement temporarily inactive"));
    }

    wl_display *display_ = nullptr;
    zwp_pointer_constraints_v1 *constraints_ = nullptr;
    zwp_confined_pointer_v1 *confinement_ = nullptr;
    wl_region *region_ = nullptr;
    wl_surface *surface_ = nullptr;
    bool requested_ = false;
    bool active_ = false;
    StateCallback stateCallback_;
};

WaylandPointerConfiner::WaylandPointerConfiner()
    : impl_(std::make_unique<Impl>())
{
}

WaylandPointerConfiner::~WaylandPointerConfiner() = default;

bool WaylandPointerConfiner::confine(QWindow *window, const QRect &region,
                                     const StateCallback &stateCallback)
{
    return impl_->confine(window, region, stateCallback);
}

bool WaylandPointerConfiner::updateRegion(QWindow *window, const QRect &region)
{
    return impl_->updateRegion(window, region);
}

void WaylandPointerConfiner::release()
{
    impl_->release();
}

bool WaylandPointerConfiner::requested() const
{
    return impl_->requested();
}
