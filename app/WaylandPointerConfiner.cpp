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
    ~Impl() { destroyWaylandObjects(); }

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

        wl_surface *surface = reinterpret_cast<wl_surface *>(window->winId());
        if (!surface) {
            report(false, QStringLiteral("Wayland surface is unavailable"));
            return false;
        }

        // Do not use QNativeInterface::QWaylandApplication here. Qt explicitly
        // gives native interfaces no minor-version ABI guarantee, so a portable
        // EWM built against Qt 6.8 asks for interface revision 1 while Arch Qt
        // 6.11 exposes revision 2 and returns nullptr. Since libwayland 1.23 we
        // can recover the owning display directly from Qt's wl_surface and bind
        // the compositor, seat, pointer and pointer-constraints globals on the
        // exact same connection ourselves.
        wl_display *display = wl_proxy_get_display(
            reinterpret_cast<wl_proxy *>(surface));
        if (!display) {
            report(false, QStringLiteral("could not recover Wayland display from Qt surface"));
            return false;
        }
        if (!ensureWaylandObjects(display))
            return false;

        region_ = wl_compositor_create_region(compositor_);
        if (!region_) {
            report(false, QStringLiteral("could not create a Wayland region"));
            return false;
        }
        addRegion(region_, region);

        confinement_ = zwp_pointer_constraints_v1_confine_pointer(
            constraints_, surface, pointer_, region_,
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
            || !compositor_ || region.isEmpty()) {
            return false;
        }
        wl_surface *surface = reinterpret_cast<wl_surface *>(window->winId());
        if (surface != surface_)
            return false;

        wl_region *replacement = wl_compositor_create_region(compositor_);
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

    bool ensureWaylandObjects(wl_display *display)
    {
        if (display_ == display && constraints_ && compositor_ && seat_ && pointer_)
            return true;

        destroyWaylandObjects();
        display_ = display;

        wl_event_queue *queue = wl_display_create_queue(display_);
        wl_registry *registry = wl_display_get_registry(display_);
        if (!queue || !registry) {
            if (registry)
                wl_registry_destroy(registry);
            if (queue)
                wl_event_queue_destroy(queue);
            display_ = nullptr;
            report(false, QStringLiteral("could not inspect Wayland globals"));
            return false;
        }
        wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(registry), queue);

        static const wl_registry_listener registryListener = {
            &Impl::handleGlobal,
            &Impl::handleGlobalRemove,
        };
        wl_registry_add_listener(registry, &registryListener, this);

        // First roundtrip discovers globals and binds wl_seat. The second one
        // receives wl_seat.capabilities, where we safely create our wl_pointer.
        const int globalsRoundtrip = wl_display_roundtrip_queue(display_, queue);
        const int seatRoundtrip = globalsRoundtrip < 0
            ? -1 : wl_display_roundtrip_queue(display_, queue);

        wl_registry_destroy(registry);

        // Objects created from the temporary registry/seat inherit its queue.
        // Move them to Qt's default Wayland queue before destroying ours.
        auto moveToDefaultQueue = [](void *object) {
            if (object)
                wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(object), nullptr);
        };
        moveToDefaultQueue(constraints_);
        moveToDefaultQueue(compositor_);
        moveToDefaultQueue(seat_);
        moveToDefaultQueue(pointer_);
        wl_event_queue_destroy(queue);

        if (globalsRoundtrip < 0 || seatRoundtrip < 0) {
            destroyWaylandObjects();
            report(false, QStringLiteral("Wayland registry roundtrip failed"));
            return false;
        }
        if (!constraints_) {
            destroyWaylandObjects();
            report(false, QStringLiteral("KWin does not expose zwp_pointer_constraints_v1"));
            return false;
        }
        if (!compositor_) {
            destroyWaylandObjects();
            report(false, QStringLiteral("Wayland compositor global is unavailable"));
            return false;
        }
        if (!seat_) {
            destroyWaylandObjects();
            report(false, QStringLiteral("Wayland seat global is unavailable"));
            return false;
        }
        if (!pointer_) {
            destroyWaylandObjects();
            report(false, QStringLiteral("Wayland seat has no pointer capability"));
            return false;
        }
        return true;
    }

    void destroyWaylandObjects()
    {
        // release() intentionally clears the callback for a completed public
        // unlock. Internal Wayland rebinding must not do that, otherwise the
        // caller loses the exact failure reason we are about to report.
        const StateCallback callback = stateCallback_;
        release();
        stateCallback_ = callback;

        if (pointer_) {
            // We bind wl_seat version 1 deliberately, so wl_pointer.release is
            // not available. Destroying the local proxy is sufficient for this
            // helper object; the server-side resource disappears with EWM's
            // existing Qt Wayland connection.
            wl_proxy_destroy(reinterpret_cast<wl_proxy *>(pointer_));
            pointer_ = nullptr;
        }
        if (seat_) {
            wl_proxy_destroy(reinterpret_cast<wl_proxy *>(seat_));
            seat_ = nullptr;
        }
        if (compositor_) {
            wl_proxy_destroy(reinterpret_cast<wl_proxy *>(compositor_));
            compositor_ = nullptr;
        }
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

        if (!self->constraints_
            && qstrcmp(interface, zwp_pointer_constraints_v1_interface.name) == 0) {
            self->constraints_ = static_cast<zwp_pointer_constraints_v1 *>(
                wl_registry_bind(registry, name,
                                 &zwp_pointer_constraints_v1_interface,
                                 std::min<uint32_t>(version, 1)));
            return;
        }
        if (!self->compositor_
            && qstrcmp(interface, wl_compositor_interface.name) == 0) {
            self->compositor_ = static_cast<wl_compositor *>(
                wl_registry_bind(registry, name, &wl_compositor_interface, 1));
            return;
        }
        if (!self->seat_ && qstrcmp(interface, wl_seat_interface.name) == 0) {
            self->seat_ = static_cast<wl_seat *>(
                wl_registry_bind(registry, name, &wl_seat_interface, 1));
            static const wl_seat_listener seatListener = {
                &Impl::handleSeatCapabilities,
                nullptr,
            };
            wl_seat_add_listener(self->seat_, &seatListener, self);
        }
    }

    static void handleGlobalRemove(void *, wl_registry *, uint32_t) {}

    static void handleSeatCapabilities(void *data, wl_seat *seat,
                                       uint32_t capabilities)
    {
        auto *self = static_cast<Impl *>(data);
        if (seat != self->seat_)
            return;

        const bool hasPointer = capabilities & WL_SEAT_CAPABILITY_POINTER;
        if (hasPointer && !self->pointer_) {
            self->pointer_ = wl_seat_get_pointer(seat);
            if (!self->pointer_)
                return;
            static const wl_pointer_listener pointerListener = {
                &Impl::handlePointerEnter,
                &Impl::handlePointerLeave,
                &Impl::handlePointerMotion,
                &Impl::handlePointerButton,
                &Impl::handlePointerAxis,
            };
            wl_pointer_add_listener(self->pointer_, &pointerListener, self);
        } else if (!hasPointer && self->pointer_) {
            wl_proxy_destroy(reinterpret_cast<wl_proxy *>(self->pointer_));
            self->pointer_ = nullptr;
        }
    }

    // EWM only needs a wl_pointer protocol object as the identity used by
    // zwp_pointer_constraints_v1. Qt keeps handling the real host pointer
    // events through its own wl_pointer, so these callbacks intentionally do
    // nothing. Binding wl_seat version 1 guarantees only these five events.
    static void handlePointerEnter(void *, wl_pointer *, uint32_t, wl_surface *,
                                   wl_fixed_t, wl_fixed_t) {}
    static void handlePointerLeave(void *, wl_pointer *, uint32_t, wl_surface *) {}
    static void handlePointerMotion(void *, wl_pointer *, uint32_t,
                                    wl_fixed_t, wl_fixed_t) {}
    static void handlePointerButton(void *, wl_pointer *, uint32_t, uint32_t,
                                    uint32_t, uint32_t) {}
    static void handlePointerAxis(void *, wl_pointer *, uint32_t, uint32_t,
                                  wl_fixed_t) {}

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
    wl_compositor *compositor_ = nullptr;
    wl_seat *seat_ = nullptr;
    wl_pointer *pointer_ = nullptr;
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
