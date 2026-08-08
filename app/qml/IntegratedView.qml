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
            property bool contentFullscreen: false
            width: 1280
            height: 760
            minimumWidth: 800
            minimumHeight: 500
            visible: true
            title: "Evgenium Waydroid Mapper — Integrated Android"
            color: "#111820"

            function toggleContentFullscreen() {
                contentFullscreen = !contentFullscreen
                visibility = contentFullscreen ? Window.FullScreen : Window.Windowed
            }

            Shortcut {
                sequence: "F11"
                context: Qt.ApplicationShortcut
                onActivated: integratedWindow.toggleContentFullscreen()
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    id: toolbar
                    Layout.fillWidth: true
                    Layout.preferredHeight: integratedWindow.contentFullscreen ? 0 : 58
                    visible: !integratedWindow.contentFullscreen
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
                        ComboBox {
                            id: resolutionBox
                            textRole: "label"
                            model: ListModel {
                                ListElement { label: "1920 × 1080"; screenWidth: 1920; screenHeight: 1080 }
                                ListElement { label: "1600 × 900"; screenWidth: 1600; screenHeight: 900 }
                                ListElement { label: "1280 × 720"; screenWidth: 1280; screenHeight: 720 }
                                ListElement { label: "1080 × 1080"; screenWidth: 1080; screenHeight: 1080 }
                                ListElement { label: "900 × 900"; screenWidth: 900; screenHeight: 900 }
                                ListElement { label: "720 × 720"; screenWidth: 720; screenHeight: 720 }
                            }
                        }
                        Button {
                            text: "Apply"
                            onClicked: {
                                const entry = resolutionBox.model.get(resolutionBox.currentIndex)
                                integratedBackend.applyResolution(entry.screenWidth, entry.screenHeight)
                            }
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

            Label {
                visible: integratedWindow.contentFullscreen
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 10
                text: "F11 — exit fullscreen"
                color: "#80ffffff"
                z: 1000
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
