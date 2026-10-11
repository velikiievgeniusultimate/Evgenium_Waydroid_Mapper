import QtQuick
import QtQml
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import QtWayland.Compositor
import QtWayland.Compositor.XdgShell

WaylandCompositor {
    id: compositor
    socketName: "evgenium-wayland-0"

    WaylandOutput {
        id: output
        sizeFollowsWindow: true

        window: Window {
            id: integratedWindow
            width: 1280
            height: 760
            minimumWidth: 640
            minimumHeight: 480
            visible: integratedBackend.windowVisible
            title: "Evgenium Waydroid Mapper — Integrated Android"
            color: "#111820"
            property var mapperOverlay: null
            property int mapperOverlayRevision: 0
            property string mapperOverlaySource: ""
            function reloadMapperOverlay(url) {
                const component = Qt.createComponent(url)
                if (component.status !== Component.Ready)
                    return component.errorString()
                const next = component.createObject(integratedWindow.contentItem,
                    {shellSurfaces: shellSurfaces, z: 10, visible: false})
                if (!next)
                    return component.errorString()
                const previous = mapperOverlay
                mapperOverlay = next
                if (previous) {
                    previous.visible = false
                    previous.destroy()
                }
                next.visible = true
                mapperOverlaySource = url.toString()
                ++mapperOverlayRevision
                return ""
            }
            Component.onCompleted: {
                const error = reloadMapperOverlay(integratedBackend.mapperOverlayUrl())
                if (error) console.error(error)
            }
            Repeater {
                model: shellSurfaces
                Item {
                    width: Math.max(1, shellItem.width)
                    height: Math.max(1, shellItem.height)
                    anchors.centerIn: parent
                    scale: Math.min(integratedWindow.width / width,
                                    integratedWindow.height / height)
                        ShellSurfaceItem {
                            id: shellItem
                            x: 0
                            y: 0
                            shellSurface: model.shellSurface
                            // The editor injects its own native Wayland touch.
                            // Never forward the physical mouse to Android at the
                            // same time: even a hover motion can replace/cancel
                            // fake_touch's currently held calibration finger.
                            inputEventsEnabled: !integratedBackend.editMode
                                                && !integratedBackend.profileManagerVisible
                                                && (!integratedBackend.centerVision.visible
                                                    || integratedBackend.centerVision.tracking)
                                                && !integratedBackend.syntheticTouchActive
                            touchEventsEnabled: false
                            focus: true
                            onSurfaceDestroyed: shellSurfaces.remove(index)
                        }

                        Connections {
                            target: integratedBackend
                            function onAutoGeometryFrameRequested(requestId) {
                                if (index !== shellSurfaces.count - 1) return
                                // Native texture for precise rims/dot. Only the detector's
                                // broad candidate search may downsample the frame.
                                const factor = 1
                                const accepted = shellItem.grabToImage(function(result) {
                                    integratedBackend.submitAutoGeometryFrame(requestId, result.image)
                                }, Qt.size(Math.max(1, Math.round(shellItem.width * factor)),
                                           Math.max(1, Math.round(shellItem.height * factor))))
                                if (!accepted)
                                    integratedBackend.reportAutoGeometryFrameFailure(requestId, "Захват текстуры недоступен")
                            }
                        }

                        Connections {
                            target: integratedBackend.centerVision
                            function onRenderedFrameRequested(requestId) {
                                // Only the newest Android surface is the active
                                // game surface.  Grab the item that is actually
                                // rendered, not the translucent laboratory UI.
                                if (index !== shellSurfaces.count - 1)
                                    return
                                const accepted = shellItem.grabToImage(function(result) {
                                    integratedBackend.centerVision.submitRenderedFrame(
                                        requestId, result.image)
                                }, Qt.size(Math.max(1, shellItem.width),
                                           Math.max(1, shellItem.height)))
                                if (!accepted)
                                    integratedBackend.centerVision.reportRenderedFrameFailure(
                                        requestId, "ShellSurfaceItem.grabToImage rejected request")
                            }
                        }

                }
            }
            onClosing: (close) => {
                visibility = Window.Windowed
                integratedBackend.hideIntegratedWindow()
                close.accepted = true
            }
        }
    }

    XdgShell {
        onToplevelCreated: (toplevel, xdgSurface) => {
            shellSurfaces.append({shellSurface: xdgSurface})
            integratedBackend.surfaceReady(xdgSurface.surface)
        }
    }

    ListModel { id: shellSurfaces }
}
