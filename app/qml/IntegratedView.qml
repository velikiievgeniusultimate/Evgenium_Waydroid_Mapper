import QtQuick
import QtQml
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
                            // The editor injects its own native Wayland touch.
                            // Never forward the physical mouse to Android at the
                            // same time: even a hover motion can replace/cancel
                            // fake_touch's currently held calibration finger.
                            inputEventsEnabled: !integratedBackend.editMode
                            touchEventsEnabled: false
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
                            id: skillCancelMarker
                            readonly property real markerScale:
                                Math.max(surfaceHost.scale, 0.01)
                            readonly property real markerSize: 64 / markerScale
                            width: markerSize
                            height: markerSize
                            x: integratedBackend.skillCancel.x * surfaceHost.width
                               - width / 2
                            y: integratedBackend.skillCancel.y * surfaceHost.height
                               - height / 2
                            visible: integratedBackend.editMode
                                     && integratedBackend.hasSkillCancel
                            z: 27

                            Rectangle {
                                id: skillCancelButton
                                anchors.fill: parent
                                radius: 12 / skillCancelMarker.markerScale
                                color: "#cc9c2438"
                                border.color: integratedBackend.skillCancel.ready
                                              ? "#ff8ca0" : "#ffbf47"
                                border.width: 3 / skillCancelMarker.markerScale

                                Text {
                                    anchors.centerIn: parent
                                    text: integratedBackend.skillCancel.key === 0
                                          ? "CANCEL\nUNBOUND"
                                          : "CANCEL\n"
                                            + integratedBackend.skillCancel.keyName
                                    horizontalAlignment: Text.AlignHCenter
                                    color: "white"
                                    font.bold: true
                                    font.pixelSize: 11 / skillCancelMarker.markerScale
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
                                        integratedBackend.moveSkillCancel(
                                            point.x / surfaceHost.width,
                                            point.y / surfaceHost.height)
                                    }
                                }
                            }

                            Button {
                                anchors.top: skillCancelButton.bottom
                                anchors.topMargin: 3 / skillCancelMarker.markerScale
                                anchors.horizontalCenter:
                                    skillCancelButton.horizontalCenter
                                width: 28 / skillCancelMarker.markerScale
                                height: 26 / skillCancelMarker.markerScale
                                text: "⚙"
                                font.pixelSize: 14 / skillCancelMarker.markerScale
                                onClicked: {
                                    cancelSettings.xValue =
                                        integratedBackend.skillCancel.pixelX
                                    cancelSettings.yValue =
                                        integratedBackend.skillCancel.pixelY
                                    cancelSettings.open()
                                }
                            }

                            Button {
                                x: parent.width - width * 0.62
                                y: -height * 0.38
                                width: 25 / skillCancelMarker.markerScale
                                height: width
                                z: 5
                                text: "×"
                                font.bold: true
                                font.pixelSize: 16 / skillCancelMarker.markerScale
                                onClicked: {
                                    cancelSettings.close()
                                    integratedBackend.removeSkillCancel()
                                }
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

                        Repeater {
                            model: integratedBackend.mobaSkills

                            Item {
                                id: skillMarker
                                required property var modelData
                                required property int index
                                readonly property real markerScale:
                                    Math.max(surfaceHost.scale, 0.01)
                                property bool resizingRadius: false
                                property real previewRadius: modelData.radius
                                readonly property real radiusPixels:
                                    (resizingRadius ? previewRadius
                                                    : modelData.radius)
                                    * Math.min(surfaceHost.width, surfaceHost.height)
                                width: radiusPixels * 2
                                height: width
                                visible: integratedBackend.editMode
                                         && !integratedBackend.calibrationActive
                                z: 24

                                Binding {
                                    target: skillMarker
                                    property: "x"
                                    value: modelData.x * surfaceHost.width
                                           - skillMarker.width / 2
                                    when: !skillMoveMouse.drag.active
                                    restoreMode: Binding.RestoreNone
                                }
                                Binding {
                                    target: skillMarker
                                    property: "y"
                                    value: modelData.y * surfaceHost.height
                                           - skillMarker.height / 2
                                    when: !skillMoveMouse.drag.active
                                    restoreMode: Binding.RestoreNone
                                }

                                Rectangle {
                                    id: skillCircle
                                    anchors.fill: parent
                                    radius: width / 2
                                    color: modelData.calibrated
                                           ? "#4439b86f" : "#44a260d1"
                                    border.color: !integratedBackend.hasCharacterCenter
                                                  ? "#ffb020"
                                                  : (modelData.calibrated
                                                     ? "#72f2a4" : "#df9cff")
                                    border.width: 3 / skillMarker.markerScale

                                    Rectangle {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        width: 2 / skillMarker.markerScale
                                        height: parent.height
                                        color: "#e8c9ff"
                                    }
                                    Rectangle {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width
                                        height: 2 / skillMarker.markerScale
                                        color: "#e8c9ff"
                                    }
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 12 / skillMarker.markerScale
                                        height: width
                                        radius: width / 2
                                        color: "white"
                                        border.color: "#8d3ab4"
                                        border.width: 2 / skillMarker.markerScale
                                    }
                                    Label {
                                        anchors.centerIn: parent
                                        anchors.verticalCenterOffset:
                                            23 / skillMarker.markerScale
                                        text: "SKILL  •  " + modelData.keyName
                                        color: "white"
                                        font.bold: true
                                        font.pixelSize: 12 / skillMarker.markerScale
                                    }
                                    MouseArea {
                                        id: skillMoveMouse
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton
                                        cursorShape: Qt.SizeAllCursor
                                        drag.target: skillMarker
                                        drag.minimumX: -skillMarker.width / 2
                                        drag.maximumX: surfaceHost.width
                                                       - skillMarker.width / 2
                                        drag.minimumY: -skillMarker.height / 2
                                        drag.maximumY: surfaceHost.height
                                                       - skillMarker.height / 2
                                        onReleased: integratedBackend.moveMobaSkill(
                                            skillMarker.index,
                                            (skillMarker.x + skillMarker.width / 2)
                                            / surfaceHost.width,
                                            (skillMarker.y + skillMarker.height / 2)
                                            / surfaceHost.height)
                                    }
                                }

                                Item {
                                    id: skillRadiusHandle
                                    width: 34 / skillMarker.markerScale
                                    height: width
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: skillMarker.width - width / 2
                                    z: 5

                                    Text {
                                        anchors.centerIn: parent
                                        text: "▶"
                                        color: "#ffe066"
                                        font.bold: true
                                        font.pixelSize: 26 / skillMarker.markerScale
                                        style: Text.Outline
                                        styleColor: "#80000000"
                                    }
                                    MouseArea {
                                        id: skillRadiusMouse
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton
                                        cursorShape: Qt.SizeHorCursor
                                        onPressed: {
                                            skillMarker.previewRadius = modelData.radius
                                            skillMarker.resizingRadius = true
                                        }
                                        onPositionChanged: mouse => {
                                            if (!pressed)
                                                return
                                            const point = mapToItem(
                                                surfaceHost, mouse.x, mouse.y)
                                            const centerX = modelData.x
                                                                  * surfaceHost.width
                                            const centerY = modelData.y
                                                                  * surfaceHost.height
                                            const distance = Math.hypot(
                                                point.x - centerX,
                                                point.y - centerY)
                                            const minimumSide = Math.min(
                                                surfaceHost.width,
                                                surfaceHost.height)
                                            skillMarker.previewRadius = Math.max(
                                                24 / minimumSide,
                                                Math.min(0.35,
                                                         distance / minimumSide))
                                        }
                                        onReleased: {
                                            integratedBackend.resizeMobaSkill(
                                                skillMarker.index,
                                                skillMarker.previewRadius)
                                            skillMarker.resizingRadius = false
                                        }
                                        onCanceled: {
                                            skillMarker.previewRadius = modelData.radius
                                            skillMarker.resizingRadius = false
                                        }
                                    }
                                }

                                Button {
                                    anchors.top: skillCircle.bottom
                                    anchors.topMargin: 3 / skillMarker.markerScale
                                    anchors.horizontalCenter: skillCircle.horizontalCenter
                                    width: 28 / skillMarker.markerScale
                                    height: 26 / skillMarker.markerScale
                                    text: "⚙"
                                    font.pixelSize: 14 / skillMarker.markerScale
                                    onClicked: {
                                        integratedBackend.selectMobaSkill(skillMarker.index)
                                        skillSettings.xValue =
                                            integratedBackend.selectedMobaSkill.pixelX
                                        skillSettings.yValue =
                                            integratedBackend.selectedMobaSkill.pixelY
                                        skillSettings.diameterValue =
                                            integratedBackend.selectedMobaSkill.diameterPixels
                                        skillSettings.modeValue =
                                            integratedBackend.selectedMobaSkill.mode
                                        skillSettings.speedValue =
                                            integratedBackend.selectedMobaSkill.speedLevel
                                        skillSettings.open()
                                    }
                                }

                                Button {
                                    x: skillCircle.width - width * 0.62
                                    y: -height * 0.38
                                    width: 25 / skillMarker.markerScale
                                    height: width
                                    z: 6
                                    text: "×"
                                    font.bold: true
                                    font.pixelSize: 16 / skillMarker.markerScale
                                    onClicked: {
                                        if (integratedBackend.selectedMobaSkillIndex
                                                === skillMarker.index)
                                            skillSettings.close()
                                        integratedBackend.removeMobaSkill(skillMarker.index)
                                    }
                                }

                                Rectangle {
                                    anchors.top: parent.bottom
                                    anchors.topMargin: 34 / skillMarker.markerScale
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: skillWarning.implicitWidth
                                           + 18 / skillMarker.markerScale
                                    height: skillWarning.implicitHeight
                                            + 8 / skillMarker.markerScale
                                    radius: 5 / skillMarker.markerScale
                                    color: modelData.ready ? "#d9267044" : "#e69a2700"
                                    border.color: modelData.ready ? "#72f2a4" : "#ffb020"

                                    Label {
                                        id: skillWarning
                                        anchors.centerIn: parent
                                        text: !integratedBackend.hasCharacterCenter
                                              ? "⚠ REQUIRES CHARACTER CENTER"
                                              : (modelData.key === 0
                                                 ? "⚠ CHOOSE A BIND"
                                                 : (modelData.calibrated
                                                    ? "✓ CALIBRATED"
                                                    : "⚠ CALIBRATION REQUIRED"))
                                        color: "white"
                                        font.bold: true
                                        font.pixelSize: 11 / skillMarker.markerScale
                                    }
                                }
                            }
                        }

                        Item {
                            anchors.fill: parent
                            visible: integratedBackend.calibrationActive
                            z: 90

                            Rectangle {
                                anchors.fill: parent
                                color: "#10000000"
                            }

                            Repeater {
                                model: integratedBackend.calibrationPoints
                                Rectangle {
                                    required property var modelData
                                    readonly property real pointSize:
                                        13 / Math.max(surfaceHost.scale, 0.01)
                                    x: modelData.x * surfaceHost.width - width / 2
                                    y: modelData.y * surfaceHost.height - height / 2
                                    width: pointSize
                                    height: pointSize
                                    radius: width / 2
                                    color: "#72f2a4"
                                    border.color: "white"
                                    border.width: 2 / Math.max(surfaceHost.scale, 0.01)
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.AllButtons
                                hoverEnabled: true
                                preventStealing: true
                                cursorShape: integratedBackend.calibrationPointReady
                                             ? Qt.CrossCursor : Qt.BusyCursor
                                onClicked: (mouse) => {
                                    if (mouse.button === Qt.LeftButton
                                            && integratedBackend.calibrationPointReady)
                                        integratedBackend.recordMobaSkillCalibrationPoint(
                                            mouse.x / width, mouse.y / height)
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                visible: integratedBackend.editMode
                         && !integratedBackend.calibrationActive
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
                        text: (integratedBackend.hasMobaMovement
                               || integratedBackend.hasMobaSkills)
                              && !integratedBackend.hasCharacterCenter
                              ? "Editing mapper  •  ⚠ MOBA controls need Character center"
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
                MenuItem {
                    text: integratedBackend.hasCharacterCenter
                          ? "MOBA skill"
                          : "⚠ MOBA skill — requires Character center"
                    onTriggered: integratedBackend.addMobaSkillAt(
                        integratedWindow.contextTapX,
                        integratedWindow.contextTapY)
                }
                MenuItem {
                    text: integratedBackend.hasSkillCancel
                          ? "Move MOBA skill cancel here"
                          : "MOBA skill cancel"
                    onTriggered: integratedBackend.addSkillCancelAt(
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

            Popup {
                id: cancelSettings
                property int xValue: 0
                property int yValue: 0
                x: Math.max(0, (surfaceArea.width - width) / 2)
                y: Math.max(0, (surfaceArea.height - height) / 2)
                width: 420
                height: 250
                modal: false
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        color: "#5a2430"
                        radius: 6

                        Label {
                            anchors.centerIn: parent
                            text: "MOBA skill cancel  •  drag this header"
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
                                cancelSettings.x = Math.max(
                                    0, Math.min(surfaceArea.width
                                                - cancelSettings.width,
                                                point.x - grabX))
                                cancelSettings.y = Math.max(
                                    0, Math.min(surfaceArea.height
                                                - cancelSettings.height,
                                                point.y - grabY))
                            }
                        }
                    }

                    GridLayout {
                        columns: 4
                        Layout.fillWidth: true
                        Label { text: "Position X" }
                        SpinBox {
                            id: cancelPositionX
                            Layout.fillWidth: true
                            from: 0
                            to: integratedBackend.androidWidth
                            editable: true
                            value: cancelSettings.xValue
                            onValueModified:
                                integratedBackend.setSkillCancelPosition(
                                    value, cancelPositionY.value)
                        }
                        Label { text: "Y" }
                        SpinBox {
                            id: cancelPositionY
                            Layout.fillWidth: true
                            from: 0
                            to: integratedBackend.androidHeight
                            editable: true
                            value: cancelSettings.yValue
                            onValueModified:
                                integratedBackend.setSkillCancelPosition(
                                    cancelPositionX.value, value)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "Cancel key"
                            font.bold: true
                        }
                        Button {
                            Layout.fillWidth: true
                            text: integratedBackend.waitingForKey
                                  ? "Press a key…"
                                  : "Bind: " + integratedBackend.skillCancel.keyName
                            onClicked: integratedBackend.beginRebindSkillCancel()
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "While a MOBA skill is held, this key moves its existing finger here and releases it to cancel."
                        color: "#718096"
                    }
                }
            }

            Popup {
                id: skillSettings
                property int xValue: 0
                property int yValue: 0
                property int diameterValue: 120
                property int modeValue: 0
                property int speedValue: 4
                x: Math.max(0, (surfaceArea.width - width) / 2)
                y: Math.max(0, (surfaceArea.height - height) / 2)
                width: 470
                height: Math.min(500, surfaceArea.height - 24)
                modal: false
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                onClosed: integratedBackend.selectMobaSkill(-1)

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 11

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        color: "#3c2749"
                        radius: 6

                        Label {
                            anchors.centerIn: parent
                            text: "MOBA skill settings  •  drag this header"
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
                                skillSettings.x = Math.max(
                                    0, Math.min(surfaceArea.width
                                                - skillSettings.width,
                                                point.x - grabX))
                                skillSettings.y = Math.max(
                                    0, Math.min(surfaceArea.height
                                                - skillSettings.height,
                                                point.y - grabY))
                            }
                        }
                    }

                    Label {
                        text: "1. Skill joystick center"
                        font.bold: true
                    }
                    GridLayout {
                        columns: 4
                        Layout.fillWidth: true
                        Label { text: "X" }
                        SpinBox {
                            id: skillPositionX
                            Layout.fillWidth: true
                            from: 0
                            to: integratedBackend.androidWidth
                            editable: true
                            value: skillSettings.xValue
                            onValueModified:
                                integratedBackend.setSelectedMobaSkillPosition(
                                    value, skillPositionY.value)
                        }
                        Label { text: "Y" }
                        SpinBox {
                            id: skillPositionY
                            Layout.fillWidth: true
                            from: 0
                            to: integratedBackend.androidHeight
                            editable: true
                            value: skillSettings.yValue
                            onValueModified:
                                integratedBackend.setSelectedMobaSkillPosition(
                                    skillPositionX.value, value)
                        }
                    }

                    GridLayout {
                        columns: 2
                        Layout.fillWidth: true
                        columnSpacing: 10
                        rowSpacing: 9

                        Label {
                            text: "2. Joystick diameter"
                            font.bold: true
                        }
                        SpinBox {
                            Layout.fillWidth: true
                            from: 48
                            to: Math.min(integratedBackend.androidWidth,
                                         integratedBackend.androidHeight) * 0.7
                            editable: true
                            value: skillSettings.diameterValue
                            textFromValue: (value) => value + " px"
                            valueFromText: (text) => parseInt(text)
                            onValueModified:
                                integratedBackend.setSelectedMobaSkillDiameter(value)
                        }

                        Label {
                            text: "3. Keyboard bind"
                            font.bold: true
                        }
                        Button {
                            Layout.fillWidth: true
                            text: integratedBackend.waitingForKey
                                  ? "Press a key…"
                                  : "Bind: "
                                    + integratedBackend.selectedMobaSkill.keyName
                            onClicked:
                                integratedBackend.beginRebindSelectedMobaSkill()
                        }

                        Label {
                            text: "4. Cast mode"
                            font.bold: true
                        }
                        ComboBox {
                            Layout.fillWidth: true
                            model: ["Follow cursor; release to cast"]
                            currentIndex: 0
                            onActivated: (index) =>
                                integratedBackend.setSelectedMobaSkillMode(index)
                        }

                        Label {
                            text: "5. Start speed"
                            font.bold: true
                        }
                        ComboBox {
                            Layout.fillWidth: true
                            model: [
                                "1 — Stable (120 ms)",
                                "2 — Fast (60 ms)",
                                "3 — Very fast (30 ms)",
                                "4 — Instant (10 ms)",
                                "5 — Superhuman (next loop)"
                            ]
                            currentIndex: Math.max(0,
                                Math.min(4, skillSettings.speedValue - 1))
                            onActivated: (index) => {
                                skillSettings.speedValue = index + 1
                                integratedBackend.setSelectedMobaSkillSpeed(index + 1)
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: !integratedBackend.hasSkillCancel
                              ? "⚠ Skill cancellation unavailable: add MOBA skill cancel from the right-click menu."
                              : (integratedBackend.skillCancel.key === 0
                                 ? "⚠ Skill cancellation unavailable: open the CANCEL control gear and bind a key."
                                 : "✓ Skill cancellation: "
                                   + integratedBackend.skillCancel.keyName)
                        color: integratedBackend.skillCancel.ready
                               ? "#218c4f" : "#b86700"
                        font.bold: true
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: "#d0d5dc"
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: "6. Perspective calibration\n"
                                  + (integratedBackend.selectedMobaSkill.calibrated
                                     ? "✓ Ready — 24/24 points"
                                     : "Required — 24 measured points")
                            color: integratedBackend.selectedMobaSkill.calibrated
                                   ? "#218c4f" : "#b86700"
                            font.bold: true
                        }
                        Button {
                            text: integratedBackend.selectedMobaSkill.calibrated
                                  ? "Recalibrate…" : "Calibrate…"
                            enabled: integratedBackend.hasCharacterCenter
                                     && !integratedBackend.waitingForKey
                            onClicked: calibrationIntro.open()
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: !integratedBackend.hasCharacterCenter
                              ? "Add Character center before calibration."
                              : "Changing the center or diameter clears calibration, because it changes the joystick geometry."
                        color: "#718096"
                    }
                }
            }

            Popup {
                id: calibrationIntro
                anchors.centerIn: Overlay.overlay
                width: Math.min(620, surfaceArea.width - 40)
                height: 390
                modal: true
                focus: true
                closePolicy: Popup.CloseOnEscape

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 13

                    Label {
                        Layout.fillWidth: true
                        text: "MOBA skill calibration"
                        font.bold: true
                        font.pixelSize: 22
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "Сейчас маппер сам зажмёт этот скилл и покажет 24 положения: "
                              + "8 направлений на 33%, 67% и 100% дальности."
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "На каждом шаге кликни ЛКМ точно в КОНЕЦ игрового указателя "
                              + "(туда, куда реально прилетит скилл), а не в центр джойстика. "
                              + "Зелёные точки покажут уже записанные измерения."
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: warningGuide.implicitHeight + 22
                        radius: 7
                        color: "#fff3d6"
                        border.color: "#e0a322"
                        Label {
                            id: warningGuide
                            anchors.fill: parent
                            anchors.margins: 11
                            wrapMode: Text.WordWrap
                            text: "Важно: делай это на пустом тренировочном поле. Враги, автоприцел "
                                  + "и препятствия могут притягивать указатель и испортить сетку. "
                                  + "Esc в любой момент отменит процедуру и вернёт старую калибровку."
                        }
                    }
                    Item { Layout.fillHeight: true }
                    RowLayout {
                        Layout.fillWidth: true
                        Item { Layout.fillWidth: true }
                        Button {
                            text: "Cancel"
                            onClicked: calibrationIntro.close()
                        }
                        Button {
                            text: "Start 24-point calibration"
                            highlighted: true
                            onClicked: {
                                const skillIndex =
                                    integratedBackend.selectedMobaSkillIndex
                                calibrationIntro.close()
                                skillSettings.close()
                                integratedBackend.beginMobaSkillCalibration(
                                    skillIndex)
                            }
                        }
                    }
                }
            }

            Rectangle {
                visible: integratedBackend.calibrationActive
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 12
                width: Math.min(parent.width - 24, 920)
                height: 142
                radius: 10
                color: "#f223182c"
                border.color: "#df9cff"
                z: 300

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 7
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: "Calibrating MOBA skill  •  point "
                                  + (integratedBackend.calibrationStep + 1)
                                  + "/" + integratedBackend.calibrationTotal
                            color: "white"
                            font.bold: true
                            font.pixelSize: 18
                        }
                        Button {
                            text: "Back one point"
                            enabled: integratedBackend.calibrationStep > 0
                            onClicked:
                                integratedBackend.undoMobaSkillCalibrationPoint()
                        }
                        Button {
                            text: "Cancel (Esc)"
                            onClicked:
                                integratedBackend.cancelMobaSkillCalibration()
                        }
                    }
                    ProgressBar {
                        Layout.fillWidth: true
                        from: 0
                        to: integratedBackend.calibrationTotal
                        value: integratedBackend.calibrationStep
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: integratedBackend.calibrationPointReady
                              ? integratedBackend.calibrationInstruction
                              : "Подожди: маппер переводит виртуальный джойстик в следующее положение…"
                        color: "white"
                        font.pixelSize: 15
                    }
                    Label {
                        text: integratedBackend.calibrationPointReady
                              ? "Положение зафиксировано — можно ставить точку."
                              : "Клик временно заблокирован, чтобы случайный двойной клик не испортил профиль."
                        color: "#d6c7df"
                    }
                }
            }

            Popup {
                id: calibrationComplete
                anchors.centerIn: Overlay.overlay
                width: Math.min(520, surfaceArea.width - 40)
                height: 230
                modal: true
                focus: true

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 14
                    Label {
                        text: "✓ Calibration complete"
                        color: "#218c4f"
                        font.bold: true
                        font.pixelSize: 22
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "24 точки записаны. Маппер будет искать курсор внутри "
                              + "измеренной треугольной сетки, а за её границей мягко "
                              + "ограничивать прицел максимальной дальностью."
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "Нажми Done в редакторе, чтобы сохранить профиль."
                        color: "#718096"
                    }
                    Item { Layout.fillHeight: true }
                    Button {
                        Layout.alignment: Qt.AlignRight
                        text: "Got it"
                        onClicked: calibrationComplete.close()
                    }
                }
            }

            Connections {
                target: integratedBackend
                function onEditModeChanged() {
                    if (!integratedBackend.editMode) {
                        addControlMenu.close()
                        bindingSettings.close()
                        cancelSettings.close()
                        skillSettings.close()
                        calibrationIntro.close()
                        calibrationComplete.close()
                    }
                }
                function onMobaSkillCalibrationCompleted(index) {
                    calibrationComplete.open()
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
