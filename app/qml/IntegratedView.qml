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
            property real contextTapX: 0
            property real contextTapY: 0

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
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                cursorShape: Qt.ArrowCursor
                                onPressed: (mouse) => {
                                    if (mouse.button === Qt.RightButton) {
                                        integratedWindow.contextTapX = mouse.x / width
                                        integratedWindow.contextTapY = mouse.y / height
                                        const point = mapToItem(surfaceArea,
                                                                mouse.x, mouse.y)
                                        addControlMenu.x = point.x
                                        addControlMenu.y = point.y
                                        addControlMenu.open()
                                    }
                                }
                            }
                        }

                        Repeater {
                            model: integratedBackend.bindings

                            Item {
                                id: marker
                                required property var modelData
                                required property int index
                                readonly property real markerScale: Math.max(surfaceHost.scale, 0.01)
                                readonly property real circleSize: 46 / markerScale
                                width: 46 / markerScale
                                height: 78 / markerScale
                                x: modelData.x * surfaceHost.width - width / 2
                                y: modelData.y * surfaceHost.height - circleSize / 2
                                visible: integratedBackend.editMode
                                z: 20

                                Rectangle {
                                    id: circle
                                    width: marker.circleSize
                                    height: marker.circleSize
                                    radius: width / 2
                                    color: modelData.key === 0 ? "#cc7d8790" : "#cc3157d5"
                                    border.color: "white"
                                    border.width: 2 / marker.markerScale

                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData.keyName
                                        color: "white"
                                        font.bold: true
                                        font.pixelSize: 15 / marker.markerScale
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton
                                        cursorShape: Qt.SizeAllCursor
                                        drag.target: marker
                                        drag.minimumX: -marker.width / 2
                                        drag.maximumX: surfaceHost.width - marker.width / 2
                                        drag.minimumY: -circle.height / 2
                                        drag.maximumY: surfaceHost.height - circle.height / 2
                                        onReleased: integratedBackend.moveBinding(
                                            marker.index,
                                            (marker.x + marker.width / 2) / surfaceHost.width,
                                            (marker.y + circle.height / 2) / surfaceHost.height)
                                    }
                                }

                                Button {
                                    anchors.top: circle.bottom
                                    anchors.topMargin: 3 / marker.markerScale
                                    anchors.horizontalCenter: circle.horizontalCenter
                                    width: 28 / marker.markerScale
                                    height: 26 / marker.markerScale
                                    text: "⚙"
                                    font.pixelSize: 14 / marker.markerScale
                                    onClicked: {
                                        integratedBackend.selectBinding(marker.index)
                                        bindingSettings.xValue =
                                            integratedBackend.selectedBinding.pixelX
                                        bindingSettings.yValue =
                                            integratedBackend.selectedBinding.pixelY
                                        bindingSettings.modeValue =
                                            integratedBackend.selectedBinding.mode
                                        bindingSettings.open()
                                    }
                                }

                                Button {
                                    x: circle.width - width * 0.62
                                    y: -height * 0.38
                                    width: 25 / marker.markerScale
                                    height: width
                                    z: 5
                                    text: "×"
                                    font.bold: true
                                    font.pixelSize: 16 / marker.markerScale
                                    onClicked: {
                                        if (integratedBackend.selectedBindingIndex
                                                === marker.index)
                                            bindingSettings.close()
                                        integratedBackend.removeBinding(marker.index)
                                    }
                                }
                            }
                        }

                        Item {
                            id: characterCenterMarker
                            readonly property real markerScale:
                                Math.max(surfaceHost.scale, 0.01)
                            readonly property real markerSize: 64 / markerScale
                            width: markerSize
                            height: markerSize
                            x: integratedBackend.characterCenter.x * surfaceHost.width
                               - width / 2
                            y: integratedBackend.characterCenter.y * surfaceHost.height
                               - height / 2
                            visible: integratedBackend.editMode
                                     && integratedBackend.hasCharacterCenter
                            z: 25

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: 3 / characterCenterMarker.markerScale
                                color: "#ff365c"
                            }
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.right: parent.right
                                height: 3 / characterCenterMarker.markerScale
                                color: "#ff365c"
                            }
                            Rectangle {
                                anchors.centerIn: parent
                                width: 9 / characterCenterMarker.markerScale
                                height: width
                                radius: width / 2
                                color: "white"
                                border.color: "#ff365c"
                                border.width: 2 / characterCenterMarker.markerScale
                            }
                            Text {
                                anchors.top: parent.bottom
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "CHARACTER CENTER"
                                color: "#ffcfda"
                                font.bold: true
                                font.pixelSize: 11 / characterCenterMarker.markerScale
                                style: Text.Outline
                                styleColor: "#80000000"
                            }
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton
                                cursorShape: Qt.SizeAllCursor
                                onPositionChanged: (mouse) => {
                                    if (!pressed)
                                        return
                                    const point = mapToItem(surfaceHost,
                                                            mouse.x, mouse.y)
                                    integratedBackend.moveCharacterCenter(
                                        point.x / surfaceHost.width,
                                        point.y / surfaceHost.height)
                                }
                            }
                            Button {
                                x: parent.width - width * 0.62
                                y: -height * 0.38
                                width: 25 / characterCenterMarker.markerScale
                                height: width
                                z: 5
                                text: "×"
                                font.bold: true
                                font.pixelSize: 16 / characterCenterMarker.markerScale
                                onClicked: integratedBackend.removeCharacterCenter()
                            }
                        }

                        Item {
                            id: mobaMovementMarker
                            readonly property real markerScale:
                                Math.max(surfaceHost.scale, 0.01)
                            readonly property real radiusPixels:
                                integratedBackend.mobaMovement.radius
                                * Math.min(surfaceHost.width, surfaceHost.height)
                            width: radiusPixels * 2
                            height: width
                            x: integratedBackend.mobaMovement.x * surfaceHost.width
                               - width / 2
                            y: integratedBackend.mobaMovement.y * surfaceHost.height
                               - height / 2
                            visible: integratedBackend.editMode
                                     && integratedBackend.hasMobaMovement
                            z: 22

                            Rectangle {
                                id: mobaCircle
                                anchors.fill: parent
                                radius: width / 2
                                color: "#3826a7d8"
                                border.color: integratedBackend.hasCharacterCenter
                                              ? "#66d5ff" : "#ffb020"
                                border.width: 3 / mobaMovementMarker.markerScale

                                Rectangle {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: 2 / mobaMovementMarker.markerScale
                                    height: parent.height
                                    color: "#a8eaff"
                                }
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width
                                    height: 2 / mobaMovementMarker.markerScale
                                    color: "#a8eaff"
                                }
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 12 / mobaMovementMarker.markerScale
                                    height: width
                                    radius: width / 2
                                    color: "white"
                                    border.color: "#1584ad"
                                    border.width: 2 / mobaMovementMarker.markerScale
                                }
                                Label {
                                    anchors.centerIn: parent
                                    anchors.verticalCenterOffset:
                                        24 / mobaMovementMarker.markerScale
                                    text: "MOBA  •  RMB"
                                    color: "white"
                                    font.bold: true
                                    font.pixelSize: 13 / mobaMovementMarker.markerScale
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    cursorShape: Qt.SizeAllCursor
                                    onPositionChanged: (mouse) => {
                                        if (!pressed)
                                            return
                                        const point = mapToItem(surfaceHost,
                                                                mouse.x, mouse.y)
                                        integratedBackend.moveMobaMovement(
                                            point.x / surfaceHost.width,
                                            point.y / surfaceHost.height)
                                    }
                                }
                            }

                            Item {
                                id: radiusHandle
                                width: 34 / mobaMovementMarker.markerScale
                                height: width
                                x: parent.width - width / 2
                                anchors.verticalCenter: parent.verticalCenter
                                z: 4

                                Text {
                                    anchors.centerIn: parent
                                    text: "▶"
                                    color: "#ffe066"
                                    font.bold: true
                                    font.pixelSize: 26 / mobaMovementMarker.markerScale
                                    style: Text.Outline
                                    styleColor: "#80000000"
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    cursorShape: Qt.SizeHorCursor
                                    onPositionChanged: (mouse) => {
                                        if (!pressed)
                                            return
                                        const point = mapToItem(surfaceHost,
                                                                mouse.x, mouse.y)
                                        const centerX = integratedBackend.mobaMovement.x
                                                        * surfaceHost.width
                                        const centerY = integratedBackend.mobaMovement.y
                                                        * surfaceHost.height
                                        const dx = point.x - centerX
                                        const dy = point.y - centerY
                                        integratedBackend.resizeMobaMovement(
                                            Math.sqrt(dx * dx + dy * dy)
                                            / Math.min(surfaceHost.width,
                                                       surfaceHost.height))
                                    }
                                }
                            }

                            Button {
                                x: parent.width - width * 0.62
                                y: -height * 0.38
                                width: 25 / mobaMovementMarker.markerScale
                                height: width
                                z: 6
                                text: "×"
                                font.bold: true
                                font.pixelSize: 16 / mobaMovementMarker.markerScale
                                onClicked: integratedBackend.removeMobaMovement()
                            }

                            Rectangle {
                                visible: !integratedBackend.hasCharacterCenter
                                anchors.top: parent.bottom
                                anchors.topMargin: 6 / mobaMovementMarker.markerScale
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: warningText.implicitWidth
                                       + 18 / mobaMovementMarker.markerScale
                                height: warningText.implicitHeight
                                        + 8 / mobaMovementMarker.markerScale
                                radius: 5 / mobaMovementMarker.markerScale
                                color: "#e69a2700"
                                border.color: "#ffb020"

                                Label {
                                    id: warningText
                                    anchors.centerIn: parent
                                    text: "⚠ REQUIRES CHARACTER CENTER"
                                    color: "white"
                                    font.bold: true
                                    font.pixelSize: 11 / mobaMovementMarker.markerScale
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

                    Label {
                        Layout.fillWidth: true
                        text: integratedBackend.hasMobaMovement
                              && !integratedBackend.hasCharacterCenter
                              ? "Editing mapper  •  ⚠ MOBA movement needs Character center"
                              : "Editing mapper"
                        color: "white"
                        font.bold: true
                        font.pixelSize: 17
                    }
                    Button {
                        text: "Done"
                        onClicked: integratedBackend.toggleEditMode()
                    }
                }
            }

            Menu {
                id: addControlMenu
                enabled: integratedBackend.editMode

                MenuItem {
                    text: "Tap button"
                    onTriggered: integratedBackend.addTapAt(
                        integratedWindow.contextTapX,
                        integratedWindow.contextTapY)
                }
                MenuSeparator { }
                MenuItem {
                    text: "Character center (cross)"
                    onTriggered: integratedBackend.addCharacterCenterAt(
                        integratedWindow.contextTapX,
                        integratedWindow.contextTapY)
                }
                MenuItem {
                    text: integratedBackend.hasCharacterCenter
                          ? "MOBA movement (hold RMB)"
                          : "⚠ MOBA movement — requires Character center"
                    onTriggered: integratedBackend.addMobaMovementAt(
                        integratedWindow.contextTapX,
                        integratedWindow.contextTapY)
                }
            }

            Popup {
                id: bindingSettings
                property int xValue: 0
                property int yValue: 0
                property int modeValue: 0
                x: Math.max(0, (surfaceArea.width - width) / 2)
                y: Math.max(0, (surfaceArea.height - height) / 2)
                width: 380
                height: 300
                modal: false
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                onClosed: integratedBackend.selectBinding(-1)

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        color: "#263747"
                        radius: 6

                        Label {
                            anchors.centerIn: parent
                            text: "Tap button settings  •  drag this header"
                            color: "white"
                            font.bold: true
                            font.pixelSize: 16
                        }
                        MouseArea {
                            property real grabX: 0
                            property real grabY: 0
                            anchors.fill: parent
                            cursorShape: Qt.SizeAllCursor
                            onPressed: (mouse) => {
                                grabX = mouse.x
                                grabY = mouse.y
                            }
                            onPositionChanged: (mouse) => {
                                if (!pressed)
                                    return
                                const point = mapToItem(surfaceArea,
                                                        mouse.x, mouse.y)
                                bindingSettings.x = Math.max(
                                    0, Math.min(surfaceArea.width
                                                - bindingSettings.width,
                                                point.x - grabX))
                                bindingSettings.y = Math.max(
                                    0, Math.min(surfaceArea.height
                                                - bindingSettings.height,
                                                point.y - grabY))
                            }
                        }
                    }

                    GridLayout {
                        columns: 2
                        Layout.fillWidth: true
                        columnSpacing: 10
                        rowSpacing: 8

                        Label { text: "Position X" }
                        SpinBox {
                            id: positionX
                            Layout.fillWidth: true
                            from: 0
                            to: integratedBackend.androidWidth
                            editable: true
                            value: bindingSettings.xValue
                            onValueModified: integratedBackend.setSelectedBindingPosition(
                                value, positionY.value)
                        }

                        Label { text: "Position Y" }
                        SpinBox {
                            id: positionY
                            Layout.fillWidth: true
                            from: 0
                            to: integratedBackend.androidHeight
                            editable: true
                            value: bindingSettings.yValue
                            onValueModified: integratedBackend.setSelectedBindingPosition(
                                positionX.value, value)
                        }

                        Label { text: "Keyboard bind" }
                        Button {
                            Layout.fillWidth: true
                            text: integratedBackend.waitingForKey
                                  ? "Press a key…"
                                  : "Bind: " + integratedBackend.selectedBinding.keyName
                            onClicked: integratedBackend.beginRebindSelected()
                        }

                        Label { text: "Tap mode" }
                        ComboBox {
                            Layout.fillWidth: true
                            model: ["Quick tap", "Hold until key release"]
                            currentIndex: bindingSettings.modeValue
                            onActivated: (index) => {
                                bindingSettings.modeValue = index
                                integratedBackend.setSelectedBindingMode(index)
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: integratedBackend.waitingForKey
                              ? "Press the desired key; Esc cancels"
                              : (bindingSettings.modeValue === 0
                                 ? "Quick tap releases after 35 ms"
                                 : "Touch stays down until the keyboard key is released")
                        color: "#718096"
                    }
                }
            }

            Connections {
                target: integratedBackend
                function onEditModeChanged() {
                    if (!integratedBackend.editMode) {
                        addControlMenu.close()
                        bindingSettings.close()
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
            integratedBackend.surfaceReady(xdgSurface.surface)
        }
    }

    ListModel { id: shellSurfaces }
}
