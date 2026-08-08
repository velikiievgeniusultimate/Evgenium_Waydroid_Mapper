import QtQuick
import QtQuick.Controls
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
                        id: surfaceHost
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

                        Rectangle {
                            anchors.fill: parent
                            color: "#18000000"
                            visible: integratedBackend.editMode
                            z: 10

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: integratedBackend.placementMode
                                             ? Qt.CrossCursor : Qt.ArrowCursor
                                onClicked: (mouse) => {
                                    if (integratedBackend.placementMode)
                                        integratedBackend.chooseTapPosition(
                                            mouse.x / width, mouse.y / height)
                                }
                            }
                        }

                        Repeater {
                            model: integratedBackend.bindings

                            Rectangle {
                                required property var modelData
                                required property int index
                                readonly property real markerScale: Math.max(surfaceHost.scale, 0.01)
                                width: 46 / markerScale
                                height: 46 / markerScale
                                radius: width / 2
                                x: modelData.x * surfaceHost.width - width / 2
                                y: modelData.y * surfaceHost.height - height / 2
                                visible: integratedBackend.editMode
                                color: "#cc3157d5"
                                border.color: "white"
                                border.width: 2 / markerScale
                                z: 20

                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.keyName
                                    color: "white"
                                    font.bold: true
                                    font.pixelSize: 15 / parent.markerScale
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: integratedBackend.removeBinding(index)
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                visible: integratedBackend.editMode
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 12
                width: Math.min(parent.width - 24, 760)
                height: 64
                radius: 10
                color: "#ee202a35"
                border.color: "#64788b"
                z: 100

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 9
                    spacing: 10

                    Button {
                        text: "Add tap"
                        enabled: !integratedBackend.placementMode
                                 && !integratedBackend.waitingForKey
                        onClicked: integratedBackend.beginAddTap()
                    }
                    Label {
                        Layout.fillWidth: true
                        text: integratedBackend.editorMessage
                        color: "white"
                        elide: Text.ElideRight
                    }
                    Button {
                        text: "Done (F5)"
                        onClicked: integratedBackend.toggleEditMode()
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
