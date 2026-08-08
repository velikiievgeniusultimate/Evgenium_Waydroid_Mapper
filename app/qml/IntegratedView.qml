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
            minimumWidth: 800
            minimumHeight: 500
            visible: true
            title: "Evgenium Waydroid Mapper — Integrated Android"
            color: "#111820"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 58
                    color: "#202a35"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 18
                        anchors.rightMargin: 12

                        Label {
                            text: "Integrated Android"
                            color: "white"
                            font.pixelSize: 18
                            font.bold: true
                        }
                        Label {
                            id: statusLabel
                            Layout.fillWidth: true
                            text: "Waiting for Waydroid surface…"
                            color: "#aebdca"
                            elide: Text.ElideRight
                        }
                        Button {
                            text: "Restart Android"
                            onClicked: integratedBackend.restartAndroid()
                        }
                        Button {
                            text: "Stop"
                            onClicked: integratedBackend.stopIntegratedSession()
                        }
                    }
                }

                Item {
                    id: surfaceArea
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Label {
                        anchors.centerIn: parent
                        text: "Starting nested Wayland compositor…"
                        color: "#8192a3"
                        font.pixelSize: 20
                    }

                    Repeater {
                        model: shellSurfaces
                        ShellSurfaceItem {
                            anchors.fill: surfaceArea
                            shellSurface: model.shellSurface
                            focus: true
                            onSurfaceDestroyed: shellSurfaces.remove(index)
                        }
                    }
                }
            }

            onClosing: integratedBackend.stopIntegratedSession()
        }
    }

    XdgShell {
        onToplevelCreated: (toplevel, xdgSurface) => {
            shellSurfaces.append({shellSurface: xdgSurface})
        }
    }

    ListModel { id: shellSurfaces }

    Connections {
        target: integratedBackend
        function onStatusChanged(status) { statusLabel.text = status }
    }
}

