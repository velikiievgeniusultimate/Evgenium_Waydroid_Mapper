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
            property real contextTapX: 0
            property real contextTapY: 0
            property string contextControlType: ""
            property int contextControlIndex: -1
            property string contextProfileId: ""
            property string contextProfileName: ""
            property bool contextProfileIsDefault: false

            function openControlMenu(source, mouseX, mouseY, type, index) {
                contextControlType = type
                contextControlIndex = index
                const point = source.mapToItem(surfaceArea, mouseX, mouseY)
                controlContextMenu.x = point.x
                controlContextMenu.y = point.y
                controlContextMenu.open()
            }

            function openControlSettings(type, index) {
                if (type === "tap") {
                    integratedBackend.selectBinding(index)
                    bindingSettings.xValue = integratedBackend.selectedBinding.pixelX
                    bindingSettings.yValue = integratedBackend.selectedBinding.pixelY
                    bindingSettings.modeValue = integratedBackend.selectedBinding.mode
                    bindingSettings.open()
                } else if (type === "center") {
                    characterCenterSettings.xValue =
                        integratedBackend.characterCenter.pixelX
                    characterCenterSettings.yValue =
                        integratedBackend.characterCenter.pixelY
                    characterCenterSettings.open()
                } else if (type === "cancel") {
                    cancelSettings.xValue = integratedBackend.skillCancel.pixelX
                    cancelSettings.yValue = integratedBackend.skillCancel.pixelY
                    cancelSettings.open()
                } else if (type === "movement") {
                    movementSettings.xValue = integratedBackend.mobaMovement.pixelX
                    movementSettings.yValue = integratedBackend.mobaMovement.pixelY
                    movementSettings.thresholdValue =
                        integratedBackend.mobaMovement.holdThresholdMs
                    movementSettings.distanceValue =
                        integratedBackend.mobaMovement.clickDistancePercent
                    movementSettings.open()
                } else if (type === "skill") {
                    integratedBackend.selectMobaSkill(index)
                    skillSettings.loadValues()
                    skillSettings.open()
                }
            }

            function baggageSuggestedName(type, index) {
                if (type === "tap")
                    return "Tap " + integratedBackend.bindings[index].keyName
                if (type === "skill")
                    return "MOBA skill "
                           + integratedBackend.mobaSkills[index].keyName
                if (type === "center")
                    return "Character center"
                if (type === "movement")
                    return "MOBA movement"
                if (type === "cancel")
                    return "Skill cancel "
                           + integratedBackend.skillCancel.keyName
                return "Mapper button"
            }

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
                                                && !integratedBackend.profileManagerVisible
                                                && (!integratedBackend.centerVision.visible
                                                    || integratedBackend.centerVision.tracking)
                                                && !integratedBackend.syntheticTouchActive
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
                                    } else if (integratedBackend.waitingForKey) {
                                        integratedBackend.cancelKeyCapture(true)
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
                                height: circleSize
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
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        cursorShape: Qt.SizeAllCursor
                                        drag.target: marker
                                        drag.minimumX: -marker.width / 2
                                        drag.maximumX: surfaceHost.width - marker.width / 2
                                        drag.minimumY: -circle.height / 2
                                        drag.maximumY: surfaceHost.height - circle.height / 2
                                        onPressed: (mouse) => {
                                            if (mouse.button === Qt.RightButton)
                                                integratedWindow.openControlMenu(
                                                    this, mouse.x, mouse.y,
                                                    "tap", marker.index)
                                        }
                                        onReleased: (mouse) => {
                                            if (mouse.button === Qt.LeftButton)
                                                integratedBackend.moveBinding(
                                                    marker.index,
                                                    (marker.x + marker.width / 2)
                                                    / surfaceHost.width,
                                                    (marker.y + circle.height / 2)
                                                    / surfaceHost.height)
                                        }
                                        onDoubleClicked: (mouse) => {
                                            if (mouse.button === Qt.LeftButton) {
                                                integratedBackend.selectBinding(marker.index)
                                                integratedBackend.beginRebindSelected()
                                            }
                                        }
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
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                cursorShape: Qt.SizeAllCursor
                                onPressed: (mouse) => {
                                    if (mouse.button === Qt.RightButton)
                                        integratedWindow.openControlMenu(
                                            this, mouse.x, mouse.y, "center", -1)
                                }
                                onPositionChanged: (mouse) => {
                                    if ((mouse.buttons & Qt.LeftButton) === 0)
                                        return
                                    const point = mapToItem(surfaceHost,
                                                            mouse.x, mouse.y)
                                    integratedBackend.moveCharacterCenter(
                                        point.x / surfaceHost.width,
                                        point.y / surfaceHost.height)
                                }
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
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    cursorShape: Qt.SizeAllCursor
                                    onPressed: (mouse) => {
                                        if (mouse.button === Qt.RightButton)
                                            integratedWindow.openControlMenu(
                                                this, mouse.x, mouse.y, "cancel", -1)
                                    }
                                    onPositionChanged: (mouse) => {
                                        if ((mouse.buttons & Qt.LeftButton) === 0)
                                            return
                                        const point = mapToItem(surfaceHost,
                                                                mouse.x, mouse.y)
                                        integratedBackend.moveSkillCancel(
                                            point.x / surfaceHost.width,
                                            point.y / surfaceHost.height)
                                    }
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
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    cursorShape: Qt.SizeAllCursor
                                    onPressed: (mouse) => {
                                        if (mouse.button === Qt.RightButton)
                                            integratedWindow.openControlMenu(
                                                this, mouse.x, mouse.y,
                                                "movement", -1)
                                    }
                                    onPositionChanged: (mouse) => {
                                        if ((mouse.buttons & Qt.LeftButton) === 0)
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
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        cursorShape: Qt.SizeAllCursor
                                        drag.target: skillMarker
                                        drag.minimumX: -skillMarker.width / 2
                                        drag.maximumX: surfaceHost.width
                                                       - skillMarker.width / 2
                                        drag.minimumY: -skillMarker.height / 2
                                        drag.maximumY: surfaceHost.height
                                                       - skillMarker.height / 2
                                        onPressed: (mouse) => {
                                            if (mouse.button === Qt.RightButton)
                                                integratedWindow.openControlMenu(
                                                    this, mouse.x, mouse.y,
                                                    "skill", skillMarker.index)
                                        }
                                        onReleased: (mouse) => {
                                            if (mouse.button === Qt.LeftButton)
                                                integratedBackend.moveMobaSkill(
                                                    skillMarker.index,
                                                    (skillMarker.x + skillMarker.width / 2)
                                                    / surfaceHost.width,
                                                    (skillMarker.y + skillMarker.height / 2)
                                                    / surfaceHost.height)
                                        }
                                        onDoubleClicked: (mouse) => {
                                            if (mouse.button === Qt.LeftButton) {
                                                integratedBackend.selectMobaSkill(
                                                    skillMarker.index)
                                                integratedBackend
                                                    .beginRebindSelectedMobaSkill()
                                            }
                                        }
                                    }
                                }

                                Item {
                                    id: artificialRoute
                                    visible: modelData.artificialCenterEnabled
                                             && routeLength > 24 / skillMarker.markerScale
                                    readonly property real startX:
                                        artificialCenter.x + artificialCenter.width / 2
                                    readonly property real startY:
                                        artificialCenter.y + artificialCenter.height / 2
                                    readonly property real endX: skillMarker.width / 2
                                    readonly property real endY: skillMarker.height / 2
                                    readonly property real deltaX: endX - startX
                                    readonly property real deltaY: endY - startY
                                    readonly property real routeLength:
                                        Math.hypot(deltaX, deltaY)
                                    x: startX
                                    y: startY - height / 2
                                    width: routeLength
                                    height: 20 / skillMarker.markerScale
                                    transformOrigin: Item.Left
                                    rotation: Math.atan2(deltaY, deltaX) * 180 / Math.PI
                                    z: 6

                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.right: routeArrow.left
                                        anchors.rightMargin: -2 / skillMarker.markerScale
                                        anchors.verticalCenter: parent.verticalCenter
                                        height: 3 / skillMarker.markerScale
                                        color: "#ff9f43"
                                        border.color: "#803d2100"
                                        border.width: 1 / skillMarker.markerScale
                                    }
                                    Text {
                                        id: routeArrow
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "▶"
                                        color: "#ffb45f"
                                        font.bold: true
                                        font.pixelSize: 18 / skillMarker.markerScale
                                        style: Text.Outline
                                        styleColor: "#b0000000"
                                    }
                                }

                                Item {
                                    id: artificialCenter
                                    visible: modelData.artificialCenterEnabled
                                    width: 38 / skillMarker.markerScale
                                    height: width
                                    z: 8

                                    Binding {
                                        target: artificialCenter
                                        property: "x"
                                        value: modelData.artificialPixelX
                                               * surfaceHost.width
                                               / integratedBackend.androidWidth
                                               - skillMarker.x
                                               - artificialCenter.width / 2
                                        when: !artificialCenterMouse.drag.active
                                        restoreMode: Binding.RestoreNone
                                    }
                                    Binding {
                                        target: artificialCenter
                                        property: "y"
                                        value: modelData.artificialPixelY
                                               * surfaceHost.height
                                               / integratedBackend.androidHeight
                                               - skillMarker.y
                                               - artificialCenter.height / 2
                                        when: !artificialCenterMouse.drag.active
                                        restoreMode: Binding.RestoreNone
                                    }

                                    Rectangle {
                                        anchors.fill: parent
                                        radius: width / 2
                                        color: "#b83d2918"
                                        border.color: "#ffad55"
                                        border.width: 3 / skillMarker.markerScale
                                    }
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 9 / skillMarker.markerScale
                                        height: width
                                        radius: width / 2
                                        color: "#fff1d8"
                                        border.color: "#9e4b00"
                                        border.width: 1 / skillMarker.markerScale
                                    }
                                    Label {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        anchors.bottom: parent.top
                                        anchors.bottomMargin: 3 / skillMarker.markerScale
                                        text: "DOWN"
                                        color: "#ffc078"
                                        font.bold: true
                                        font.pixelSize: 10 / skillMarker.markerScale
                                        style: Text.Outline
                                        styleColor: "#c0000000"
                                    }
                                    MouseArea {
                                        id: artificialCenterMouse
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton
                                        cursorShape: Qt.SizeAllCursor
                                        drag.target: artificialCenter
                                        drag.minimumX: -skillMarker.x
                                                       - artificialCenter.width / 2
                                        drag.maximumX: surfaceHost.width
                                                       - skillMarker.x
                                                       - artificialCenter.width / 2
                                        drag.minimumY: -skillMarker.y
                                                       - artificialCenter.height / 2
                                        drag.maximumY: surfaceHost.height
                                                       - skillMarker.y
                                                       - artificialCenter.height / 2
                                        onPressed: integratedBackend.selectMobaSkill(
                                            skillMarker.index)
                                        onReleased: integratedBackend
                                            .moveMobaSkillArtificialCenter(
                                                skillMarker.index,
                                                (skillMarker.x + artificialCenter.x
                                                 + artificialCenter.width / 2)
                                                / surfaceHost.width,
                                                (skillMarker.y + artificialCenter.y
                                                 + artificialCenter.height / 2)
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

                                Rectangle {
                                    anchors.top: parent.bottom
                                    anchors.topMargin: 8 / skillMarker.markerScale
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: skillWarning.implicitWidth
                                           + 18 / skillMarker.markerScale
                                    height: skillWarning.implicitHeight
                                            + 8 / skillMarker.markerScale
                                    radius: 5 / skillMarker.markerScale
                                    color: modelData.calibrationStale
                                           ? "#e69a2700"
                                           : (modelData.ready ? "#d9267044" : "#e69a2700")
                                    border.color: modelData.calibrationStale
                                                  ? "#ffb020"
                                                  : (modelData.ready
                                                     ? "#72f2a4" : "#ffb020")

                                    Label {
                                        id: skillWarning
                                        anchors.centerIn: parent
                                        text: modelData.calibrationStale
                                              ? "⚠ CALIBRATION CHANGED"
                                              : (!integratedBackend.hasCharacterCenter
                                              ? "⚠ REQUIRES CHARACTER CENTER"
                                              : (modelData.key === 0
                                                 ? "⚠ CHOOSE A BIND"
                                                 : (modelData.calibrated
                                                    ? "✓ CALIBRATED"
                                                    : "⚠ CALIBRATION REQUIRED")))
                                        color: "white"
                                        font.bold: true
                                        font.pixelSize: 11 / skillMarker.markerScale
                                    }
                                }
                            }
                        }

                        Item {
                            id: calibrationOverlay
                            property real hoverX: integratedBackend.characterCenter.x
                            property real hoverY: integratedBackend.characterCenter.y
                            anchors.fill: parent
                            visible: integratedBackend.calibrationActive
                            z: 90

                            Rectangle {
                                anchors.fill: parent
                                color: "#10000000"
                            }

                            Rectangle {
                                readonly property real dx:
                                    (calibrationOverlay.hoverX
                                     - integratedBackend.characterCenter.x)
                                    * surfaceHost.width
                                readonly property real dy:
                                    (calibrationOverlay.hoverY
                                     - integratedBackend.characterCenter.y)
                                    * surfaceHost.height
                                visible: integratedBackend.calibrationPointReady
                                x: integratedBackend.characterCenter.x
                                   * surfaceHost.width
                                y: integratedBackend.characterCenter.y
                                   * surfaceHost.height
                                width: Math.sqrt(dx * dx + dy * dy)
                                height: 3 / Math.max(surfaceHost.scale, 0.01)
                                transformOrigin: Item.Left
                                rotation: Math.atan2(dy, dx) * 180 / Math.PI
                                color: "#ffd15c"
                                opacity: 0.9
                            }

                            Repeater {
                                model: integratedBackend.calibrationPoints
                                Item {
                                    required property var modelData
                                    readonly property real pointSize:
                                        13 / Math.max(surfaceHost.scale, 0.01)
                                    anchors.fill: parent

                                    Rectangle {
                                        readonly property real dx:
                                            (modelData.x - modelData.centerX)
                                            * surfaceHost.width
                                        readonly property real dy:
                                            (modelData.y - modelData.centerY)
                                            * surfaceHost.height
                                        x: modelData.centerX * surfaceHost.width
                                        y: modelData.centerY * surfaceHost.height
                                        width: Math.sqrt(dx * dx + dy * dy)
                                        height: 2 / Math.max(surfaceHost.scale, 0.01)
                                        transformOrigin: Item.Left
                                        rotation: Math.atan2(dy, dx) * 180 / Math.PI
                                        color: modelData.ring === 0
                                               ? "#dba6ff" : "#587dff"
                                        opacity: modelData.ring === 0 ? 0.78 : 0.38
                                    }
                                    Rectangle {
                                        x: modelData.x * surfaceHost.width
                                           - width / 2
                                        y: modelData.y * surfaceHost.height
                                           - height / 2
                                        width: pointSize
                                        height: pointSize
                                        radius: width / 2
                                        color: modelData.ring === 0
                                               ? "#df9cff" : "#72f2a4"
                                        border.color: "white"
                                        border.width:
                                            2 / Math.max(surfaceHost.scale, 0.01)
                                    }
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
                                onPositionChanged: (mouse) => {
                                    calibrationOverlay.hoverX = mouse.x / width
                                    calibrationOverlay.hoverY = mouse.y / height
                                }
                            }
                        }

                        Item {
                            id: centerVisionCanvas
                            anchors.fill: parent
                            visible: integratedBackend.centerVision.visible
                            z: 160
                            property real selectionStartX: 0
                            property real selectionStartY: 0
                            property real selectionCurrentX: 0
                            property real selectionCurrentY: 0
                            property bool selecting: false

                            Image {
                                anchors.fill: parent
                                visible: integratedBackend.centerVision.frameFrozen
                                         && integratedBackend.centerVision.frameSource.toString().length > 0
                                source: integratedBackend.centerVision.frameSource
                                cache: false
                                fillMode: Image.Stretch
                            }

                            Rectangle {
                                visible: integratedBackend.centerVision.matchRect.width > 0
                                         && integratedBackend.centerVision.matchRect.height > 0
                                x: integratedBackend.centerVision.matchRect.x
                                   * centerVisionCanvas.width
                                y: integratedBackend.centerVision.matchRect.y
                                   * centerVisionCanvas.height
                                width: integratedBackend.centerVision.matchRect.width
                                       * centerVisionCanvas.width
                                height: integratedBackend.centerVision.matchRect.height
                                        * centerVisionCanvas.height
                                color: "#1626d980"
                                border.color: integratedBackend.centerVision.trackingState === "LOCKED"
                                              ? "#48f29a" : "#ffbd52"
                                border.width: 3 / Math.max(surfaceHost.scale, 0.01)
                            }

                            Rectangle {
                                readonly property real fromX:
                                    (integratedBackend.centerVision.matchRect.x
                                     + integratedBackend.centerVision.matchRect.width / 2)
                                    * centerVisionCanvas.width
                                readonly property real fromY:
                                    (integratedBackend.centerVision.matchRect.y
                                     + integratedBackend.centerVision.matchRect.height / 2)
                                    * centerVisionCanvas.height
                                readonly property real dx:
                                    integratedBackend.centerVision.trackedCenter.x
                                    * centerVisionCanvas.width - fromX
                                readonly property real dy:
                                    integratedBackend.centerVision.trackedCenter.y
                                    * centerVisionCanvas.height - fromY
                                visible: integratedBackend.centerVision.hasReference
                                x: fromX
                                y: fromY
                                width: Math.hypot(dx, dy)
                                height: 2 / Math.max(surfaceHost.scale, 0.01)
                                transformOrigin: Item.Left
                                rotation: Math.atan2(dy, dx) * 180 / Math.PI
                                color: "#77e9b0"
                                opacity: 0.72
                            }

                            Rectangle {
                                readonly property real dotSize:
                                    13 / Math.max(surfaceHost.scale, 0.01)
                                visible: integratedBackend.centerVision.tracking
                                x: integratedBackend.centerVision.rawCenter.x
                                   * centerVisionCanvas.width - dotSize / 2
                                y: integratedBackend.centerVision.rawCenter.y
                                   * centerVisionCanvas.height - dotSize / 2
                                width: dotSize
                                height: dotSize
                                radius: width / 2
                                color: "#ff9f43"
                                border.color: "white"
                                border.width: 2 / Math.max(surfaceHost.scale, 0.01)
                            }

                            Item {
                                id: centerVisionMarker
                                readonly property real markerScale:
                                    Math.max(surfaceHost.scale, 0.01)
                                readonly property real markerSize: 62 / markerScale
                                visible: integratedBackend.centerVision.hasReference
                                x: integratedBackend.centerVision.trackedCenter.x
                                   * centerVisionCanvas.width - markerSize / 2
                                y: integratedBackend.centerVision.trackedCenter.y
                                   * centerVisionCanvas.height - markerSize / 2
                                width: markerSize
                                height: markerSize

                                Rectangle {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: 3 / centerVisionMarker.markerScale
                                    color: integratedBackend.centerVision.trackingState === "LOST"
                                           ? "#ff4f64" : "#55f5a0"
                                }
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    height: 3 / centerVisionMarker.markerScale
                                    color: integratedBackend.centerVision.trackingState === "LOST"
                                           ? "#ff4f64" : "#55f5a0"
                                }
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 10 / centerVisionMarker.markerScale
                                    height: width
                                    radius: width / 2
                                    color: "white"
                                    border.color: "#16212b"
                                    border.width: 2 / centerVisionMarker.markerScale
                                }
                            }

                            Rectangle {
                                visible: centerVisionCanvas.selecting
                                x: Math.min(centerVisionCanvas.selectionStartX,
                                            centerVisionCanvas.selectionCurrentX)
                                y: Math.min(centerVisionCanvas.selectionStartY,
                                            centerVisionCanvas.selectionCurrentY)
                                width: Math.abs(centerVisionCanvas.selectionCurrentX
                                                - centerVisionCanvas.selectionStartX)
                                height: Math.abs(centerVisionCanvas.selectionCurrentY
                                                 - centerVisionCanvas.selectionStartY)
                                color: "#263da9ff"
                                border.color: "#62d7ff"
                                border.width: 3 / Math.max(surfaceHost.scale, 0.01)
                            }

                            MouseArea {
                                anchors.fill: parent
                                enabled: integratedBackend.centerVision.stage === 1
                                         || integratedBackend.centerVision.stage === 2
                                         || integratedBackend.centerVision.stage === 5
                                acceptedButtons: Qt.AllButtons
                                hoverEnabled: true
                                preventStealing: true
                                cursorShape: integratedBackend.centerVision.stage === 1
                                             || integratedBackend.centerVision.stage === 2
                                             || integratedBackend.centerVision.stage === 5
                                             ? Qt.CrossCursor : Qt.ArrowCursor
                                onPressed: (mouse) => {
                                    if (mouse.button !== Qt.LeftButton)
                                        return
                                    if (integratedBackend.centerVision.stage === 1) {
                                        centerVisionCanvas.selectionStartX = mouse.x
                                        centerVisionCanvas.selectionStartY = mouse.y
                                        centerVisionCanvas.selectionCurrentX = mouse.x
                                        centerVisionCanvas.selectionCurrentY = mouse.y
                                        centerVisionCanvas.selecting = true
                                    } else if (integratedBackend.centerVision.stage === 2) {
                                        integratedBackend.centerVision.setAnchorPoint(
                                            mouse.x / width, mouse.y / height)
                                    } else if (integratedBackend.centerVision.stage === 5) {
                                        integratedBackend.centerVision.setCorrectionPoint(
                                            mouse.x / width, mouse.y / height)
                                    }
                                }
                                onPositionChanged: (mouse) => {
                                    if (!centerVisionCanvas.selecting)
                                        return
                                    centerVisionCanvas.selectionCurrentX =
                                        Math.max(0, Math.min(width, mouse.x))
                                    centerVisionCanvas.selectionCurrentY =
                                        Math.max(0, Math.min(height, mouse.y))
                                }
                                onReleased: (mouse) => {
                                    if (!centerVisionCanvas.selecting)
                                        return
                                    centerVisionCanvas.selecting = false
                                    integratedBackend.centerVision.setTemplateSelection(
                                        centerVisionCanvas.selectionStartX / width,
                                        centerVisionCanvas.selectionStartY / height,
                                        (centerVisionCanvas.selectionCurrentX
                                         - centerVisionCanvas.selectionStartX) / width,
                                        (centerVisionCanvas.selectionCurrentY
                                         - centerVisionCanvas.selectionStartY) / height)
                                }
                                onCanceled: centerVisionCanvas.selecting = false
                            }
                        }
                    }
                }
            }

            Rectangle {
                id: centerVisionPanel
                property real panelMargin: 12
                property real dragOffsetX: 0
                property real dragOffsetY: 0
                function fitInside() {
                    x = Math.max(panelMargin,
                                 Math.min(x, integratedWindow.width - width - panelMargin))
                    y = Math.max(panelMargin,
                                 Math.min(y, integratedWindow.height - height - panelMargin))
                }
                width: Math.min(460, integratedWindow.width - panelMargin * 2)
                height: Math.min(610, integratedWindow.height - panelMargin * 2)
                x: Math.max(panelMargin,
                            integratedWindow.width - width - panelMargin)
                y: panelMargin
                visible: integratedBackend.centerVision.visible
                color: "#f51a2430"
                border.color: "#5bdfaa"
                border.width: 2
                radius: 14
                clip: true
                z: 230
                onVisibleChanged: {
                    if (visible)
                        Qt.callLater(fitInside)
                }

                Rectangle {
                    id: centerVisionHeader
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 54
                    color: "#243746"

                    Label {
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Поиск центра  •  EXPERIMENTAL  •  F2"
                        color: "white"
                        font.bold: true
                        font.pixelSize: 16
                    }
                    Button {
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: "✕"
                        flat: true
                        onClicked: integratedBackend.centerVision.close()
                    }
                    MouseArea {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.rightMargin: 52
                        cursorShape: Qt.SizeAllCursor
                        onPressed: (mouse) => {
                            centerVisionPanel.dragOffsetX = mouse.x
                            centerVisionPanel.dragOffsetY = mouse.y
                        }
                        onPositionChanged: (mouse) => {
                            if (!pressed)
                                return
                            const point = mapToItem(integratedWindow, mouse.x, mouse.y)
                            centerVisionPanel.x = Math.max(centerVisionPanel.panelMargin,
                                Math.min(integratedWindow.width - centerVisionPanel.width
                                         - centerVisionPanel.panelMargin,
                                         point.x - centerVisionPanel.dragOffsetX))
                            centerVisionPanel.y = Math.max(centerVisionPanel.panelMargin,
                                Math.min(integratedWindow.height - centerVisionPanel.height
                                         - centerVisionPanel.panelMargin,
                                         point.y - centerVisionPanel.dragOffsetY))
                        }
                    }
                }

                ScrollView {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: centerVisionHeader.bottom
                    anchors.bottom: parent.bottom
                    clip: true

                    ColumnLayout {
                        width: centerVisionPanel.width - 30
                        spacing: 10

                        Label {
                            Layout.fillWidth: true
                            Layout.topMargin: 12
                            text: integratedBackend.centerVision.status
                            color: "#e7f0f7"
                            wrapMode: Text.WordWrap
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 76
                            radius: 8
                            color: "#18232d"
                            border.color: "#3e5365"

                            GridLayout {
                                anchors.fill: parent
                                anchors.margins: 9
                                columns: 2
                                columnSpacing: 16
                                rowSpacing: 3
                                Label {
                                    text: "Этап: " + integratedBackend.centerVision.stageName
                                    color: "#b9cbda"
                                    font.bold: true
                                }
                                Label {
                                    text: "Состояние: "
                                          + integratedBackend.centerVision.trackingState
                                    color: integratedBackend.centerVision.trackingState === "LOCKED"
                                           ? "#60e9a0" : "#ffbf5a"
                                    font.bold: true
                                }
                                Label {
                                    text: "Совпадение: "
                                          + (integratedBackend.centerVision.score * 100).toFixed(1)
                                          + "%"
                                    color: "#dbe7ef"
                                }
                                Label {
                                    text: "Уверенность: "
                                          + (integratedBackend.centerVision.confidence * 100).toFixed(1)
                                          + "%"
                                    color: "#dbe7ef"
                                }
                                Label {
                                    text: "Кадр: " + integratedBackend.centerVision.frameNumber
                                    color: "#91a8ba"
                                }
                                Label {
                                    text: "Анализ: "
                                          + integratedBackend.centerVision.analysisFps.toFixed(1)
                                          + " FPS"
                                    color: "#91a8ba"
                                }
                            }
                        }

                        Button {
                            Layout.fillWidth: true
                            text: "1. Заморозить новый кадр"
                            onClicked: integratedBackend.centerVision.captureReference()
                        }
                        Label {
                            Layout.fillWidth: true
                            text: "На замороженном кадре обведи HP-полоску вместе с "
                                  + "её рамкой, уровнем/именем и стабильными значками. "
                                  + "Затем кликни в настоящий центр персонажа на земле."
                            color: "#9eb2c2"
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Button {
                                Layout.fillWidth: true
                                text: integratedBackend.centerVision.tracking
                                      ? "Остановить слежение" : "Начать слежение"
                                enabled: integratedBackend.centerVision.hasReference
                                onClicked: integratedBackend.centerVision.tracking
                                           ? integratedBackend.centerVision.stopTracking()
                                           : integratedBackend.centerVision.startTracking()
                            }
                            Button {
                                text: "✓ Хорошо"
                                enabled: integratedBackend.centerVision.frameNumber > 0
                                onClicked: integratedBackend.centerVision.markGood()
                            }
                        }
                        Button {
                            Layout.fillWidth: true
                            text: "✕ Центр неверный — показать правильный"
                            enabled: integratedBackend.centerVision.frameNumber > 0
                            onClicked: integratedBackend.centerVision.beginCorrection()
                        }

                        Label {
                            text: "Порог совпадения: "
                                  + (integratedBackend.centerVision.threshold * 100).toFixed(0)
                                  + "%"
                            color: "#c9d8e3"
                        }
                        Slider {
                            Layout.fillWidth: true
                            from: 0.35
                            to: 0.95
                            stepSize: 0.01
                            value: integratedBackend.centerVision.threshold
                            onMoved: integratedBackend.centerVision.setThreshold(value)
                        }

                        Label {
                            Layout.fillWidth: true
                            text: "Лаборатория пока только измеряет и пишет данные. "
                                  + "Она не двигает центр рабочего маппера — это защита "
                                  + "от экспериментальных ошибок."
                            color: "#ffcc72"
                            wrapMode: Text.WordWrap
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Button {
                                Layout.fillWidth: true
                                text: "Открыть логи"
                                onClicked: integratedBackend.centerVision.openSessionFolder()
                            }
                            Button {
                                Layout.fillWidth: true
                                text: "Собрать пакет"
                                onClicked: integratedBackend.centerVision.exportDiagnostics()
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.bottomMargin: 12
                            text: integratedBackend.centerVision.sessionDirectory
                            color: "#71889b"
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }

            Connections {
                target: integratedWindow
                function onWidthChanged() { centerVisionPanel.fitInside() }
                function onHeightChanged() { centerVisionPanel.fitInside() }
            }

            Rectangle {
                id: profileInputShield
                anchors.fill: parent
                visible: integratedBackend.profileManagerVisible
                color: "#59071018"
                z: 180

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.AllButtons
                    hoverEnabled: true
                    preventStealing: true
                }
            }

            Rectangle {
                id: profilePanel
                property real panelMargin: 12
                property real minimumPanelWidth:
                    Math.min(520, Math.max(1, surfaceArea.width - panelMargin * 2))
                property real minimumPanelHeight:
                    Math.min(300, Math.max(1, surfaceArea.height - panelMargin * 2))
                function fitInside() {
                    const maximumWidth = Math.max(1,
                        surfaceArea.width - panelMargin * 2)
                    const maximumHeight = Math.max(1,
                        surfaceArea.height - panelMargin * 2)
                    width = Math.max(Math.min(minimumPanelWidth, maximumWidth),
                                     Math.min(width, maximumWidth))
                    height = Math.max(Math.min(minimumPanelHeight, maximumHeight),
                                      Math.min(height, maximumHeight))
                    x = Math.max(panelMargin,
                                 Math.min(x, surfaceArea.width - width - panelMargin))
                    y = Math.max(panelMargin,
                                 Math.min(y, surfaceArea.height - height - panelMargin))
                }

                x: Math.max(panelMargin, (surfaceArea.width - width) / 2)
                y: Math.max(panelMargin, (surfaceArea.height - height) / 2)
                width: Math.min(760, surfaceArea.width - panelMargin * 2)
                height: Math.min(330, surfaceArea.height - panelMargin * 2)
                visible: integratedBackend.profileManagerVisible
                color: "#f51a2430"
                border.color: "#73879b"
                border.width: 2
                radius: 14
                clip: true
                z: 190
                onVisibleChanged: {
                    if (visible)
                        Qt.callLater(fitInside)
                }

                Rectangle {
                    id: profileHeader
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 52
                    color: "#263746"

                    Label {
                        anchors.left: parent.left
                        anchors.leftMargin: 18
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Profiles  •  F6"
                        color: "white"
                        font.bold: true
                        font.pixelSize: 18
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
                            const point = mapToItem(surfaceArea, mouse.x, mouse.y)
                            profilePanel.x = Math.max(profilePanel.panelMargin,
                                Math.min(surfaceArea.width - profilePanel.width
                                         - profilePanel.panelMargin,
                                         point.x - grabX))
                            profilePanel.y = Math.max(profilePanel.panelMargin,
                                Math.min(surfaceArea.height - profilePanel.height
                                         - profilePanel.panelMargin,
                                         point.y - grabY))
                        }
                    }

                    Button {
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: 38
                        height: 36
                        z: 2
                        text: "×"
                        font.bold: true
                        font.pixelSize: 20
                        onClicked: integratedBackend.closeProfileManager()
                    }
                }

                GridView {
                    id: profileList
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.top: profileHeader.bottom
                    anchors.topMargin: 10
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 18
                    cellWidth: 132
                    cellHeight: 148
                    flow: GridView.FlowLeftToRight
                    clip: true
                    model: integratedBackend.profiles

                    delegate: Item {
                        id: profileCell
                        required property var modelData
                        required property int index
                        width: profileList.cellWidth
                        height: profileList.cellHeight

                        Rectangle {
                            id: profileAvatar
                            anchors.top: parent.top
                            anchors.topMargin: 4
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 94
                            height: width
                            radius: width / 2
                            color: "#334c60"
                            border.color: modelData.active ? "#42db7b"
                                          : (modelData.supported ? "#91a4b7" : "#e4b63f")
                            border.width: modelData.active ? 5 : 3
                            clip: true

                            Image {
                                anchors.fill: parent
                                anchors.margins: 5
                                source: modelData.imageUrl
                                fillMode: Image.Stretch
                                visible: modelData.imageUrl !== ""
                                asynchronous: true
                            }
                            Label {
                                anchors.centerIn: parent
                                text: modelData.letter === "" ? "?" : modelData.letter
                                visible: modelData.imageUrl === ""
                                color: "white"
                                font.bold: true
                                font.pixelSize: profileAvatar.width * 0.42
                            }
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                cursorShape: Qt.PointingHandCursor
                                onClicked: (mouse) => {
                                    if (mouse.button === Qt.LeftButton)
                                        integratedBackend.selectProfile(modelData.id)
                                }
                                onPressed: (mouse) => {
                                    if (mouse.button !== Qt.RightButton)
                                        return
                                    integratedWindow.contextProfileId = modelData.id
                                    integratedWindow.contextProfileName = modelData.name
                                    integratedWindow.contextProfileIsDefault =
                                        modelData.isDefault
                                    const point = mapToItem(surfaceArea,
                                                            mouse.x, mouse.y)
                                    profileContextMenu.x = point.x
                                    profileContextMenu.y = point.y
                                    profileContextMenu.open()
                                }
                            }
                        }

                        Label {
                            anchors.top: profileAvatar.bottom
                            anchors.topMargin: 6
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 5
                            anchors.rightMargin: 5
                            text: modelData.name
                            horizontalAlignment: Text.AlignHCenter
                            color: "white"
                            font.bold: true
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }

                        Label {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 3
                            text: modelData.supported
                                  ? (modelData.active ? "ACTIVE" : modelData.statusText)
                                  : "⚠ " + modelData.statusText
                            horizontalAlignment: Text.AlignHCenter
                            color: modelData.supported ? "#9fc2d8" : "#ffd66b"
                            font.pixelSize: 9
                            elide: Text.ElideRight
                        }
                    }
                }

                Button {
                    id: createProfileButton
                    anchors.right: parent.right
                    anchors.rightMargin: 22
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 22
                    width: 52
                    height: 52
                    z: 4
                    text: "+"
                    font.bold: true
                    font.pixelSize: 30
                    onClicked: integratedBackend.createProfile()
                    ToolTip.visible: hovered
                    ToolTip.text: "Create a blank profile for the current resolution"
                }

                Item {
                    id: profileResizeHandle
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    width: 30
                    height: 30
                    z: 5

                    Text {
                        anchors.centerIn: parent
                        text: "◢"
                        color: "#b8cad8"
                        font.pixelSize: 20
                    }
                    MouseArea {
                        property real startMouseX: 0
                        property real startMouseY: 0
                        property real startWidth: 0
                        property real startHeight: 0
                        anchors.fill: parent
                        cursorShape: Qt.SizeFDiagCursor
                        onPressed: (mouse) => {
                            const point = mapToItem(surfaceArea, mouse.x, mouse.y)
                            startMouseX = point.x
                            startMouseY = point.y
                            startWidth = profilePanel.width
                            startHeight = profilePanel.height
                        }
                        onPositionChanged: (mouse) => {
                            if (!pressed)
                                return
                            const point = mapToItem(surfaceArea, mouse.x, mouse.y)
                            const maximumWidth = surfaceArea.width - profilePanel.x
                                                       - profilePanel.panelMargin
                            const maximumHeight = surfaceArea.height - profilePanel.y
                                                        - profilePanel.panelMargin
                            profilePanel.width = Math.max(profilePanel.minimumPanelWidth,
                                Math.min(maximumWidth,
                                         startWidth + point.x - startMouseX))
                            profilePanel.height = Math.max(profilePanel.minimumPanelHeight,
                                Math.min(maximumHeight,
                                         startHeight + point.y - startMouseY))
                        }
                    }
                }
            }

            Connections {
                target: surfaceArea
                function onWidthChanged() { profilePanel.fitInside() }
                function onHeightChanged() { profilePanel.fitInside() }
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
                        text: ((integratedBackend.hasMobaMovement
                                || integratedBackend.hasMobaSkills)
                               && !integratedBackend.hasCharacterCenter)
                              ? "Editing mapper  •  ⚠ MOBA controls need Character center"
                              : "Editing mapper"
                        color: "white"
                        font.bold: true
                        font.pixelSize: 17
                        wrapMode: Text.WordWrap
                    }
                    Button {
                        text: "Done"
                        onClicked: integratedBackend.toggleEditMode()
                    }
                }
            }

            Rectangle {
                visible: integratedBackend.cursorLocked
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 12
                width: cursorLockLabel.implicitWidth + 22
                height: 34
                radius: 8
                color: "#e61b5131"
                border.color: "#62e899"
                z: 105
                Label {
                    id: cursorLockLabel
                    anchors.centerIn: parent
                    text: "CURSOR LOCKED  •  F12"
                    color: "white"
                    font.bold: true
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
                MenuSeparator { }
                Menu {
                    id: baggageInsertMenu
                    title: integratedBackend.baggageItems.length > 0
                           ? "Кнопка из багажа"
                           : "Кнопка из багажа (пусто)"
                    enabled: integratedBackend.baggageItems.length > 0

                    Instantiator {
                        model: integratedBackend.baggageItems
                        delegate: MenuItem {
                            required property var modelData
                            text: modelData.name + "  •  " + modelData.typeName
                            onTriggered: integratedBackend.insertBaggageItem(
                                modelData.id,
                                integratedWindow.contextTapX,
                                integratedWindow.contextTapY)
                        }
                        onObjectAdded: (index, object) =>
                            baggageInsertMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) =>
                            baggageInsertMenu.removeItem(object)
                    }
                }
                Menu {
                    id: baggageDeleteMenu
                    title: "Удалить из багажа"
                    enabled: integratedBackend.baggageItems.length > 0

                    Instantiator {
                        model: integratedBackend.baggageItems
                        delegate: MenuItem {
                            required property var modelData
                            text: modelData.name + "  •  " + modelData.typeName
                            onTriggered:
                                integratedBackend.deleteBaggageItem(modelData.id)
                        }
                        onObjectAdded: (index, object) =>
                            baggageDeleteMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) =>
                            baggageDeleteMenu.removeItem(object)
                    }
                }
            }

            Menu {
                id: controlContextMenu
                enabled: integratedBackend.editMode

                MenuItem {
                    text: "Настройки"
                    onTriggered: integratedWindow.openControlSettings(
                        integratedWindow.contextControlType,
                        integratedWindow.contextControlIndex)
                }
                MenuItem {
                    text: "Сделать копию"
                    enabled: integratedWindow.contextControlType === "tap"
                             || integratedWindow.contextControlType === "skill"
                    onTriggered: {
                        if (integratedWindow.contextControlType === "tap")
                            integratedBackend.duplicateBinding(
                                integratedWindow.contextControlIndex)
                        else if (integratedWindow.contextControlType === "skill")
                            integratedBackend.duplicateMobaSkill(
                                integratedWindow.contextControlIndex)
                    }
                }
                MenuItem {
                    text: "В багаж"
                    onTriggered: {
                        baggageNamePopup.controlType =
                            integratedWindow.contextControlType
                        baggageNamePopup.controlIndex =
                            integratedWindow.contextControlIndex
                        baggageNameField.text =
                            integratedWindow.baggageSuggestedName(
                                baggageNamePopup.controlType,
                                baggageNamePopup.controlIndex)
                        baggageNamePopup.open()
                        baggageNameField.selectAll()
                        baggageNameField.forceActiveFocus()
                    }
                }
                MenuItem {
                    text: "Удалить"
                    onTriggered: {
                        const type = integratedWindow.contextControlType
                        const index = integratedWindow.contextControlIndex
                        if (type === "tap")
                            integratedBackend.removeBinding(index)
                        else if (type === "center")
                            integratedBackend.removeCharacterCenter()
                        else if (type === "cancel")
                            integratedBackend.removeSkillCancel()
                        else if (type === "movement")
                            integratedBackend.removeMobaMovement()
                        else if (type === "skill")
                            integratedBackend.removeMobaSkill(index)
                    }
                }
            }

            Popup {
                id: baggageNamePopup
                property string controlType: ""
                property int controlIndex: -1
                function saveItem() {
                    const name = baggageNameField.text.trim()
                    if (name.length === 0)
                        return
                    integratedBackend.storeControlInBaggage(
                        controlType, controlIndex, name)
                    close()
                }
                anchors.centerIn: Overlay.overlay
                width: Math.min(470, surfaceArea.width - 40)
                height: 210
                modal: true
                focus: true
                closePolicy: Popup.CloseOnEscape

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12
                    Label {
                        Layout.fillWidth: true
                        text: "Сохранить кнопку в багаж"
                        font.bold: true
                        font.pixelSize: 20
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "Багаж доступен во всех профилях и разрешениях. "
                              + "Исходная кнопка останется на месте."
                        color: "#718096"
                    }
                    TextField {
                        id: baggageNameField
                        Layout.fillWidth: true
                        placeholderText: "Название кнопки"
                        onAccepted: baggageNamePopup.saveItem()
                    }
                    Item { Layout.fillHeight: true }
                    RowLayout {
                        Layout.fillWidth: true
                        Item { Layout.fillWidth: true }
                        Button {
                            text: "Отмена"
                            onClicked: baggageNamePopup.close()
                        }
                        Button {
                            id: baggageSaveButton
                            text: "В багаж"
                            highlighted: true
                            enabled: baggageNameField.text.trim().length > 0
                            onClicked: baggageNamePopup.saveItem()
                        }
                    }
                }
            }

            Menu {
                id: profileContextMenu

                MenuItem {
                    text: "Выбрать изображение"
                    onTriggered: {
                        profileImageDialog.profileId =
                            integratedWindow.contextProfileId
                        profileImageDialog.open()
                    }
                }
                MenuItem {
                    text: "Сделать копию"
                    onTriggered: integratedBackend.duplicateProfile(
                        integratedWindow.contextProfileId)
                }
                MenuItem {
                    text: "Переименовать профиль"
                    enabled: !integratedWindow.contextProfileIsDefault
                    onTriggered: {
                        renameProfilePopup.profileId =
                            integratedWindow.contextProfileId
                        renameProfilePopup.profileName =
                            integratedWindow.contextProfileName
                        renameField.text = integratedWindow.contextProfileName
                        renameProfilePopup.open()
                        renameField.forceActiveFocus()
                    }
                }
                MenuSeparator {}
                MenuItem {
                    text: "Удалить профиль"
                    enabled: !integratedWindow.contextProfileIsDefault
                    onTriggered: {
                        deleteProfilePopup.profileId =
                            integratedWindow.contextProfileId
                        deleteProfilePopup.profileName =
                            integratedWindow.contextProfileName
                        deleteProfilePopup.open()
                    }
                }
            }

            FileDialog {
                id: profileImageDialog
                property string profileId: ""
                title: "Choose profile image"
                fileMode: FileDialog.OpenFile
                nameFilters: ["Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif)"]
                onAccepted: {
                    integratedBackend.setProfileImage(profileId, selectedFile)
                }
            }

            Popup {
                id: renameProfilePopup
                property string profileId: ""
                property string profileName: ""
                anchors.centerIn: Overlay.overlay
                width: Math.min(390, surfaceArea.width - 32)
                height: 190
                modal: true
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12
                    Label {
                        text: "Rename profile"
                        font.bold: true
                        font.pixelSize: 19
                    }
                    TextField {
                        id: renameField
                        Layout.fillWidth: true
                        maximumLength: 64
                        placeholderText: "Profile name"
                        onAccepted: {
                            integratedBackend.renameProfile(
                                renameProfilePopup.profileId, text)
                            renameProfilePopup.close()
                        }
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        Button {
                            text: "Cancel"
                            onClicked: renameProfilePopup.close()
                        }
                        Button {
                            text: "Save"
                            enabled: renameField.text.trim().length > 0
                            onClicked: {
                                integratedBackend.renameProfile(
                                    renameProfilePopup.profileId, renameField.text)
                                renameProfilePopup.close()
                            }
                        }
                    }
                }
            }

            Popup {
                id: deleteProfilePopup
                property string profileId: ""
                property string profileName: ""
                anchors.centerIn: Overlay.overlay
                width: Math.min(450, surfaceArea.width - 32)
                height: 225
                modal: true
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12
                    Label {
                        Layout.fillWidth: true
                        text: "Удалить профиль?"
                        font.bold: true
                        font.pixelSize: 19
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        wrapMode: Text.WordWrap
                        text: "Профиль «" + deleteProfilePopup.profileName
                              + "» и все его варианты разрешений будут удалены. "
                              + "Это действие нельзя отменить."
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        Button {
                            text: "Отмена"
                            onClicked: deleteProfilePopup.close()
                        }
                        Button {
                            text: "Удалить"
                            highlighted: true
                            onClicked: {
                                integratedBackend.deleteProfile(
                                    deleteProfilePopup.profileId)
                                deleteProfilePopup.close()
                            }
                        }
                    }
                }
            }

            Popup {
                id: profileAdaptationPopup
                anchors.centerIn: Overlay.overlay
                width: Math.min(560, surfaceArea.width - 36)
                height: integratedBackend.pendingProfile.isDefault ? 245 : 340
                modal: true
                focus: true
                closePolicy: Popup.NoAutoClose

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12
                    Label {
                        Layout.fillWidth: true
                        text: integratedBackend.pendingProfile.name || "Profile"
                        font.bold: true
                        font.pixelSize: 20
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: integratedBackend.pendingProfile.isDefault
                              ? "Default is permanently fixed to "
                                + integratedBackend.pendingProfile.sourceResolution
                                + ". It cannot migrate to "
                                + integratedBackend.pendingProfile.targetResolution
                                + ". Switch Android resolution or choose another profile."
                              : "This profile has not been configured for "
                                + integratedBackend.pendingProfile.targetResolution
                                + ". Its closest existing variant is "
                                + integratedBackend.pendingProfile.sourceResolution + "."
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: !integratedBackend.pendingProfile.isDefault
                        wrapMode: Text.WordWrap
                        text: "Automatic adaptation copies every normalized position, size, "
                              + "bind and calibration proportionally. Build from scratch creates "
                              + "an empty variant for manual setup."
                        color: "#718096"
                    }
                    Item { Layout.fillHeight: true }
                    RowLayout {
                        Layout.fillWidth: true
                        layoutDirection: Qt.RightToLeft
                        Button {
                            text: "Cancel"
                            onClicked: {
                                integratedBackend.cancelPendingProfileSwitch()
                                profileAdaptationPopup.close()
                            }
                        }
                        Button {
                            visible: !integratedBackend.pendingProfile.isDefault
                            text: "Build from scratch"
                            onClicked: {
                                integratedBackend.createPendingProfileFromScratch()
                                profileAdaptationPopup.close()
                            }
                        }
                        Button {
                            visible: integratedBackend.pendingProfile.canAdapt === true
                            text: "Adapt automatically"
                            onClicked: {
                                integratedBackend.adaptPendingProfileAutomatically()
                                profileAdaptationPopup.close()
                            }
                        }
                    }
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
                id: characterCenterSettings
                property int xValue: 0
                property int yValue: 0
                x: Math.max(0, (surfaceArea.width - width) / 2)
                y: Math.max(0, (surfaceArea.height - height) / 2)
                width: 380
                height: 215
                modal: false
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        color: "#512633"
                        radius: 6
                        Label {
                            anchors.centerIn: parent
                            text: "Character center settings  •  drag this header"
                            color: "white"
                            font.bold: true
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
                                const point = mapToItem(surfaceArea, mouse.x, mouse.y)
                                characterCenterSettings.x = Math.max(0,
                                    Math.min(surfaceArea.width
                                             - characterCenterSettings.width,
                                             point.x - grabX))
                                characterCenterSettings.y = Math.max(0,
                                    Math.min(surfaceArea.height
                                             - characterCenterSettings.height,
                                             point.y - grabY))
                            }
                        }
                    }
                    GridLayout {
                        columns: 4
                        Layout.fillWidth: true
                        Label { text: "Position X" }
                        SpinBox {
                            id: characterCenterX
                            Layout.fillWidth: true
                            from: 0
                            to: integratedBackend.androidWidth
                            editable: true
                            value: characterCenterSettings.xValue
                            onValueModified:
                                integratedBackend.setCharacterCenterPosition(
                                    value, characterCenterY.value)
                        }
                        Label { text: "Y" }
                        SpinBox {
                            id: characterCenterY
                            Layout.fillWidth: true
                            from: 0
                            to: integratedBackend.androidHeight
                            editable: true
                            value: characterCenterSettings.yValue
                            onValueModified:
                                integratedBackend.setCharacterCenterPosition(
                                    characterCenterX.value, value)
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "Changing this point preserves skill calibration, but marks it for review."
                        color: "#718096"
                    }
                }
            }

            Popup {
                id: movementSettings
                property int xValue: 0
                property int yValue: 0
                property int thresholdValue: 120
                property int distanceValue: 100
                x: Math.max(0, (surfaceArea.width - width) / 2)
                y: Math.max(0, (surfaceArea.height - height) / 2)
                width: 470
                height: Math.min(370, surfaceArea.height - 24)
                modal: false
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 11

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        color: "#174f68"
                        radius: 6

                        Label {
                            anchors.centerIn: parent
                            text: "MOBA movement  •  drag this header"
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
                                movementSettings.x = Math.max(
                                    0, Math.min(surfaceArea.width
                                                - movementSettings.width,
                                                point.x - grabX))
                                movementSettings.y = Math.max(
                                    0, Math.min(surfaceArea.height
                                                - movementSettings.height,
                                                point.y - grabY))
                            }
                        }
                    }

                    GridLayout {
                        columns: 4
                        Layout.fillWidth: true
                        Label { text: "Position X" }
                        SpinBox {
                            id: movementPositionX
                            Layout.fillWidth: true
                            from: 0
                            to: integratedBackend.androidWidth
                            editable: true
                            value: movementSettings.xValue
                            onValueModified:
                                integratedBackend.setMobaMovementPosition(
                                    value, movementPositionY.value)
                        }
                        Label { text: "Y" }
                        SpinBox {
                            id: movementPositionY
                            Layout.fillWidth: true
                            from: 0
                            to: integratedBackend.androidHeight
                            editable: true
                            value: movementSettings.yValue
                            onValueModified:
                                integratedBackend.setMobaMovementPosition(
                                    movementPositionX.value, value)
                        }
                    }

                    GridLayout {
                        columns: 2
                        Layout.fillWidth: true
                        Label { text: "Click / hold threshold" }
                        SpinBox {
                            Layout.fillWidth: true
                            from: 30
                            to: 500
                            stepSize: 5
                            editable: true
                            value: movementSettings.thresholdValue
                            textFromValue: (value, locale) => value + " ms"
                            valueFromText: (text, locale) => parseInt(text)
                            onValueModified:
                                integratedBackend.setMobaMovementHoldThreshold(value)
                        }

                        Label { text: "Click distance modifier" }
                        SpinBox {
                            Layout.fillWidth: true
                            from: 10
                            to: 500
                            stepSize: 5
                            editable: true
                            value: movementSettings.distanceValue
                            textFromValue: (value, locale) => value + "%"
                            valueFromText: (text, locale) => parseInt(text)
                            onValueModified:
                                integratedBackend.setMobaMovementDistanceModifier(value)
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "Hold RMB past the threshold to follow the cursor until release. "
                              + "A shorter click keeps walking in that direction; its duration "
                              + "grows with distance from Character center. A new RMB press "
                              + "always cancels the previous route."
                        color: "#718096"
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "At 100%, a click one shorter-screen side from the center walks "
                              + "for 1600 ms. The modifier scales that time."
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
                property bool cancellableValue: true
                property int cancelReactionValue: 3
                property bool artificialEnabled: false
                property int artificialXValue: 0
                property int artificialYValue: 0
                function loadValues() {
                    xValue = integratedBackend.selectedMobaSkill.pixelX
                    yValue = integratedBackend.selectedMobaSkill.pixelY
                    diameterValue =
                        integratedBackend.selectedMobaSkill.diameterPixels
                    modeValue = integratedBackend.selectedMobaSkill.mode
                    speedValue = integratedBackend.selectedMobaSkill.speedLevel
                    cancellableValue =
                        integratedBackend.selectedMobaSkill.cancellable
                    cancelReactionValue =
                        integratedBackend.selectedMobaSkill.cancelReactionLevel
                    artificialEnabled =
                        integratedBackend.selectedMobaSkill.artificialCenterEnabled
                    artificialXValue =
                        integratedBackend.selectedMobaSkill.artificialPixelX
                    artificialYValue =
                        integratedBackend.selectedMobaSkill.artificialPixelY
                }
                x: Math.max(0, (surfaceArea.width - width) / 2)
                y: Math.max(0, (surfaceArea.height - height) / 2)
                width: Math.min(540, surfaceArea.width - 24)
                height: Math.min(720, surfaceArea.height - 24)
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

                    ScrollView {
                        id: skillSettingsScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                        ColumnLayout {
                            width: skillSettingsScroll.availableWidth
                            spacing: 10

                            Frame {
                                Layout.fillWidth: true
                                ColumnLayout {
                                    anchors.fill: parent
                                    Label {
                                        text: "Geometry"
                                        font.bold: true
                                        font.pixelSize: 15
                                    }
                                    GridLayout {
                                        columns: 4
                                        Layout.fillWidth: true
                                        Label { text: "Center X" }
                                        SpinBox {
                                            id: skillPositionX
                                            Layout.fillWidth: true
                                            from: 0
                                            to: integratedBackend.androidWidth
                                            editable: true
                                            value: skillSettings.xValue
                                            onValueModified: integratedBackend
                                                .setSelectedMobaSkillPosition(
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
                                            onValueModified: integratedBackend
                                                .setSelectedMobaSkillPosition(
                                                    skillPositionX.value, value)
                                        }
                                        Label { text: "Diameter" }
                                        SpinBox {
                                            Layout.columnSpan: 3
                                            Layout.fillWidth: true
                                            from: 48
                                            to: Math.min(
                                                integratedBackend.androidWidth,
                                                integratedBackend.androidHeight) * 0.7
                                            editable: true
                                            value: skillSettings.diameterValue
                                            textFromValue: (value) => value + " px"
                                            valueFromText: (text) => parseInt(text)
                                            onValueModified: integratedBackend
                                                .setSelectedMobaSkillDiameter(value)
                                        }
                                    }
                                }
                            }

                            Frame {
                                Layout.fillWidth: true
                                ColumnLayout {
                                    anchors.fill: parent
                                    Label {
                                        text: "Input and cast"
                                        font.bold: true
                                        font.pixelSize: 15
                                    }
                                    GridLayout {
                                        columns: 2
                                        Layout.fillWidth: true
                                        columnSpacing: 10
                                        rowSpacing: 9
                                        Label { text: "Keyboard bind" }
                                        Button {
                                            Layout.fillWidth: true
                                            text: integratedBackend.waitingForKey
                                                  ? "Press a key…"
                                                  : "Bind: " + integratedBackend
                                                    .selectedMobaSkill.keyName
                                            onClicked: integratedBackend
                                                .beginRebindSelectedMobaSkill()
                                        }
                                        Label { text: "Cast mode" }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            model: ["Follow cursor; release to cast"]
                                            currentIndex: 0
                                            onActivated: (index) => integratedBackend
                                                .setSelectedMobaSkillMode(index)
                                        }
                                        Label { text: "Start speed" }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            model: [
                                                "1 — Stable (120 ms)",
                                                "2 — Fast (60 ms)",
                                                "3 — Very fast (30 ms)",
                                                "4 — Instant (10 ms)",
                                                "5 — Superhuman (next loop)"
                                            ]
                                            currentIndex: Math.max(0, Math.min(
                                                4, skillSettings.speedValue - 1))
                                            onActivated: (index) => {
                                                skillSettings.speedValue = index + 1
                                                integratedBackend
                                                    .setSelectedMobaSkillSpeed(index + 1)
                                            }
                                        }
                                    }
                                }
                            }

                            Frame {
                                Layout.fillWidth: true
                                ColumnLayout {
                                    anchors.fill: parent
                                    Label {
                                        text: "Skill cancellation"
                                        font.bold: true
                                        font.pixelSize: 15
                                    }
                                    CheckBox {
                                        text: "Can be cancelled"
                                        checked: skillSettings.cancellableValue
                                        onToggled: {
                                            skillSettings.cancellableValue = checked
                                            integratedBackend
                                                .setSelectedMobaSkillCancellable(checked)
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        enabled: skillSettings.cancellableValue
                                        Label { text: "Reaction speed" }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            model: [
                                                "1 — Gentle (180 ms)",
                                                "2 — Smooth (110 ms)",
                                                "3 — Balanced (65 ms)",
                                                "4 — Fast (30 ms)",
                                                "5 — Instant"
                                            ]
                                            currentIndex: Math.max(0, Math.min(
                                                4, skillSettings.cancelReactionValue - 1))
                                            onActivated: (index) => {
                                                skillSettings.cancelReactionValue = index + 1
                                                integratedBackend
                                                    .setSelectedMobaSkillCancelReaction(
                                                        index + 1)
                                            }
                                        }
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: !skillSettings.cancellableValue
                                              ? "Cancellation is disabled for this skill."
                                              : (!integratedBackend.hasSkillCancel
                                                 ? "⚠ Add MOBA skill cancel from the right-click menu."
                                                 : (integratedBackend.skillCancel.key === 0
                                                    ? "⚠ Open CANCEL settings and bind a key."
                                                    : "✓ Cancel key: "
                                                      + integratedBackend.skillCancel.keyName))
                                        color: !skillSettings.cancellableValue
                                               || integratedBackend.skillCancel.ready
                                               ? "#218c4f" : "#b86700"
                                        font.bold: true
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: "Reaction speed controls how smoothly the held finger travels into the cancel target before UP."
                                        color: "#718096"
                                    }
                                }
                            }

                            Frame {
                                Layout.fillWidth: true
                                ColumnLayout {
                                    anchors.fill: parent
                                    Label {
                                        text: "Artificial centre"
                                        font.bold: true
                                        font.pixelSize: 15
                                    }
                                    CheckBox {
                                        text: "Press point differs from joystick centre"
                                        checked: skillSettings.artificialEnabled
                                        onToggled: {
                                            skillSettings.artificialEnabled = checked
                                            integratedBackend
                                                .setSelectedMobaSkillArtificialCenterEnabled(
                                                    checked)
                                            if (checked)
                                                skillSettings.loadValues()
                                        }
                                    }
                                    GridLayout {
                                        columns: 4
                                        Layout.fillWidth: true
                                        enabled: skillSettings.artificialEnabled
                                        Label { text: "Press X" }
                                        SpinBox {
                                            id: artificialCenterX
                                            Layout.fillWidth: true
                                            from: 0
                                            to: integratedBackend.androidWidth
                                            editable: true
                                            value: skillSettings.artificialXValue
                                            onValueModified: integratedBackend
                                                .setSelectedMobaSkillArtificialCenterPosition(
                                                    value, artificialCenterY.value)
                                        }
                                        Label { text: "Y" }
                                        SpinBox {
                                            id: artificialCenterY
                                            Layout.fillWidth: true
                                            from: 0
                                            to: integratedBackend.androidHeight
                                            editable: true
                                            value: skillSettings.artificialYValue
                                            onValueModified: integratedBackend
                                                .setSelectedMobaSkillArtificialCenterPosition(
                                                    artificialCenterX.value, value)
                                        }
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: "DOWN starts at the artificial point, moves to the real centre, then follows calibrated aiming."
                                        color: "#718096"
                                    }
                                }
                            }

                            Frame {
                                Layout.fillWidth: true
                                ColumnLayout {
                                    anchors.fill: parent
                                    Label {
                                        text: "Perspective calibration"
                                        font.bold: true
                                        font.pixelSize: 15
                                    }
                                    Frame {
                                        Layout.fillWidth: true
                                        visible: integratedBackend.selectedMobaSkill
                                            .calibrationStale === true
                                        background: Rectangle {
                                            radius: 7
                                            color: "#fff0cf"
                                            border.color: "#e0a322"
                                        }
                                        ColumnLayout {
                                            anchors.fill: parent
                                            Label {
                                                Layout.fillWidth: true
                                                wrapMode: Text.WordWrap
                                                text: "Калибровка навыка была сброшена, он может работать некорректно"
                                                color: "#7a4b00"
                                                font.bold: true
                                            }
                                            RowLayout {
                                                Layout.fillWidth: true
                                                Button {
                                                    Layout.fillWidth: true
                                                    text: "Калибровка корректна"
                                                    onClicked: integratedBackend
                                                        .acceptSelectedMobaSkillCalibration()
                                                }
                                                Button {
                                                    Layout.fillWidth: true
                                                    text: "Вернуть изначальную калибровку"
                                                    enabled: integratedBackend
                                                        .selectedMobaSkill
                                                        .calibrationRecoveryAvailable === true
                                                    onClicked: {
                                                        integratedBackend
                                                            .restoreSelectedMobaSkillCalibration()
                                                        skillSettings.loadValues()
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label {
                                            Layout.fillWidth: true
                                            text: integratedBackend.selectedMobaSkill
                                                  .calibrated
                                                  ? "✓ Ready — "
                                                    + integratedBackend.selectedMobaSkill
                                                        .calibrationCount
                                                    + "/"
                                                    + integratedBackend.selectedMobaSkill
                                                        .calibrationExpected
                                                    + " • "
                                                    + integratedBackend.selectedMobaSkill
                                                        .calibrationModeName
                                                  : "Required — MEGA 66-point calibration"
                                            color: integratedBackend.selectedMobaSkill
                                                   .calibrated
                                                   ? "#218c4f" : "#b86700"
                                            font.bold: true
                                        }
                                        Button {
                                            text: integratedBackend.selectedMobaSkill
                                                  .calibrated
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
                                              : "Changing geometry preserves measured points and marks them for review; accept, restore, or recalibrate."
                                        color: "#718096"
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Popup {
                id: calibrationIntro
                anchors.centerIn: Overlay.overlay
                width: Math.min(620, surfaceArea.width - 40)
                height: 440
                modal: true
                focus: true
                closePolicy: Popup.CloseOnEscape

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 13

                    Label {
                        Layout.fillWidth: true
                        text: "MOBA skill MEGA calibration"
                        font.bold: true
                        font.pixelSize: 22
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "Сейчас маппер одним непрерывным пальцем измерит 66 положений. "
                              + "Сначала — самый крайний контур из 16 лучей, затем пять "
                              + "внутренних контуров: 14, 12, 10, 8 и 6 точек."
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "На каждом шаге кликни ЛКМ точно в КОНЕЦ игрового указателя. "
                              + "EWM сохранит не только точку, но и искусственный луч от "
                              + "центра персонажа. Фиолетовым отмечается внешний предел."
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
                            text: "Start MEGA calibration • 66 points"
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
                        text: "66 точек записаны. Построены шесть фактических контуров "
                              + "и лучевая матрица. За внешним контуром дальность "
                              + "ограничивается, но направление продолжает следовать "
                              + "по линии от персонажа к курсору."
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
                        controlContextMenu.close()
                        bindingSettings.close()
                        characterCenterSettings.close()
                        movementSettings.close()
                        cancelSettings.close()
                        skillSettings.close()
                        calibrationIntro.close()
                        calibrationComplete.close()
                    }
                }
                function onMobaSkillCalibrationCompleted(index) {
                    calibrationComplete.open()
                }
                function onProfileAdaptationRequested() {
                    profileAdaptationPopup.open()
                }
                function onProfileManagerVisibleChanged() {
                    if (!integratedBackend.profileManagerVisible) {
                        profileContextMenu.close()
                        renameProfilePopup.close()
                        deleteProfilePopup.close()
                        profileAdaptationPopup.close()
                        profileImageDialog.close()
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
