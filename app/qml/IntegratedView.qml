import QtQuick
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

            function toggleFullscreen() {
                if (visibility === Window.FullScreen)
                    visibility = Window.Windowed
                else
                    visibility = Window.FullScreen
            }

            onVisibleChanged: {
                if (visible) {
                    raise()
                    requestActivate()
                }
            }

            Shortcut {
                sequence: "F11"
                context: Qt.ApplicationShortcut
                enabled: integratedBackend.windowVisible
                autoRepeat: false
                onActivated: integratedWindow.toggleFullscreen()
            }

            Item {
                id: surfaceArea
                anchors.fill: parent

                Repeater {
                    model: shellSurfaces
                    Item {
                        width: Math.max(1, shellItem.width)
                        height: Math.max(1, shellItem.height)
                        anchors.centerIn: surfaceArea
                        transformOrigin: Item.Center
                        scale: Math.min(surfaceArea.width / width,
                                        surfaceArea.height / height)

                        ShellSurfaceItem {
                            id: shellItem
                            x: 0
                            y: 0
                            shellSurface: model.shellSurface
                            focus: true
                            onSurfaceDestroyed: shellSurfaces.remove(index)
                        }
                    }
                }
            }

            onClosing: (close) => {
                close.accepted = false
                visibility = Window.Windowed
                integratedBackend.hideIntegratedWindow()
            }
        }
    }

    XdgShell {
        onToplevelCreated: (toplevel, xdgSurface) => {
            shellSurfaces.append({shellSurface: xdgSurface})
            integratedBackend.surfaceReady()
        }
    }

    ListModel { id: shellSurfaces }
}
