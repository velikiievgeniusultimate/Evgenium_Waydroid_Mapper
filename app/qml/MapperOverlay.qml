import QtQuick
import QtQml
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import QtWayland.Compositor
import QtWayland.Compositor.XdgShell

Item {
    id: integratedWindow
    objectName: "mapperOverlay"
    readonly property bool experimentalAvailable: typeof integratedBackend.globalCalibration !== "undefined"
    readonly property bool experimentalSelected: experimentalAvailable && integratedBackend.selectedMobaSkill.experimental === true
    readonly property var sharedCalibration: experimentalAvailable ? integratedBackend.globalCalibration : ({})
    readonly property bool directionOnly: experimentalAvailable && integratedBackend.selectedMobaSkill.directionalOnly === true
    readonly property bool experimentReady: experimentalSelected && sharedCalibration.angleReady === true
    readonly property bool autoGeometryAvailable: typeof integratedBackend.autoGeometryActive !== "undefined"
    readonly property bool geometrySearching: autoGeometryAvailable && integratedBackend.autoGeometryActive
    property bool uranusPicking: false
    property real uranusUltimateX: .85
    property real uranusUltimateY: .7
    property var shellSurfaces
    function visionStateName(state) {
        const names = { IDLE: "Ожидание", ACQUIRING: "Захват", LOCKED: "Зафиксирован",
            PREDICT: "Прогноз", CONFIRM: "Подтверждение", LOST: "Потерян", SEARCH: "Поиск" }
        return names[state] || state
    }
    component PopupCloseMark: Rectangle {
        property bool mapperCloseControl: true
        signal triggered()
        implicitWidth: 32; implicitHeight: 30
        width: 32; height: 30
        radius: 5
        color: closeMouse.containsMouse ? "#456c8194" : "transparent"
        z: 10
        property color foreground: "#23313c"
        ToolTip.visible: closeMouse.containsMouse
        ToolTip.text: "Закрыть"
        Text { anchors.centerIn: parent; text: "×"; color: parent.foreground; font.pixelSize: 24 }
        MouseArea {
            id: closeMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.triggered()
        }
    }
    component CalibrationSwitch: TabBar {
        Layout.fillWidth: true
        currentIndex: integratedWindow.experimentalSelected ? 1 : 0
        TabButton {
            objectName: "originalCalibrationTab"
            text: "Оригинал"
            onClicked: if (integratedWindow.experimentalAvailable) integratedBackend.setSelectedSkillExperimental(false)
        }
        TabButton {
            objectName: "experimentalCalibrationTab"
            text: "Экспериментальная"
            enabled: integratedWindow.experimentalAvailable
            onClicked: integratedBackend.setSelectedSkillExperimental(true)
        }
    }
    component ExperimentalControls: ColumnLayout {
        Layout.fillWidth: true
        spacing: 8
        TabBar {
            Layout.fillWidth: true
            currentIndex: integratedWindow.directionOnly ? 1 : 0
            TabButton {
                objectName: "rangeSkillTab"
                text: "Навык с дальностью"
                onClicked: integratedBackend.setSelectedSkillDirectional(false)
            }
            TabButton {
                objectName: "directionSkillTab"
                text: "Навык направления"
                onClicked: integratedBackend.setSelectedSkillDirectional(true)
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Общие центр и угол · " + (integratedWindow.sharedCalibration.resolution || "")
                  + "\nУгол: " + (integratedWindow.sharedCalibration.angleReady ? "готов · 64 точки" : "не откалиброван")
                  + (integratedWindow.directionOnly ? "" : "\nДальность навыка: "
                     + (integratedBackend.selectedMobaSkill.rangeReady ? (integratedBackend.selectedMobaSkill.rangeMissingMask ? "готова · есть обрезанные края" : "готова · " + integratedBackend.selectedMobaSkill.rangeCount + " точек") : "не откалибрована"))
            color: "#516477"
        }
        RowLayout {
            Layout.fillWidth: true
            visible: !integratedWindow.directionOnly
            Label { text: "Точность дальности" }
            ComboBox {
                objectName: "experimentalRangeDensity"
                Layout.fillWidth: true
                model: ["12 точек · 4 направления", "24 точки · 8 направлений", "48 точек · 16 направлений"]
                currentIndex: integratedBackend.selectedMobaSkill.rangeSampleCount === 48 ? 2
                    : integratedBackend.selectedMobaSkill.rangeSampleCount === 24 ? 1 : 0
                onActivated: integratedBackend.setSelectedSkillRangeSamples([12,24,48][currentIndex])
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Button {
                objectName: "experimentalAngleButton"
                Layout.fillWidth: true
                text: "Калибровка угла"
                enabled: integratedBackend.hasCharacterCenter && !integratedBackend.waitingForKey
                onClicked: integratedWindow.startExperimentalCalibration(true)
            }
            Button {
                objectName: "experimentalRangeButton"
                Layout.fillWidth: true
                text: "Калибровка дальности"
                enabled: integratedWindow.experimentReady && !integratedWindow.directionOnly
                         && integratedBackend.hasCharacterCenter && !integratedBackend.waitingForKey
                onClicked: integratedWindow.startExperimentalCalibration(false)
            }
        }
        RowLayout {
            visible: !integratedWindow.directionOnly
            Layout.fillWidth: true
            Label { text: "Разгон у края" }
            SpinBox {
                objectName: "experimentalEdgeZone"
                from: 1; to: 30
                value: integratedBackend.selectedMobaSkill.edgeZonePercent || 5
                editable: true
                onValueModified: integratedBackend.setSelectedSkillEdgeZone(value)
            }
            Label { text: "% короткой стороны экрана"; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: integratedWindow.directionOnly
                ? "Используется общий угол. Джойстик отклоняется до края, расстояние до курсора не учитывается."
                : "При калибровке пропускайте невидимые концы кнопкой «Конец за экраном». Внутри — измеренная дальность, у обрезанного края — плавный разгон. Если видимых точек мало, зона разгона расширяется."
            color: "#718096"
        }
    }
    function startExperimentalCalibration(angle) {
        const index = integratedBackend.selectedMobaSkillIndex
        skillSettings.close()
        calibrationIntro.close()
        integratedBackend.beginExperimentalCalibration(index, angle)
    }
    // Marker geometry stays in Android coordinates; this badge stays screen-sized.
    component ControlBadge: Item {
        id: badge
        property real uiScale: 1
        property string keyText: ""
        property string typeText: ""
        property string shape: "circle"
        property color accent: "#5276e5"
        property bool selected: false
        property string warning: ""
        property Item moveTarget
        property bool recordEdit: true
        readonly property bool dragActive: badgeMouse.drag.active || badgeMouse.dragged
        // Track the moving parent explicitly: mapToItem alone has no change signal.
        readonly property point screenPosition: {
            const targetX = moveTarget ? moveTarget.x : 0
            const targetY = moveTarget ? moveTarget.y : 0
            const scale = uiScale
            const windowWidth = integratedWindow.width
            const windowHeight = integratedWindow.height
            return mapToItem(integratedWindow, 0, 0)
        }
        readonly property bool labelAbove: screenPosition.y + 73 > integratedWindow.height
        signal activated()
        signal moved(real normalizedX, real normalizedY)
        signal contextRequested(real px, real py)
        signal rebindRequested()
        width: 48 / uiScale
        height: width
        z: 12
        Rectangle {
            visible: badge.shape !== "plus"
            anchors.centerIn: parent
            width: (badge.shape === "diamond" ? 34 : 44) / badge.uiScale
            height: width
            rotation: badge.shape === "diamond" ? 45 : 0
            radius: badge.shape === "circle" ? width / 2 : 7 / badge.uiScale
            color: Qt.rgba(badge.accent.r, badge.accent.g, badge.accent.b, 0.88)
            border.color: badge.selected ? "#ffe08a" : "#e8f1fc"
            border.width: (badge.selected ? 3 : 2) / badge.uiScale
        }
        Item {
            objectName: badge.objectName + "-cross"
            anchors.fill: parent
            visible: badge.shape === "plus"
            Rectangle { anchors.centerIn: parent; width: 44 / badge.uiScale; height: 8 / badge.uiScale; radius: 3 / badge.uiScale; color: "#b3111820" }
            Rectangle { anchors.centerIn: parent; height: 44 / badge.uiScale; width: 8 / badge.uiScale; radius: 3 / badge.uiScale; color: "#b3111820" }
            Rectangle { anchors.centerIn: parent; width: 42 / badge.uiScale; height: 3 / badge.uiScale; color: badge.selected ? "#ffe08a" : "#ff839e" }
            Rectangle { anchors.centerIn: parent; height: 42 / badge.uiScale; width: 3 / badge.uiScale; color: badge.selected ? "#ffe08a" : "#ff839e" }
            Rectangle { anchors.centerIn: parent; width: 7 / badge.uiScale; height: width; radius: width / 2; color: "white" }
        }
        Text {
            anchors.centerIn: parent
            visible: badge.shape !== "plus"
            text: badge.keyText || "—"
            width: 40 / badge.uiScale
            horizontalAlignment: Text.AlignHCenter
            fontSizeMode: Text.Fit
            minimumPixelSize: 9 / badge.uiScale
            font.pixelSize: 15 / badge.uiScale
            font.bold: true
            color: "white"
        }
        Rectangle {
            objectName: badge.objectName + "-label"
            y: badge.labelAbove ? -height - 3 / badge.uiScale : badge.height + 3 / badge.uiScale
            x: Math.max(-badge.screenPosition.x / badge.uiScale,
                        Math.min((badge.width - width) / 2,
                                 (integratedWindow.width - badge.screenPosition.x) / badge.uiScale - width))
            width: badgeLabel.implicitWidth + 10 / badge.uiScale
            height: 18 / badge.uiScale
            radius: 5 / badge.uiScale
            color: "#cc172330"
            Text {
                id: badgeLabel
                anchors.centerIn: parent
                text: badge.typeText
                font.pixelSize: 10 / badge.uiScale
                color: badge.selected ? "#ffe08a" : "#e1eaf4"
            }
        }
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            width: 9 / badge.uiScale
            height: width
            radius: width / 2
            color: "#ffc65c"
            border.color: "#252c37"
            visible: badge.warning !== ""
        }
        MouseArea {
            id: badgeMouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: badge.selected ? Qt.SizeAllCursor : Qt.PointingHandCursor
            drag.target: badge.selected ? badge.moveTarget : null
            drag.minimumX: badge.moveTarget ? -badge.moveTarget.width / 2 : 0
            drag.maximumX: badge.moveTarget ? badge.moveTarget.parent.width - badge.moveTarget.width / 2 : 0
            drag.minimumY: badge.moveTarget ? -badge.moveTarget.height / 2 : 0
            drag.maximumY: badge.moveTarget ? badge.moveTarget.parent.height - badge.moveTarget.height / 2 : 0
            property bool dragged: false
            property bool movedDuringPress: false
            property bool editActionStarted: false
            function finishEditAction() {
                if (editActionStarted) {
                    editActionStarted = false
                    integratedBackend.endMapperEditAction()
                }
            }
            Component.onDestruction: finishEditAction()
            onPressed: (mouse) => {
                dragged = false
                movedDuringPress = false
                if (mouse.button === Qt.RightButton)
                    badge.contextRequested(mouse.x, mouse.y)
            }
            onPositionChanged: {
                if (drag.active) {
                    if (badge.recordEdit && !editActionStarted) {
                        integratedBackend.beginMapperEditAction()
                        editActionStarted = true
                    }
                    dragged = true
                    movedDuringPress = true
                }
            }
            onReleased: (mouse) => {
                // Updating a QVariantList can destroy this delegate immediately.
                // Capture coordinates and backend before committing, and use
                // only local variables after emitting the move signal.
                const backend = integratedBackend
                const grouped = editActionStarted
                const commit = mouse.button === Qt.LeftButton && dragged
                const nx = badge.moveTarget ? (badge.moveTarget.x + badge.moveTarget.width / 2) / badge.moveTarget.parent.width : 0
                const ny = badge.moveTarget ? (badge.moveTarget.y + badge.moveTarget.height / 2) / badge.moveTarget.parent.height : 0
                editActionStarted = false
                dragged = false
                if (commit) badge.moved(nx, ny)
                if (grouped) backend.endMapperEditAction()
            }
            onCanceled: {
                finishEditAction()
                dragged = false
            }
            onClicked: (mouse) => {
                if (mouse.button === Qt.LeftButton && !movedDuringPress) badge.activated()
            }
            onDoubleClicked: (mouse) => {
                if (mouse.button === Qt.LeftButton) badge.rebindRequested()
            }
        }
        ToolTip.visible: badgeMouse.containsMouse && badge.warning !== ""
        ToolTip.text: badge.warning
        ToolTip.delay: 450

    }
    property string auxiliarySelection: ""
    function selectControl(type, index) {
        auxiliarySelection = (type === "tap" || type === "skill") ? "" : type
        integratedBackend.selectBinding(type === "tap" ? index : -1)
        integratedBackend.selectMobaSkill(type === "skill" ? index : -1)
    }
    function clearControlSelection() {
        auxiliarySelection = ""
        integratedBackend.selectBinding(-1)
        integratedBackend.selectMobaSkill(-1)
    }
    readonly property int calibrationIssueCount: {
        const skills = integratedBackend.mobaSkills
        let count = 0
        for (let i = 0; i < skills.length; ++i)
            if (skills[i].calibrationStale || !skills[i].calibrated
                    || !integratedBackend.hasCharacterCenter || skills[i].key === 0)
                ++count
        return count
    }
    Connections {
        target: integratedBackend
        function onEditModeChanged() { integratedWindow.auxiliarySelection = "" }
        function onSelectedBindingChanged() {
            if (integratedBackend.selectedBinding.key !== undefined)
                integratedWindow.auxiliarySelection = ""
        }
        function onSelectedMobaSkillChanged() {
            if (integratedBackend.selectedMobaSkill.key !== undefined)
                integratedWindow.auxiliarySelection = ""
        }
    }

    anchors.fill: parent
            property real contextTapX: 0
            property real contextTapY: 0
            property string contextControlType: ""
            property int contextControlIndex: -1
            property string contextProfileId: ""
            property string contextProfileName: ""
            property bool contextProfileIsDefault: false
            property bool calibrationWasActive: false
            property bool calibrationCenterHintVisible: false

            function openControlMenu(source, mouseX, mouseY, type, index) {
                // Selection may rebuild the source delegate. Resolve the
                // cursor position first, in the menu's explicit parent space.
                const point = source.mapToItem(surfaceArea, mouseX, mouseY)
                contextControlType = type
                contextControlIndex = index
                selectControl(type, index)
                controlContextMenu.popup(point.x, point.y)
            }

            function openControlSettings(type, index) {
                if (type !== "tap") bindingSettings.close()
                if (type !== "center") characterCenterSettings.close()
                if (type !== "movement") movementSettings.close()
                if (type !== "cancel") cancelSettings.close()
                if (type !== "skill") skillSettings.close()
                skillSettings.autoGeometryMessage = ""
                selectControl(type, index)
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
                    return "Нажатие " + integratedBackend.bindings[index].keyName
                if (type === "skill")
                    return "Навык MOBA "
                           + integratedBackend.mobaSkills[index].keyName
                if (type === "center")
                    return "Центр героя"
                if (type === "movement")
                    return "Движение MOBA"
                if (type === "cancel")
                    return "Отмена навыка "
                           + integratedBackend.skillCancel.keyName
                return "Кнопка маппера"
            }

            function toggleFullscreen() {
                const win = Window.window
                win.visibility = win.visibility === Window.FullScreen
                    ? Window.Windowed : Window.FullScreen
            }

            function mouseButtonName(button) {
                if (button === 1) return "ЛКМ"
                if (button === 2) return "ПКМ"
                if (button === 4) return "СКМ"
                if (button === 8) return "Мышь 4"
                if (button === 16) return "Мышь 5"
                return "Мышь " + button
            }

            onVisibleChanged: {
                if (visible) {
                    Window.window.raise()
                    Window.window.requestActivate()
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

                Button {
                    id: cursorLibraryButton
                    z: 1000
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: 12
                    visible: integratedBackend.editMode && !integratedWindow.uranusPicking
                    text: "Курсор ▾"
                    onClicked: cursorLibrary.open()
                }
                Popup {
                    id: cursorLibrary
                    z: 1001
                    x: 12
                    y: cursorLibraryButton.height + 16
                    width: 210
                    height: 235
                    padding: 8
                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                    ColumnLayout {
                        anchors.fill: parent
                        RowLayout {
                        Layout.fillWidth: true
                        Label {
                        Layout.fillWidth: true; text: "Выберите курсор"; font.bold: true }
                        PopupCloseMark { objectName: "cursorLibraryClose"; onTriggered: cursorLibrary.close() }
                    }
                        Repeater {
                            model: ["Оранжевый прицел", "Голубой прицел",
                                    "Обычная стрелка", "Фиолетовый прицел"]
                            Button {
                                Layout.fillWidth: true
                                text: (integratedBackend.cursorStyle === index ? "✓  " : "    ")
                                      + modelData
                                onClicked: {
                                    integratedBackend.setCursorStyle(index)
                                    cursorLibrary.close()
                                }
                            }
                        }
                    }
                }

                Repeater {
                    model: shellSurfaces
                    Item {
                        id: surfaceHost
                        visible: !integratedWindow.geometrySearching
                        width: Math.max(1, model.shellSurface.surface.destinationSize.width)
                        height: Math.max(1, model.shellSurface.surface.destinationSize.height)
                        anchors.centerIn: surfaceArea
                        transformOrigin: Item.Center
                        scale: Math.min(surfaceArea.width / width,
                                        surfaceArea.height / height)

                        MouseArea {
                            anchors.fill: parent
                            visible: integratedWindow.uranusPicking
                            acceptedButtons: Qt.AllButtons
                            z: 800
                        }
                        Item {
                            id: uranusUltimateHandle
                            visible: integratedWindow.uranusPicking
                            z: 801
                            width: 48 / surfaceHost.scale; height: width
                            x: integratedWindow.uranusUltimateX * parent.width - width / 2
                            y: integratedWindow.uranusUltimateY * parent.height - height / 2
                            ControlBadge {
                                objectName: "uranusUltimateBadge"
                                anchors.fill: parent
                                uiScale: surfaceHost.scale
                                moveTarget: uranusUltimateHandle
                                recordEdit: false
                                selected: true
                                keyText: "У"
                                typeText: "Ульта"
                                accent: "#168ea7"
                                onMoved: (nx, ny) => {
                                    integratedWindow.uranusUltimateX = nx
                                    integratedWindow.uranusUltimateY = ny
                                    uranusUltimateHandle.x = Qt.binding(() => integratedWindow.uranusUltimateX * surfaceHost.width - uranusUltimateHandle.width / 2)
                                    uranusUltimateHandle.y = Qt.binding(() => integratedWindow.uranusUltimateY * surfaceHost.height - uranusUltimateHandle.height / 2)
                                }
                            }
                        }
                        Rectangle {
                            anchors.fill: parent
                            color: "#18000000"
                            visible: integratedBackend.editMode && !integratedWindow.uranusPicking
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
                                        addControlMenu.popup(point.x, point.y)
                                    } else if (integratedBackend.waitingForKey) {
                                        integratedBackend.cancelKeyCapture(true)
                                    } else {
                                        integratedWindow.clearControlSelection()
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
                                width: 48 / markerScale
                                height: width
                                visible: integratedBackend.editMode && !integratedWindow.uranusPicking && !integratedBackend.calibrationActive
                                z: modelData.selected ? 70 : 30
                                Binding {
                                    target: marker; property: "x"
                                    value: modelData.x * surfaceHost.width - marker.width / 2
                                    when: !tapBadge.dragActive
                                    restoreMode: Binding.RestoreNone
                                }
                                Binding {
                                    target: marker; property: "y"
                                    value: modelData.y * surfaceHost.height - marker.height / 2
                                    when: !tapBadge.dragActive
                                    restoreMode: Binding.RestoreNone
                                }
                                ControlBadge {
                                    id: tapBadge
                                    objectName: "tapBadge-" + marker.index
                                    anchors.centerIn: parent
                                    uiScale: marker.markerScale
                                    keyText: modelData.keyName
                                    typeText: modelData.mode === 0 ? "Нажатие" : "Удержание"
                                    selected: modelData.selected
                                    accent: modelData.key === 0 ? "#7d8790" : "#3157d5"
                                    warning: modelData.keyConflict ? "Конфликт клавиши " + modelData.keyName : ""
                                    moveTarget: marker
                                    onActivated: integratedWindow.selectControl("tap", marker.index)
                                    onMoved: (nx, ny) => integratedBackend.moveBinding(marker.index, nx, ny)

                                    onContextRequested: (px, py) => integratedWindow.openControlMenu(tapBadge, px, py, "tap", marker.index)
                                    onRebindRequested: { integratedWindow.selectControl("tap", marker.index); integratedBackend.beginRebindSelected() }
                                }
                            }
                        }

                        Item {
                            id: characterCenterMarker
                            readonly property real markerScale: Math.max(surfaceHost.scale, 0.01)
                            width: 64 / markerScale; height: width
                            visible: integratedBackend.editMode && !integratedWindow.uranusPicking && integratedBackend.hasCharacterCenter
                            z: 31
                            Binding { target: characterCenterMarker; property: "x"; value: integratedBackend.characterCenter.x * surfaceHost.width - characterCenterMarker.width / 2; when: !centerBadge.dragActive; restoreMode: Binding.RestoreNone }
                            Binding { target: characterCenterMarker; property: "y"; value: integratedBackend.characterCenter.y * surfaceHost.height - characterCenterMarker.height / 2; when: !centerBadge.dragActive; restoreMode: Binding.RestoreNone }
                            Item {
                                anchors.centerIn: parent; width: 96 / characterCenterMarker.markerScale; height: width
                                visible: integratedWindow.auxiliarySelection === "center" || integratedBackend.calibrationActive
                                Rectangle { anchors.centerIn: parent; width: parent.width; height: 2 / characterCenterMarker.markerScale; color: "#ff839e" }
                                Rectangle { anchors.centerIn: parent; height: parent.height; width: 2 / characterCenterMarker.markerScale; color: "#ff839e" }
                            }
                            ControlBadge {
                                id: centerBadge
                                objectName: "centerBadge"
                                anchors.centerIn: parent
                                uiScale: characterCenterMarker.markerScale
                                keyText: "+"; typeText: "Центр героя"; shape: "plus"; accent: "#a13758"
                                selected: integratedWindow.auxiliarySelection === "center"
                                moveTarget: characterCenterMarker
                                visible: !integratedBackend.calibrationActive
                                onActivated: integratedWindow.selectControl("center", -1)
                                onMoved: (nx, ny) => integratedBackend.moveCharacterCenter(nx, ny)

                                onContextRequested: (px, py) => integratedWindow.openControlMenu(centerBadge, px, py, "center", -1)
                            }
                        }

                        Item {
                            id: skillCancelMarker
                            readonly property real markerScale: Math.max(surfaceHost.scale, 0.01)
                            width: 48 / markerScale; height: width
                            visible: integratedBackend.editMode && !integratedWindow.uranusPicking && integratedBackend.hasSkillCancel && !integratedBackend.calibrationActive
                            z: 32
                            Binding { target: skillCancelMarker; property: "x"; value: integratedBackend.skillCancel.x * surfaceHost.width - skillCancelMarker.width / 2; when: !cancelBadge.dragActive; restoreMode: Binding.RestoreNone }
                            Binding { target: skillCancelMarker; property: "y"; value: integratedBackend.skillCancel.y * surfaceHost.height - skillCancelMarker.height / 2; when: !cancelBadge.dragActive; restoreMode: Binding.RestoreNone }
                            ControlBadge {
                                id: cancelBadge
                                objectName: "cancelBadge"
                                anchors.centerIn: parent
                                uiScale: skillCancelMarker.markerScale
                                keyText: integratedBackend.skillCancel.key === 0 ? "—" : integratedBackend.skillCancel.keyName
                                typeText: "Отмена навыка"; shape: "square"; accent: "#9c2438"
                                selected: integratedWindow.auxiliarySelection === "cancel"
                                warning: integratedBackend.skillCancel.ready ? "" : "Назначьте клавишу отмены"
                                moveTarget: skillCancelMarker
                                onActivated: integratedWindow.selectControl("cancel", -1)
                                onMoved: (nx, ny) => integratedBackend.moveSkillCancel(nx, ny)

                                onContextRequested: (px, py) => integratedWindow.openControlMenu(cancelBadge, px, py, "cancel", -1)
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
                            Binding { target: mobaMovementMarker; property: "x"; value: integratedBackend.mobaMovement.x * surfaceHost.width - mobaMovementMarker.width / 2; when: !movementBadge.dragActive; restoreMode: Binding.RestoreNone }
                            Binding { target: mobaMovementMarker; property: "y"; value: integratedBackend.mobaMovement.y * surfaceHost.height - mobaMovementMarker.height / 2; when: !movementBadge.dragActive; restoreMode: Binding.RestoreNone }
                            visible: integratedBackend.editMode && !integratedWindow.uranusPicking
                                     && integratedBackend.hasMobaMovement
                            z: expanded ? 60 : 29
                            readonly property bool expanded: integratedWindow.auxiliarySelection === "movement"

                            Rectangle {
                                id: mobaCircle
                                objectName: "movementGeometry"
                                visible: mobaMovementMarker.expanded
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
                                    visible: false
                                    anchors.centerIn: parent
                                    anchors.verticalCenterOffset:
                                        24 / mobaMovementMarker.markerScale
                                    text: integratedBackend.mobaMovement.inputMode === 1
                                          ? "MOBA  •  WASD"
                                          : "MOBA  •  " + integratedWindow.mouseButtonName(
                                              integratedBackend.mobaMovement.mouseButton)
                                    color: "white"
                                    font.bold: true
                                    font.pixelSize: 13 / mobaMovementMarker.markerScale
                                }

                                MouseArea {
                                    enabled: false
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    cursorShape: Qt.SizeAllCursor
                                    onPressed: (mouse) => {
                                        if (mouse.button === Qt.LeftButton)
                                            integratedBackend.beginMapperEditAction()
                                        else if (mouse.button === Qt.RightButton)
                                            integratedWindow.openControlMenu(
                                                this, mouse.x, mouse.y,
                                                "movement", -1)
                                    }
                                    onReleased: (mouse) => {
                                        if (mouse.button === Qt.LeftButton)
                                            integratedBackend.endMapperEditAction()
                                    }
                                    onCanceled: integratedBackend.endMapperEditAction()
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
                                objectName: "movementRadiusHandle"
                                visible: mobaMovementMarker.expanded
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
                                    onPressed: integratedBackend.beginMapperEditAction()
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
                                    onReleased: integratedBackend.endMapperEditAction()
                                    onCanceled: integratedBackend.endMapperEditAction()
                                }
                            }

                            Rectangle {
                                visible: mobaMovementMarker.expanded && !integratedBackend.hasCharacterCenter
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
                                    text: "⚠ НУЖЕН ЦЕНТР ГЕРОЯ"
                                    color: "white"
                                    font.bold: true
                                    font.pixelSize: 11 / mobaMovementMarker.markerScale
                                }
                            }
                            ControlBadge {
                                id: movementBadge
                                objectName: "movementBadge"
                                anchors.centerIn: parent
                                uiScale: mobaMovementMarker.markerScale
                                keyText: integratedBackend.mobaMovement.inputMode === 1 ? "WASD" : integratedWindow.mouseButtonName(integratedBackend.mobaMovement.mouseButton)
                                typeText: "MOBA движение"; accent: "#1689ad"
                                selected: mobaMovementMarker.expanded
                                warning: integratedBackend.hasCharacterCenter ? "" : "Укажите центр героя"
                                moveTarget: mobaMovementMarker
                                onActivated: integratedWindow.selectControl("movement", -1)
                                onMoved: (nx, ny) => integratedBackend.moveMobaMovement(nx, ny)

                                onContextRequested: (px, py) => integratedWindow.openControlMenu(movementBadge, px, py, "movement", -1)
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
                                readonly property bool expanded: modelData.selected
                                // Keep runtime coordinates intact. Only the editor's
                                // visual origin moves to the physical press point.
                                readonly property real editorCenterX: modelData.artificialCenterEnabled ? modelData.artificialX : modelData.x
                                readonly property real editorCenterY: modelData.artificialCenterEnabled ? modelData.artificialY : modelData.y
                                // The badge marks touch-down; joystick geometry is
                                // anchored to the separate true/approach centre.
                                readonly property real joystickCenterX: modelData.artificialCenterEnabled
                                    ? realCenterHandle.x + realCenterHandle.width / 2 : width / 2
                                readonly property real joystickCenterY: modelData.artificialCenterEnabled
                                    ? realCenterHandle.y + realCenterHandle.height / 2 : height / 2
                                property bool resizingRadius: false
                                property real previewRadius: modelData.radius
                                readonly property real radiusPixels:
                                    (resizingRadius ? previewRadius
                                                    : modelData.radius)
                                    * Math.min(surfaceHost.width, surfaceHost.height)
                                width: radiusPixels * 2
                                height: width
                                visible: integratedBackend.editMode && !integratedWindow.uranusPicking
                                         && !integratedBackend.calibrationActive
                                z: modelData.selected ? 70 : 24

                                Rectangle {
                                    x: skillCircle.x - 7 / skillMarker.markerScale
                                    y: skillCircle.y - 7 / skillMarker.markerScale
                                    width: skillCircle.width + 14 / skillMarker.markerScale
                                    height: width
                                    radius: width / 2
                                    color: "transparent"
                                    border.color: "#ffe066"
                                    border.width: 4 / skillMarker.markerScale
                                    visible: skillMarker.expanded
                                }

                                Binding {
                                    target: skillMarker
                                    property: "x"
                                    value: skillMarker.editorCenterX * surfaceHost.width
                                           - skillMarker.width / 2
                                    when: !skillBadge.dragActive
                                    restoreMode: Binding.RestoreNone
                                }
                                Binding {
                                    target: skillMarker
                                    property: "y"
                                    value: skillMarker.editorCenterY * surfaceHost.height
                                           - skillMarker.height / 2
                                    when: !skillBadge.dragActive
                                    restoreMode: Binding.RestoreNone
                                }

                                Rectangle {
                                    id: skillCircle
                                    objectName: "skillGeometry-" + skillMarker.index
                                    visible: skillMarker.expanded
                                    x: skillMarker.joystickCenterX - width / 2
                                    y: skillMarker.joystickCenterY - height / 2
                                    width: skillMarker.width
                                    height: width
                                    radius: width / 2
                                    color: modelData.calibrated
                                           ? "#4439b86f" : "#44a260d1"
                                    border.color: modelData.keyConflict
                                                  ? "#ff304f"
                                                  : (!integratedBackend.hasCharacterCenter
                                                     ? "#ffb020"
                                                     : (modelData.calibrated
                                                        ? "#72f2a4" : "#df9cff"))
                                    border.width: (modelData.keyConflict ? 5 : 3)
                                                  / skillMarker.markerScale

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
                                        visible: false
                                        anchors.centerIn: parent
                                        anchors.verticalCenterOffset:
                                            23 / skillMarker.markerScale
                                        text: "НАВЫК · " + modelData.keyName
                                        color: "white"
                                        font.bold: true
                                        font.pixelSize: 12 / skillMarker.markerScale
                                    }
                                    Label {
                                        anchors.left: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "⚠ конфликт " + modelData.keyName
                                        color: "#ff5068"
                                        font.bold: true
                                        visible: modelData.keyConflict
                                    }
                                    MouseArea {
                                        id: skillMoveMouse
                                        enabled: false
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        cursorShape: Qt.SizeAllCursor
                                        drag.target: modelData.selected
                                                     ? skillMarker : null
                                        drag.minimumX: -skillMarker.width / 2
                                        drag.maximumX: surfaceHost.width
                                                       - skillMarker.width / 2
                                        drag.minimumY: -skillMarker.height / 2
                                        drag.maximumY: surfaceHost.height
                                                       - skillMarker.height / 2
                                        onPressed: (mouse) => {
                                            if (mouse.button === Qt.RightButton
                                                    && modelData.selected)
                                                integratedWindow.openControlMenu(
                                                    this, mouse.x, mouse.y,
                                                    "skill", skillMarker.index)
                                        }
                                        onReleased: (mouse) => {
                                            if (mouse.button === Qt.LeftButton
                                                    && modelData.selected)
                                                integratedBackend.moveMobaSkill(
                                                    skillMarker.index,
                                                    (skillMarker.x + skillMarker.width / 2)
                                                    / surfaceHost.width,
                                                    (skillMarker.y + skillMarker.height / 2)
                                                    / surfaceHost.height)
                                        }
                                        onClicked: (mouse) => {
                                            if (mouse.button !== Qt.LeftButton)
                                                return
                                            const point = mapToItem(surfaceHost,
                                                                    mouse.x, mouse.y)
                                            integratedBackend.selectNextControlAt(
                                                point.x / surfaceHost.width,
                                                point.y / surfaceHost.height)
                                        }
                                        onDoubleClicked: (mouse) => {
                                            if (mouse.button === Qt.LeftButton
                                                    && modelData.selected) {
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
                                    visible: skillMarker.expanded && modelData.artificialCenterEnabled
                                             && routeLength > 24 / skillMarker.markerScale
                                    objectName: "skillCenterRoute-" + skillMarker.index
                                    readonly property real startX: skillMarker.width / 2
                                    readonly property real startY: skillMarker.height / 2
                                    readonly property real endX: realCenterHandle.x + realCenterHandle.width / 2
                                    readonly property real endY: realCenterHandle.y + realCenterHandle.height / 2
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
                                    id: realCenterHandle
                                    objectName: "skillTrueCenter-" + skillMarker.index
                                    property bool movingTarget: false
                                    visible: skillMarker.expanded && modelData.artificialCenterEnabled
                                    width: 38 / skillMarker.markerScale
                                    height: width
                                    z: 8

                                    Binding {
                                        target: realCenterHandle
                                        property: "x"
                                        value: modelData.x * surfaceHost.width
                                               - skillMarker.x
                                               - realCenterHandle.width / 2
                                        when: !realCenterHandle.movingTarget
                                        restoreMode: Binding.RestoreNone
                                    }
                                    Binding {
                                        target: realCenterHandle
                                        property: "y"
                                        value: modelData.y * surfaceHost.height
                                               - skillMarker.y
                                               - realCenterHandle.height / 2
                                        when: !realCenterHandle.movingTarget
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
                                        text: "Отвод"
                                        color: "#ffc078"
                                        font.bold: true
                                        font.pixelSize: 10 / skillMarker.markerScale
                                        style: Text.Outline
                                        styleColor: "#c0000000"
                                    }
                                    MouseArea {
                                        id: realCenterHandleMouse
                                        anchors.fill: parent
                                        acceptedButtons: Qt.LeftButton
                                        cursorShape: Qt.SizeAllCursor
                                        drag.target: realCenterHandle
                                        drag.minimumX: -skillMarker.x
                                                       - realCenterHandle.width / 2
                                        drag.maximumX: surfaceHost.width
                                                       - skillMarker.x
                                                       - realCenterHandle.width / 2
                                        drag.minimumY: -skillMarker.y
                                                       - realCenterHandle.height / 2
                                        drag.maximumY: surfaceHost.height
                                                       - skillMarker.y
                                                       - realCenterHandle.height / 2
                                        onPressed: {
                                            realCenterHandle.movingTarget = true
                                            integratedBackend.beginMapperEditAction()
                                        }
                                        onReleased: {
                                            const backend = integratedBackend
                                            const index = skillMarker.index
                                            const nx = (skillMarker.x + realCenterHandle.x + realCenterHandle.width / 2) / surfaceHost.width
                                            const ny = (skillMarker.y + realCenterHandle.y + realCenterHandle.height / 2) / surfaceHost.height
                                            realCenterHandle.movingTarget = false
                                            backend.moveMobaSkill(index, nx, ny)
                                            backend.endMapperEditAction()
                                        }
                                        onCanceled: {
                                            integratedBackend.endMapperEditAction()
                                            realCenterHandle.movingTarget = false
                                        }
                                    }
                                }

                                Item {
                                    id: skillRadiusHandle
                                    objectName: "skillRadiusHandle-" + skillMarker.index
                                    visible: skillMarker.expanded
                                    width: 34 / skillMarker.markerScale
                                    height: width
                                    x: skillMarker.joystickCenterX + skillMarker.radiusPixels - width / 2
                                    y: skillMarker.joystickCenterY - height / 2
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
                                            integratedBackend.beginMapperEditAction()
                                            skillMarker.previewRadius = modelData.radius
                                            skillMarker.resizingRadius = true
                                        }
                                        onPositionChanged: mouse => {
                                            if (!pressed)
                                                return
                                            const point = mapToItem(
                                                surfaceHost, mouse.x, mouse.y)
                                            const centerX = skillMarker.x + skillMarker.joystickCenterX
                                            const centerY = skillMarker.y + skillMarker.joystickCenterY
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
                                            const backend = integratedBackend
                                            const index = skillMarker.index
                                            const radius = skillMarker.previewRadius
                                            skillMarker.resizingRadius = false
                                            backend.resizeMobaSkill(index, radius)
                                            backend.endMapperEditAction()
                                        }
                                        onCanceled: {
                                            skillMarker.previewRadius = modelData.radius
                                            skillMarker.resizingRadius = false
                                            integratedBackend.endMapperEditAction()
                                        }
                                    }
                                }

                                Rectangle {
                                    visible: skillMarker.expanded
                                    x: skillCircle.x + (skillCircle.width - width) / 2
                                    y: skillCircle.y + skillCircle.height + 8 / skillMarker.markerScale
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
                                              ? "⚠ КАЛИБРОВКА ИЗМЕНЕНА"
                                              : (!integratedBackend.hasCharacterCenter
                                              ? "⚠ НУЖЕН ЦЕНТР ГЕРОЯ"
                                              : (modelData.key === 0
                                                 ? "⚠ НАЗНАЧЬТЕ КЛАВИШУ"
                                                 : (modelData.calibrated
                                                    ? "✓ ОТКАЛИБРОВАНО"
                                                    : "⚠ НУЖНА КАЛИБРОВКА")))
                                        color: "white"
                                        font.bold: true
                                        font.pixelSize: 11 / skillMarker.markerScale
                                    }
                                }
                                ControlBadge {
                                    id: skillBadge
                                    objectName: "skillBadge-" + skillMarker.index
                                    anchors.centerIn: parent
                                    uiScale: skillMarker.markerScale
                                    keyText: modelData.keyName
                                    typeText: "Навык MOBA"; shape: "diamond"
                                    accent: modelData.calibrated ? "#2d9664" : "#8b50be"
                                    selected: skillMarker.expanded
                                    warning: modelData.keyConflict ? "Конфликт клавиши " + modelData.keyName
                                        : modelData.calibrationStale ? "Калибровка изменилась"
                                        : !integratedBackend.hasCharacterCenter ? "Укажите центр героя"
                                        : modelData.key === 0 ? "Назначьте клавишу"
                                        : !modelData.calibrated ? "Требуется калибровка" : ""
                                    moveTarget: skillMarker
                                    onActivated: integratedWindow.selectControl("skill", skillMarker.index)
                                    onMoved: (nx, ny) => {
                                        if (modelData.artificialCenterEnabled)
                                            integratedBackend.moveMobaSkillArtificialCenter(skillMarker.index, nx, ny)
                                        else
                                            integratedBackend.moveMobaSkill(skillMarker.index, nx, ny)
                                    }

                                    onContextRequested: (px, py) => integratedWindow.openControlMenu(skillBadge, px, py, "skill", skillMarker.index)
                                    onRebindRequested: { integratedWindow.selectControl("skill", skillMarker.index); integratedBackend.beginRebindSelectedMobaSkill() }
                                }
                            }
                        }

                        Item {
                            id: calibrationOverlay
                objectName: "calibrationOverlay"
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
                                    if (mouse.button === Qt.RightButton) {
                                        integratedBackend.moveCalibrationCharacterCenter(
                                            mouse.x / width, mouse.y / height)
                                    } else if (mouse.button === Qt.LeftButton
                                               && integratedBackend.calibrationPointReady) {
                                        integratedBackend.recordMobaSkillCalibrationPoint(
                                            mouse.x / width, mouse.y / height)
                                    }
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
                                opacity: 1.0
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
                property real savedX: 12
                property real savedY: 12
                readonly property bool compactSelection:
                    integratedBackend.centerVision.stage === 1
                    || integratedBackend.centerVision.stage === 2
                    || integratedBackend.centerVision.stage === 5
                function fitInside() {
                    x = Math.max(panelMargin,
                                 Math.min(x, integratedWindow.width - width - panelMargin))
                    y = Math.max(panelMargin,
                                 Math.min(y, integratedWindow.height - height - panelMargin))
                }
                width: Math.min(compactSelection ? 420 : 440,
                                integratedWindow.width - panelMargin * 2)
                height: Math.min(compactSelection ? 126 : 650,
                                 integratedWindow.height - panelMargin * 2)
                x: Math.max(panelMargin,
                            integratedWindow.width - width - panelMargin)
                y: panelMargin
                visible: integratedBackend.centerVision.visible
                // Keep the controls opaque: a translucent full-height panel made
                // the frozen Android frame look globally dimmed.  During template,
                // anchor and correction selection the panel also collapses to the
                // small instruction strip below.
                color: "#1a2430"
                border.color: "#5bdfaa"
                border.width: 2
                radius: 14
                clip: true
                z: 230
                onVisibleChanged: {
                    if (visible) {
                        x = savedX
                        y = savedY
                        Qt.callLater(fitInside)
                    } else {
                        savedX = x
                        savedY = y
                    }
                }
                onWidthChanged: Qt.callLater(fitInside)
                onHeightChanged: Qt.callLater(fitInside)

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
                        anchors.right: centerVisionDiagnosticsButton.left
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Поиск центра  •  F2"
                        color: "white"
                        font.bold: true
                        font.pixelSize: 16
                        elide: Text.ElideRight
                    }
                    Button {
                        id: centerVisionDiagnosticsButton
                        anchors.right: closeCenterVisionButton.left
                        anchors.rightMargin: 2
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Диагностика"
                        flat: true
                        onClicked: integratedBackend.centerVision.exportDiagnostics()
                    }
                    Button {
                        id: closeCenterVisionButton
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
                        anchors.rightMargin: 154
                        cursorShape: Qt.SizeAllCursor
                        preventStealing: true
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
                            centerVisionPanel.savedX = centerVisionPanel.x
                            centerVisionPanel.savedY = centerVisionPanel.y
                        }
                    }
                }

                Label {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: centerVisionHeader.bottom
                    anchors.margins: 12
                    visible: centerVisionPanel.compactSelection
                    text: integratedBackend.centerVision.status
                    color: "#e7f0f7"
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }

                ScrollView {
                    id: centerVisionScroll
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: centerVisionHeader.bottom
                    anchors.bottom: parent.bottom
                    visible: !centerVisionPanel.compactSelection
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                    ColumnLayout {
                        x: 14
                        width: Math.max(1, centerVisionScroll.availableWidth - 28)
                        spacing: 10

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.topMargin: 12
                            Layout.preferredHeight: autoTestColumn.implicitHeight + 22
                            radius: 10
                            color: "#142c27"
                            border.width: 2
                            border.color: integratedBackend.centerVision.autotestResult === "PASS"
                                          ? "#4ee39a"
                                          : integratedBackend.centerVision.autotestResult === "FAIL"
                                            ? "#ff6670" : "#4ba3d9"

                            ColumnLayout {
                                id: autoTestColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 11
                                spacing: 7

                                Label {
                                    Layout.fillWidth: true
                                    text: "ОТСЛЕЖИВАНИЕ ЦЕНТРА · РЕЖИМ НАБЛЮДЕНИЯ"
                                    color: "#8df0c1"
                                    font.bold: true
                                    wrapMode: Text.WordWrap
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: "Безопасный тест: зрение только наблюдает и не меняет центр рабочего маппера."
                                    color: "#b8cdc7"
                                    wrapMode: Text.WordWrap
                                }
                                Button {
                                    Layout.fillWidth: true
                                    visible: !integratedBackend.centerVision.autotestRunning
                                    text: "Запустить автоматический тест — 10 минут"
                                    enabled: integratedBackend.centerVision.hasReference
                                    onClicked: integratedBackend.centerVision.startAutotest()
                                }
                                ProgressBar {
                                    Layout.fillWidth: true
                                    visible: integratedBackend.centerVision.autotestRunning
                                    from: 0
                                    to: 1
                                    value: integratedBackend.centerVision.autotestProgress
                                }
                                Label {
                                    Layout.fillWidth: true
                                    visible: integratedBackend.centerVision.autotestRunning
                                    readonly property int elapsed:
                                        integratedBackend.centerVision.autotestElapsedSeconds
                                    text: "Идёт тест: " + Math.floor(elapsed / 60) + ":"
                                          + (elapsed % 60 < 10 ? "0" : "")
                                          + (elapsed % 60) + " / 10:00"
                                    color: "white"
                                    horizontalAlignment: Text.AlignHCenter
                                    font.bold: true
                                }
                                Button {
                                    Layout.fillWidth: true
                                    visible: integratedBackend.centerVision.autotestRunning
                                    text: "Завершить раньше и собрать архив"
                                    onClicked: integratedBackend.centerVision.finishAutotestEarly()
                                }
                                Label {
                                    Layout.fillWidth: true
                                    visible: integratedBackend.centerVision.autotestResult !== "NOT RUN"
                                    text: integratedBackend.centerVision.autotestSummary
                                    color: integratedBackend.centerVision.autotestResult === "PASS"
                                           ? "#7af0af"
                                           : integratedBackend.centerVision.autotestResult === "FAIL"
                                             ? "#ff8d94" : "#ffd178"
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: integratedBackend.centerVision.status
                            color: "#e7f0f7"
                            wrapMode: Text.WordWrap
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 166
                            radius: 8
                            color: "#18232d"
                            border.color: "#3e5365"

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 9
                                spacing: 3
                                Label {
                                    Layout.fillWidth: true
                                    text: "Этап: " + integratedBackend.centerVision.stageName
                                    color: "#b9cbda"
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: "Состояние: "
                                          + integratedWindow.visionStateName(integratedBackend.centerVision.trackingState)
                                    color: integratedBackend.centerVision.trackingState === "LOCKED"
                                           ? "#60e9a0" : "#ffbf5a"
                                    font.bold: true
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: "Совпадение: "
                                          + (integratedBackend.centerVision.score * 100).toFixed(1)
                                          + "%"
                                    color: "#dbe7ef"
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: "Уверенность: "
                                          + (integratedBackend.centerVision.confidence * 100).toFixed(1)
                                          + "%"
                                    color: "#dbe7ef"
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: "HP героя: "
                                          + (integratedBackend.centerVision.heroGradientScore * 100).toFixed(0)
                                          + "%"
                                    color: integratedBackend.centerVision.heroGradientScore > 0
                                           ? "#7df26d" : "#91a8ba"
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: "Кадр: " + integratedBackend.centerVision.frameNumber
                                    color: "#91a8ba"
                                }
                                Label {
                                    Layout.fillWidth: true
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
                            enabled: !integratedBackend.centerVision.autotestRunning
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

                        ColumnLayout {
                            Layout.fillWidth: true
                            Button {
                                Layout.fillWidth: true
                                text: integratedBackend.centerVision.tracking
                                      ? "Остановить слежение" : "Начать слежение"
                                enabled: integratedBackend.centerVision.hasReference
                                         && !integratedBackend.centerVision.autotestRunning
                                onClicked: integratedBackend.centerVision.tracking
                                           ? integratedBackend.centerVision.stopTracking()
                                           : integratedBackend.centerVision.startTracking()
                            }
                            Button {
                                Layout.fillWidth: true
                                text: "✓ Хорошо"
                                enabled: integratedBackend.centerVision.frameNumber > 0
                                         && !integratedBackend.centerVision.autotestRunning
                                onClicked: integratedBackend.centerVision.markGood()
                            }
                        }
                        Button {
                            Layout.fillWidth: true
                            text: "✕ Центр неверный — показать правильный"
                            enabled: integratedBackend.centerVision.frameNumber > 0
                                     && !integratedBackend.centerVision.autotestRunning
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
                            enabled: !integratedBackend.centerVision.autotestRunning
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
                        ColumnLayout {
                            Layout.fillWidth: true
                            Button {
                                Layout.fillWidth: true
                                text: "Открыть логи"
                                onClicked: integratedBackend.centerVision.openSessionFolder()
                            }
                            Button {
                                Layout.fillWidth: true
                                text: "Собрать диагностику зрения"
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
                        text: "Профили · F6"
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
                                  ? (modelData.active ? "ВЫБРАН" : modelData.statusText)
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
                    ToolTip.text: "Создать пустой профиль для текущего разрешения"
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
                visible: integratedBackend.editMode && !integratedWindow.uranusPicking
                         && !integratedBackend.calibrationActive
                         && !integratedWindow.geometrySearching
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 12
                width: Math.min(parent.width - 24, 680)
                height: 48
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
                        text: "Редактор · " + integratedBackend.activeProfileName
                        color: "white"
                        font.bold: true
                        font.pixelSize: 14
                        elide: Text.ElideRight
                    }
                    Label {
                        visible: parent.width > 500
                        text: "Клик — выбрать · ПКМ — настройки"
                        font.pixelSize: 11
                        color: "#b4c5d6"
                    }
                    Button {
                        visible: integratedWindow.calibrationIssueCount > 0
                        text: "⚠ " + integratedWindow.calibrationIssueCount
                        ToolTip.visible: hovered
                        ToolTip.text: "Элементы, требующие калибровки или настройки"
                        onClicked: {
                            const skills = integratedBackend.mobaSkills
                            for (let i = 0; i < skills.length; ++i) {
                                if (skills[i].calibrationStale || !skills[i].calibrated
                                        || !integratedBackend.hasCharacterCenter || skills[i].key === 0) {
                                    integratedWindow.selectControl("skill", i)
                                    break
                                }
                            }
                        }
                    }
                    Button {
                        text: "Готово"
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
                    text: "КУРСОР ЗАКРЕПЛЁН · F12"
                    color: "white"
                    font.bold: true
                }
            }

            Menu {
                id: addControlMenu
                parent: surfaceArea
                enabled: integratedBackend.editMode

                MenuItem {
                    text: "Нажатие / удержание"
                    onTriggered: integratedBackend.addTapAt(
                        integratedWindow.contextTapX,
                        integratedWindow.contextTapY)
                }
                MenuSeparator { }
                MenuItem {
                    text: "Центр героя (крестик)"
                    onTriggered: integratedBackend.addCharacterCenterAt(
                        integratedWindow.contextTapX,
                        integratedWindow.contextTapY)
                }
                MenuItem {
                    text: integratedBackend.hasCharacterCenter
                          ? "Движение MOBA (WASD или мышь)"
                          : "⚠ Движение MOBA — нужен центр героя"
                    onTriggered: integratedBackend.addMobaMovementAt(
                        integratedWindow.contextTapX,
                        integratedWindow.contextTapY)
                }
                MenuItem {
                    text: integratedBackend.hasCharacterCenter
                          ? "Навык MOBA"
                          : "⚠ Навык MOBA — нужен центр героя"
                    onTriggered: integratedBackend.addMobaSkillAt(
                        integratedWindow.contextTapX,
                        integratedWindow.contextTapY)
                }
                MenuItem {
                    text: integratedBackend.hasSkillCancel
                          ? "Перенести точку отмены сюда"
                          : "Отмена навыка"
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
                objectName: "controlContextMenu"
                parent: surfaceArea
                Component.onCompleted: contentItem.objectName = "controlContextMenuContent"
                enabled: integratedBackend.editMode

                MenuItem {
                    objectName: "controlSettingsMenuItem"
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
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                        Layout.fillWidth: true
                        text: "Сохранить кнопку в багаж"
                        font.bold: true
                        font.pixelSize: 20
                    }
                        PopupCloseMark { objectName: "baggageNamePopupClose"; onTriggered: baggageNamePopup.close() }
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
                title: "Выберите изображение профиля"
                fileMode: FileDialog.OpenFile
                nameFilters: ["Изображения (*.png *.jpg *.jpeg *.webp *.bmp *.gif)"]
                onAccepted: {
                    integratedBackend.setProfileImage(profileId, selectedFile)
                }
            }

            Popup {
                id: renameProfilePopup
                property string profileId: ""
                property string profileName: ""
                property bool uppercase: false
                anchors.centerIn: Overlay.overlay
                width: Math.min(390, surfaceArea.width - 32)
                height: 340
                modal: true
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                        Layout.fillWidth: true
                        text: "Переименовать профиль"
                        font.bold: true
                        font.pixelSize: 19
                    }
                        PopupCloseMark { objectName: "renameProfilePopupClose"; onTriggered: renameProfilePopup.close() }
                    }
                    TextField {
                        id: renameField
                        Layout.fillWidth: true
                        maximumLength: 64
                        placeholderText: "Название профиля"
                        onAccepted: {
                            integratedBackend.renameProfile(
                                renameProfilePopup.profileId, text)
                            renameProfilePopup.close()
                        }
                    }
                    Label {
                        text: "Если раскладка не вводит кириллицу, используйте буквы ниже:"
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 2
                        Repeater {
                            model: "йцукенгшщзхъфывапролджэячсмитьбюё".split("")
                            Button {
                                width: 30
                                height: 28
                                padding: 0
                                text: renameProfilePopup.uppercase
                                      ? modelData.toUpperCase() : modelData
                                onClicked: {
                                    renameField.insert(renameField.cursorPosition, text)
                                    renameField.forceActiveFocus()
                                }
                            }
                        }
                    }
                    RowLayout {
                        Button {
                            text: renameProfilePopup.uppercase ? "АБВ" : "абв"
                            onClicked: renameProfilePopup.uppercase =
                                !renameProfilePopup.uppercase
                        }
                        Button {
                            text: "Пробел"
                            onClicked: renameField.insert(renameField.cursorPosition, " ")
                        }
                        Button {
                            text: "⌫"
                            onClicked: {
                                if (renameField.cursorPosition > 0)
                                    renameField.remove(renameField.cursorPosition - 1,
                                                       renameField.cursorPosition)
                            }
                        }
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        Button {
                            text: "Отмена"
                            onClicked: renameProfilePopup.close()
                        }
                        Button {
                            text: "Сохранить"
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
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                        Layout.fillWidth: true
                        text: "Удалить профиль?"
                        font.bold: true
                        font.pixelSize: 19
                    }
                        PopupCloseMark { objectName: "deleteProfilePopupClose"; onTriggered: deleteProfilePopup.close() }
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
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                        Layout.fillWidth: true
                        text: integratedBackend.pendingProfile.name || "Профиль"
                        font.bold: true
                        font.pixelSize: 20
                        elide: Text.ElideRight
                    }
                        PopupCloseMark { objectName: "profileAdaptationPopupClose"; onTriggered: { integratedBackend.cancelPendingProfileSwitch(); profileAdaptationPopup.close() } }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: integratedBackend.pendingProfile.isDefault
                              ? "Профиль Default закреплён за разрешением "
                                + integratedBackend.pendingProfile.sourceResolution
                                + ". Его нельзя перенести на "
                                + integratedBackend.pendingProfile.targetResolution
                                + ". Измените разрешение Android или выберите другой профиль."
                              : "Профиль ещё не настроен для "
                                + integratedBackend.pendingProfile.targetResolution
                                + ". Ближайший настроенный вариант: "
                                + integratedBackend.pendingProfile.sourceResolution + "."
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: !integratedBackend.pendingProfile.isDefault
                        wrapMode: Text.WordWrap
                        text: "Автоадаптация пропорционально переносит позиции, размеры, "
                              + "клавиши и калибровку. Создание с нуля добавляет "
                              + "пустой вариант для ручной настройки."
                        color: "#718096"
                    }
                    Item { Layout.fillHeight: true }
                    RowLayout {
                        Layout.fillWidth: true
                        layoutDirection: Qt.RightToLeft
                        Button {
                            text: "Отмена"
                            onClicked: {
                                integratedBackend.cancelPendingProfileSwitch()
                                profileAdaptationPopup.close()
                            }
                        }
                        Button {
                            visible: !integratedBackend.pendingProfile.isDefault
                            text: "Создать с нуля"
                            onClicked: {
                                integratedBackend.createPendingProfileFromScratch()
                                profileAdaptationPopup.close()
                            }
                        }
                        Button {
                            visible: integratedBackend.pendingProfile.canAdapt === true
                            text: "Адаптировать"
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
                objectName: "bindingSettings"
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
                    objectName: "bindingSettingsContent"
                    anchors.fill: parent
                    spacing: 12

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        color: "#263747"
                        radius: 6

                        Label {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Нажатие · Настройки"
                            color: "white"
                            font.bold: true
                            font.pixelSize: 16
                        }
                        MouseArea {
                            property real grabX: 0
                            property real grabY: 0
                            anchors.fill: parent
                            anchors.rightMargin: 42
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
                        PopupCloseMark {
                            objectName: "bindingSettingsClose"
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            foreground: "white"
                            onTriggered: bindingSettings.close()
                        }
                    }

                    GridLayout {
                        columns: 2
                        Layout.fillWidth: true
                        columnSpacing: 10
                        rowSpacing: 8

                        Label { text: "X" }
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

                        Label { text: "Y" }
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

                        Label { text: "Клавиша" }
                        Button {
                            Layout.fillWidth: true
                            text: integratedBackend.waitingForKey
                                  ? "Нажмите клавишу или кнопку мыши…"
                                  : "Клавиша: " + integratedBackend.selectedBinding.keyName
                            onClicked: integratedBackend.beginRebindSelected()
                        }

                        Label { text: "Режим" }
                        ComboBox {
                            Layout.fillWidth: true
                            model: ["Короткое нажатие", "До отпускания клавиши"]
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
                              ? "Нажмите клавишу; Esc — отмена"
                              : (bindingSettings.modeValue === 0
                                 ? "Короткое нажатие длится 35 мс"
                                 : "Касание удерживается до отпускания клавиши или кнопки мыши")
                        color: "#718096"
                    }
                }
            }

            Popup {
                id: characterCenterSettings
                objectName: "characterCenterSettings"
                property string searchMessage: ""
                property int xValue: 0
                property int yValue: 0
                x: Math.max(0, (surfaceArea.width - width) / 2)
                y: Math.max(0, (surfaceArea.height - height) / 2)
                width: Math.min(440, surfaceArea.width - 24)
                height: Math.min(searchMessage.length > 0 ? 280 : 230, surfaceArea.height - 24)
                modal: false
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                ColumnLayout {
                    objectName: "characterCenterSettingsContent"
                    anchors.fill: parent
                    spacing: 12
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        color: "#512633"
                        radius: 6
                        Label {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Центр героя · Настройки"
                            color: "white"
                            font.bold: true
                        }
                        MouseArea {
                            property real grabX: 0
                            property real grabY: 0
                            anchors.fill: parent
                            anchors.rightMargin: 42
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
                        PopupCloseMark {
                            objectName: "characterCenterSettingsClose"
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            foreground: "white"
                            onTriggered: characterCenterSettings.close()
                        }
                    }
                    Button {
                        objectName: "uranusSearchButton"
                        Layout.fillWidth: true
                        text: "Автопоиск Урануса"
                        enabled: typeof integratedBackend.beginUranusCenterSearch === "function"
                        onClicked: {
                            characterCenterSettings.close()
                            integratedWindow.uranusPicking = true
                        }
                    }
                    Label {
                        visible: characterCenterSettings.searchMessage.length > 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: characterCenterSettings.searchMessage
                        color: "#516477"
                    }
                    GridLayout {
                        columns: 4
                        Layout.fillWidth: true
                        Label { text: "X" }
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
                        text: "Центр общий для всех профилей разрешения " + integratedBackend.androidWidth + " × " + integratedBackend.androidHeight + ". При переносе исходные калибровки требуют проверки."
                        color: "#718096"
                    }
                }
            }

            Popup {
                id: movementSettings
                objectName: "movementSettings"
                property int xValue: 0
                property int yValue: 0
                property int thresholdValue: 120
                property int distanceValue: 100
                x: Math.max(0, (surfaceArea.width - width) / 2)
                y: Math.max(0, (surfaceArea.height - height) / 2)
                width: Math.min(470, surfaceArea.width - 24)
                height: Math.min(590, movementSettingsContent.implicitHeight + topPadding + bottomPadding, surfaceArea.height - 24)
                modal: false
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                ScrollView {
                    id: movementSettingsScroll
                    anchors.fill: parent
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                    id: movementSettingsContent
                    objectName: "movementSettingsContent"
                    width: movementSettingsScroll.availableWidth
                    spacing: 11

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        color: "#174f68"
                        radius: 6

                        Label {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Движение MOBA · Настройки"
                            color: "white"
                            font.bold: true
                            font.pixelSize: 16
                        }
                        MouseArea {
                            property real grabX: 0
                            property real grabY: 0
                            anchors.fill: parent
                            anchors.rightMargin: 42
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
                        PopupCloseMark {
                            objectName: "movementSettingsClose"
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            foreground: "white"
                            onTriggered: movementSettings.close()
                        }
                    }

                    TabBar {
                        Layout.fillWidth: true
                        currentIndex: integratedBackend.mobaMovement.experimental ? 1 : 0
                        TabButton {
                            objectName: "originalMovementTab"
                            text: "Оригинальный"
                            onClicked: integratedBackend.setMobaMovementExperimental(false)
                        }
                        TabButton {
                            objectName: "experimentalMovementTab"
                            text: "Экспериментальный"
                            onClicked: integratedBackend.setMobaMovementExperimental(true)
                        }
                    }
                    Label {
                        visible: integratedBackend.mobaMovement.experimental === true
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: integratedWindow.sharedCalibration.angleReady
                            ? "Общий угол · " + integratedWindow.sharedCalibration.resolution + ". Применяется к мыши и WASD."
                            : "Общий угол не настроен для этого разрешения. Выполните «Калибровку угла» в экспериментальных настройках любого MOBA-навыка. Движение пока недоступно."
                        color: integratedWindow.sharedCalibration.angleReady ? "#516477" : "#b87500"
                    }

                    GridLayout {
                        columns: 4
                        Layout.fillWidth: true
                        Label { text: "X" }
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
                        Label { text: "Управление" }
                        ComboBox {
                            Layout.fillWidth: true
                            model: ["Курсор + кнопка мыши", "WASD"]
                            currentIndex: integratedBackend.mobaMovement.inputMode
                            onActivated: integratedBackend.setMobaMovementInputMode(currentIndex)
                        }
                        Label {
                            text: "Кнопка мыши"
                            visible: integratedBackend.mobaMovement.inputMode === 0
                        }
                        ComboBox {
                            Layout.fillWidth: true
                            visible: integratedBackend.mobaMovement.inputMode === 0
                            model: ["ПКМ", "ЛКМ", "СКМ", "Мышь 4", "Мышь 5"]
                            property var buttonValues: [2, 1, 4, 8, 16]
                            currentIndex: Math.max(0, buttonValues.indexOf(
                                integratedBackend.mobaMovement.mouseButton))
                            onActivated: integratedBackend.setMobaMovementMouseButton(
                                buttonValues[currentIndex])
                        }
                    }

                    GridLayout {
                        columns: 2
                        Layout.fillWidth: true
                        visible: integratedBackend.mobaMovement.inputMode === 0
                        Label { text: "Порог удержания" }
                        SpinBox {
                            Layout.fillWidth: true
                            from: 30
                            to: 500
                            stepSize: 5
                            editable: true
                            value: movementSettings.thresholdValue
                            textFromValue: (value, locale) => value + " мс"
                            valueFromText: (text, locale) => parseInt(text)
                            onValueModified:
                                integratedBackend.setMobaMovementHoldThreshold(value)
                        }

                        Label { text: "Длительность движения" }
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
                        text: integratedBackend.mobaMovement.inputMode === 1
                              ? "WASD двигает круговой джойстик. Диагонали нормализованы; отпускание всех клавиш снимает палец."
                              : "Удерживайте кнопку мыши дольше порога, чтобы двигаться за курсором. "
                              + "Короткий клик запускает движение в выбранном направлении; длительность "
                              + "зависит от расстояния до центра героя. Новое нажатие "
                              + "отменяет предыдущий маршрут."
                        color: "#718096"
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        visible: integratedBackend.mobaMovement.inputMode === 0
                        text: "При 100% клик на расстоянии короткой стороны экрана от центра задаёт "
                              + "движение на 1600 мс. Этот параметр изменяет длительность."
                        color: "#718096"
                    }
                    }
                }
            }

            Popup {
                id: cancelSettings
                objectName: "cancelSettings"
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
                    objectName: "cancelSettingsContent"
                    anchors.fill: parent
                    spacing: 12

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        color: "#5a2430"
                        radius: 6

                        Label {
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Отмена навыка · Настройки"
                            color: "white"
                            font.bold: true
                            font.pixelSize: 16
                        }
                        MouseArea {
                            property real grabX: 0
                            property real grabY: 0
                            anchors.fill: parent
                            anchors.rightMargin: 42
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
                        PopupCloseMark {
                            objectName: "cancelSettingsClose"
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            foreground: "white"
                            onTriggered: cancelSettings.close()
                        }
                    }

                    GridLayout {
                        columns: 4
                        Layout.fillWidth: true
                        Label { text: "X" }
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
                            text: "Клавиша отмены"
                            font.bold: true
                        }
                        Button {
                            Layout.fillWidth: true
                            text: integratedBackend.waitingForKey
                                  ? "Нажмите клавишу или кнопку мыши…"
                                  : "Клавиша: " + integratedBackend.skillCancel.keyName
                            onClicked: integratedBackend.beginRebindSkillCancel()
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "При удержании навыка клавиша отмены переводит палец в эту точку и отпускает его."
                        color: "#718096"
                    }
                }
            }

            Popup {
                id: skillSettings
                objectName: "skillSettingsPopup"
                property int xValue: 0
                property int yValue: 0
                property int diameterValue: 120
                property string autoGeometryMessage: ""
                property int modeValue: 0
                property int startSpeedMsValue: 10
                property bool cancellableValue: true
                property int cancelDurationValue: 65
                property bool artificialEnabled: false
                property int editedSkillIndex: -1
                property int artificialDelayMsValue: 1
                property int artificialXValue: 0
                property int artificialYValue: 0
                function loadValues() {
                    editedSkillIndex = integratedBackend.selectedMobaSkill.index
                    xValue = integratedBackend.selectedMobaSkill.pixelX
                    yValue = integratedBackend.selectedMobaSkill.pixelY
                    diameterValue =
                        integratedBackend.selectedMobaSkill.diameterPixels
                    modeValue = integratedBackend.selectedMobaSkill.mode
                    startSpeedMsValue =
                        integratedBackend.selectedMobaSkill.startSpeedMs
                    cancellableValue =
                        integratedBackend.selectedMobaSkill.cancellable
                    const duration = integratedBackend.selectedMobaSkill.cancelDurationMs
                    cancelDurationValue = duration === undefined
                        ? [180, 110, 65, 30, 0][Math.max(0, Math.min(4,
                            integratedBackend.selectedMobaSkill.cancelReactionLevel - 1))]
                        : duration
                    artificialEnabled =
                        integratedBackend.selectedMobaSkill.artificialCenterEnabled
                    artificialDelayMsValue =
                        integratedBackend.selectedMobaSkill.artificialCenterDelayMs
                    artificialXValue =
                        integratedBackend.selectedMobaSkill.artificialPixelX
                    artificialYValue =
                        integratedBackend.selectedMobaSkill.artificialPixelY
                }
                x: Math.max(0, (surfaceArea.width - width) / 2)
                y: Math.max(0, (surfaceArea.height - height) / 2)
                width: Math.min(600, surfaceArea.width - 24)
                height: Math.min(650, surfaceArea.height - 24)
                modal: false
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                onClosed: integratedBackend.selectMobaSkill(-1)

                ColumnLayout {
                    objectName: "skillSettingsContent"
                    anchors.fill: parent
                    spacing: 7

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        color: "#3c2749"
                        radius: 6

                        Label {
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Навык " + integratedBackend.selectedMobaSkill.keyName + " · Настройки"
                            color: "white"
                            font.bold: true
                            font.pixelSize: 15
                        }
                        MouseArea {
                            property real grabX: 0
                            property real grabY: 0
                            anchors.fill: parent
                            anchors.rightMargin: 42
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
                        Rectangle {
                            id: skillSettingsClose
                            objectName: "skillSettingsClose"
                            property bool mapperCloseControl: true
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.rightMargin: 4
                            width: 32; height: 30
                            z: 2
                            radius: 5
                            color: skillSettingsCloseMouse.containsMouse ? "#654778" : "transparent"
                            ToolTip.visible: skillSettingsCloseMouse.containsMouse
                            ToolTip.text: "Закрыть"
                            Text {
                                anchors.centerIn: parent
                                text: "×"
                                color: "white"
                                font.pixelSize: 24
                            }
                            MouseArea {
                                id: skillSettingsCloseMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: skillSettings.close()
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
                            spacing: 6

                            Frame {
                                Layout.fillWidth: true
                                padding: 7
                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 5
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label {
                                            text: "Геометрия"
                                            font.bold: true
                                            font.pixelSize: 14
                                            Layout.fillWidth: true
                                        }
                                        Button {
                                            objectName: "autoGeometryButton"
                                            text: "АвтоГеометрия"
                                            enabled: integratedWindow.autoGeometryAvailable
                                                     && !integratedBackend.waitingForKey
                                                     && !integratedBackend.calibrationActive
                                            ToolTip.visible: hovered
                                            ToolTip.text: integratedWindow.autoGeometryAvailable
                                                ? "Удержать кнопку и найти внешний круг джойстика"
                                                : "Для нового анализатора запустите обновлённый EWM"
                                            onClicked: {
                                                const index = skillSettings.editedSkillIndex
                                                skillSettings.close()
                                                integratedBackend.beginAutoGeometry(index)
                                            }
                                        }
                                    }
                                    GridLayout {
                                        columns: 6
                                        columnSpacing: 6
                                        Layout.fillWidth: true
                                        Label { text: "X" }
                                        SpinBox {
                                            id: skillPositionX
                                            objectName: "skillPositionX"
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
                                            objectName: "skillPositionY"
                                            Layout.fillWidth: true
                                            from: 0
                                            to: integratedBackend.androidHeight
                                            editable: true
                                            value: skillSettings.yValue
                                            onValueModified: integratedBackend
                                                .setSelectedMobaSkillPosition(
                                                    skillPositionX.value, value)
                                        }
                                        Label { text: "Диаметр" }
                                        SpinBox {
                                            objectName: "skillDiameter"
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
                                    Label {
                                        Layout.fillWidth: true
                                        visible: skillSettings.autoGeometryMessage.length > 0
                                        text: skillSettings.autoGeometryMessage
                                        wrapMode: Text.WordWrap
                                        color: "#516477"
                                    }
                                }
                            }

                            Frame {
                                Layout.fillWidth: true
                                padding: 7
                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 5
                                    Label {
                                        text: "Управление"
                                        font.bold: true
                                        font.pixelSize: 14
                                    }
                                    GridLayout {
                                        columns: 4
                                        Layout.fillWidth: true
                                        columnSpacing: 6
                                        rowSpacing: 5
                                        Label { text: "Клавиша" }
                                        Button {
                                            objectName: "skillKeyBind"
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            text: integratedBackend.waitingForKey
                                                  ? "Нажмите клавишу…"
                                                  : "" + integratedBackend
                                                    .selectedMobaSkill.keyName
                                            onClicked: integratedBackend
                                                .beginRebindSelectedMobaSkill()
                                        }
                                        Label { text: "Разгон" }
                                        SpinBox {
                                            objectName: "skillStartSpeed"
                                            Layout.fillWidth: true
                                            Layout.preferredWidth: 112
                                            Layout.maximumWidth: 140
                                            from: 0
                                            to: 1000
                                            editable: true
                                            value: skillSettings.startSpeedMsValue
                                            textFromValue: (value) => value + " мс"
                                            valueFromText: (text) => {
                                                const parsed = parseInt(text)
                                                return isNaN(parsed) ? value : parsed
                                            }
                                            onValueModified: {
                                                skillSettings.startSpeedMsValue = value
                                                integratedBackend
                                                    .setSelectedMobaSkillStartSpeedMs(
                                                        value)
                                            }
                                        }
                                        Label { text: "Режим" }
                                        ComboBox {
                                            Layout.fillWidth: true
                                            Layout.columnSpan: 3
                                            Layout.minimumWidth: 0
                                            model: ["За курсором · отпустить для применения"]
                                            currentIndex: 0
                                            onActivated: (index) => integratedBackend
                                                .setSelectedMobaSkillMode(index)
                                        }
                                    }
                                }
                            }

                            Frame {
                                Layout.fillWidth: true
                                padding: 7
                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 5

                                    RowLayout {
                                        Layout.fillWidth: true
                                        CheckBox {
                                            font.bold: true
                                            text: "Отмена навыка"
                                            checked: skillSettings.cancellableValue
                                            onToggled: {
                                                skillSettings.cancellableValue = checked
                                                integratedBackend
                                                    .setSelectedMobaSkillCancellable(checked)
                                            }
                                        }
                                        Label { text: "Переход"; enabled: skillSettings.cancellableValue }
                                        SpinBox {
                                            objectName: "skillCancelDuration"
                                            Layout.fillWidth: true
                                            from: 0
                                            to: 5000
                                            stepSize: 1
                                            editable: true
                                            enabled: skillSettings.cancellableValue
                                                && typeof integratedBackend.setSelectedMobaSkillCancelDurationMs === "function"
                                            value: skillSettings.cancelDurationValue
                                            textFromValue: (value) => value + " мс"
                                            valueFromText: (text) => {
                                                const parsed = parseInt(text)
                                                return isNaN(parsed) ? value : parsed
                                            }
                                            ToolTip.visible: hovered
                                            ToolTip.text: "Время перехода пальца к точке отмены. Меньше — быстрее; 0 мс — мгновенно."
                                            onValueModified: {
                                                skillSettings.cancelDurationValue = value
                                                integratedBackend.setSelectedMobaSkillCancelDurationMs(value)
                                            }
                                        }
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        text: !skillSettings.cancellableValue
                                              ? "Отмена отключена"
                                              : (!integratedBackend.hasSkillCancel
                                                 ? "⚠ Добавьте точку отмены через ПКМ."
                                                 : (integratedBackend.skillCancel.key === 0
                                                    ? "⚠ Назначьте клавишу отмены."
                                                    : "Клавиша отмены: "
                                                      + integratedBackend.skillCancel.keyName))
                                        color: !skillSettings.cancellableValue
                                               || integratedBackend.skillCancel.ready
                                               ? "#218c4f" : "#b86700"
                                        font.bold: true
                                    }

                                }
                            }

                            Frame {
                                Layout.fillWidth: true
                                padding: 7
                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 5

                                    CheckBox {
                                        text: "Искусственный центр"
                                        font.bold: true
                                        ToolTip.visible: hovered
                                        ToolTip.text: "Нажатие в отдельной точке, затем переход к центру джойстика."
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
                                        columns: 6
                                        columnSpacing: 6
                                        Layout.fillWidth: true
                                        enabled: skillSettings.artificialEnabled
                                        Label { text: "X" }
                                        SpinBox {
                                            id: artificialCenterX
                                            objectName: "artificialCenterX"
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
                                            objectName: "artificialCenterY"
                                            Layout.fillWidth: true
                                            from: 0
                                            to: integratedBackend.androidHeight
                                            editable: true
                                            value: skillSettings.artificialYValue
                                            onValueModified: integratedBackend
                                                .setSelectedMobaSkillArtificialCenterPosition(
                                                    artificialCenterX.value, value)
                                        }
                                        Label { text: "Переход" }
                                        SpinBox {
                                            objectName: "artificialCenterDelay"
                                            Layout.fillWidth: true
                                            from: 1
                                            to: 1000
                                            editable: true
                                            value: skillSettings.artificialDelayMsValue
                                            textFromValue: (value) => value + " мс"
                                            valueFromText: (text) => {
                                                const parsed = parseInt(text)
                                                return isNaN(parsed) ? value : parsed
                                            }
                                            onValueModified: {
                                                skillSettings.artificialDelayMsValue = value
                                                integratedBackend
                                                    .setMobaSkillArtificialCenterDelayMs(
                                                        skillSettings.editedSkillIndex,
                                                        value)
                                            }
                                        }
                                    }

                                }
                            }

                            Frame {
                                Layout.fillWidth: true
                                padding: 7
                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 5
                                    Label {
                                        text: "Калибровка перспективы"
                                        font.bold: true
                                        font.pixelSize: 14
                                    }
                                    CalibrationSwitch {}
                                    ExperimentalControls { visible: integratedWindow.experimentalSelected }
                                    Frame {
                                        Layout.fillWidth: true
                                        visible: !integratedWindow.experimentalSelected && integratedBackend.selectedMobaSkill
                                            .calibrationStale === true
                                        background: Rectangle {
                                            radius: 7
                                            color: "#fff0cf"
                                            border.color: "#e0a322"
                                        }
                                        ColumnLayout {
                                            anchors.fill: parent
                                            spacing: 5
                                            Label {
                                                Layout.fillWidth: true
                                                wrapMode: Text.WordWrap
                                                text: "Геометрия изменена — проверьте калибровку."
                                                color: "#7a4b00"
                                                font.bold: true
                                            }
                                            RowLayout {
                                                Layout.fillWidth: true
                                                Button {
                                                    Layout.fillWidth: true
                                                    text: "Подтвердить"
                                                    onClicked: integratedBackend
                                                        .acceptSelectedMobaSkillCalibration()
                                                }
                                                Button {
                                                    Layout.fillWidth: true
                                                    text: "Восстановить"
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
                                        visible: !integratedWindow.experimentalSelected
                                        Layout.fillWidth: true
                                        Label {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            wrapMode: Text.WordWrap
                                            text: integratedBackend.selectedMobaSkill
                                                  .calibrated
                                                  ? "✓ Готово — "
                                                    + integratedBackend.selectedMobaSkill
                                                        .calibrationCount
                                                    + "/"
                                                    + integratedBackend.selectedMobaSkill
                                                        .calibrationExpected
                                                    + " • "
                                                    + (integratedBackend.selectedMobaSkill
                                                        .calibrationVersion === 1
                                                       ? "Быстрая"
                                                       : (integratedBackend.selectedMobaSkill
                                                          .calibrationVersion === 3
                                                          ? "Направленная"
                                                          : "Подробная"))
                                                  : "Требуется калибровка"
                                            color: integratedBackend.selectedMobaSkill
                                                   .calibrated
                                                   ? "#218c4f" : "#b86700"
                                            font.bold: true
                                        }
                                        Button {
                                            objectName: "beginSkillCalibration"
                                            Layout.minimumWidth: implicitWidth
                                            text: "Калибровать…"
                                            ToolTip.visible: hovered
                                            ToolTip.text: integratedBackend.selectedMobaSkill.calibrated
                                                          ? "Повторить калибровку навыка"
                                                          : "Настроить калибровку навыка"
                                            enabled: integratedBackend.hasCharacterCenter
                                                     && !integratedBackend.waitingForKey
                                            onClicked: {
                                                calibrationIntro.skillIndex =
                                                    integratedBackend.selectedMobaSkillIndex
                                                skillSettings.close()
                                                calibrationIntro.open()
                                            }
                                        }
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        visible: !integratedWindow.experimentalSelected
                                        text: !integratedBackend.hasCharacterCenter
                                              ? "Перед калибровкой добавь центр персонажа."
                                              : "Точки сохраняются при изменении геометрии."
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
                objectName: "calibrationIntro"
                property int skillIndex: -1
                anchors.centerIn: Overlay.overlay
                width: Math.min(560, surfaceArea.width - 40)
                height: Math.min(520, surfaceArea.height - 40)
                modal: true
                focus: true
                closePolicy: Popup.CloseOnEscape

                ColumnLayout {
                    objectName: "calibrationIntroContent"
                    anchors.fill: parent
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                        Layout.fillWidth: true
                        text: "Выбери калибровку"
                        font.bold: true
                        font.pixelSize: 22
                    }
                        PopupCloseMark { objectName: "calibrationIntroClose"; onTriggered: calibrationIntro.close() }
                    }

                    CalibrationSwitch {}
                    ExperimentalControls { visible: integratedWindow.experimentalSelected }
                    Item { Layout.fillHeight: true; visible: integratedWindow.experimentalSelected }
                    ListView {
                        visible: !integratedWindow.experimentalSelected
                        id: calibrationModeList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 10
                        model: ListModel {
                            ListElement {
                                modeName: "Быстрая"
                                pointCount: 24
                                calibrationVersion: 1
                            }
                            ListElement {
                                modeName: "Подробная"
                                pointCount: 66
                                calibrationVersion: 2
                            }
                            ListElement {
                                modeName: "Направленная"
                                pointCount: 64
                                calibrationVersion: 3
                            }
                        }
                        delegate: Button {
                            objectName: "calibrationMode-" + calibrationVersion
                            required property string modeName
                            required property int pointCount
                            required property int calibrationVersion
                            width: ListView.view.width
                            height: 64
                            text: modeName + " — " + pointCount + " точек"
                            font.pixelSize: 17
                            onClicked: {
                                const targetSkill = calibrationIntro.skillIndex
                                calibrationIntro.close()
                                integratedBackend.beginMobaSkillCalibration(
                                    targetSkill, calibrationVersion)
                            }
                        }
                    }

                    Button {
                        Layout.alignment: Qt.AlignRight
                        text: "Отмена"
                        onClicked: calibrationIntro.close()
                    }
                }
            }

            Timer {
                id: calibrationCenterHintTimer
                interval: 5000
                onTriggered: integratedWindow.calibrationCenterHintVisible = false
            }

            Rectangle {
                visible: integratedBackend.calibrationActive
                         && integratedBackend.calibrationHudVisible
                         && integratedWindow.calibrationCenterHintVisible
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 164
                width: Math.min(parent.width - 24, 760)
                height: 58
                radius: 9
                color: "#ed263448"
                border.color: "#69b7ff"
                z: 310
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 8
                    Label {
                        Layout.fillWidth: true
                        text: "Стрелки — подстройка центра на 1 пкс · Shift — 10 пкс · ПКМ — поставить центр"
                        color: "white"
                        wrapMode: Text.WordWrap
                        font.bold: true
                    }
                    ToolButton {
                        text: "✕"
                        onClicked: {
                            integratedWindow.calibrationCenterHintVisible = false
                            calibrationCenterHintTimer.stop()
                        }
                    }
                }
            }

            Rectangle {
                visible: integratedBackend.calibrationActive
                         && integratedBackend.calibrationHudVisible
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 12
                width: Math.min(parent.width - 24, 920)
                height: 166
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
                            Layout.minimumWidth: 0
                            elide: Text.ElideRight
                            text: "Калибровка MOBA-навыка • точка "
                                  + (integratedBackend.calibrationStep + 1)
                                  + "/" + integratedBackend.calibrationTotal
                            color: "white"
                            font.bold: true
                            font.pixelSize: 18
                        }
                        Button {
                            objectName: "offscreenRangeButton"
                            visible: integratedBackend.rangeCalibrationActive === true
                            text: "Конец за экраном"
                            enabled: integratedBackend.calibrationPointReady
                            onClicked: integratedBackend.skipOffscreenRangePoint()
                        }
                        Button {
                            text: "На точку назад"
                            enabled: integratedBackend.calibrationStep > 0
                            onClicked:
                                integratedBackend.undoMobaSkillCalibrationPoint()
                        }
                        Button {
                            text: "Отмена (Esc)"
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
                        Layout.fillWidth: true
                        text: "Центр: " + integratedBackend.characterCenter.pixelX + ", "
                              + integratedBackend.characterCenter.pixelY
                              + " · Стрелки — 1 пкс · Shift — 10 пкс · O — скрыть панель"
                        color: "#ffd166"
                        font.bold: true
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
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                        Layout.fillWidth: true
                        text: "✓ Калибровка завершена"
                        color: "#218c4f"
                        font.bold: true
                        font.pixelSize: 22
                    }
                        PopupCloseMark { objectName: "calibrationCompleteClose"; onTriggered: calibrationComplete.close() }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: integratedWindow.experimentalAvailable ? integratedBackend.calibrationSummary
                            : "Калибровка сохранена."
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "Нажми «Готово» в редакторе, чтобы сохранить профиль."
                        color: "#718096"
                    }
                    Item { Layout.fillHeight: true }
                    Button {
                        Layout.alignment: Qt.AlignRight
                        text: "Понятно"
                        onClicked: calibrationComplete.close()
                    }
                }
            }

            Connections {
                target: integratedBackend
                function onEditModeChanged() {
                    if (!integratedBackend.editMode) {
                        integratedWindow.uranusPicking = false
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
                function onCalibrationChanged() {
                    const active = integratedBackend.calibrationActive
                    if (active && !integratedWindow.calibrationWasActive) {
                        integratedWindow.calibrationCenterHintVisible = true
                        calibrationCenterHintTimer.restart()
                    } else if (!active) {
                        integratedWindow.calibrationCenterHintVisible = false
                        calibrationCenterHintTimer.stop()
                    }
                    integratedWindow.calibrationWasActive = active
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

            Connections {
                target: integratedBackend
                ignoreUnknownSignals: true
                function onAutoGeometryFinished(index, applied) {
                    if (integratedBackend.editMode && integratedBackend.windowVisible) {
                        if (index === -2) {
                            integratedWindow.openControlSettings("center", -1)
                            characterCenterSettings.searchMessage = integratedBackend.autoGeometryStatus
                        } else {
                            integratedWindow.openControlSettings("skill", index)
                            skillSettings.autoGeometryMessage = integratedBackend.autoGeometryStatus
                        }
                    }
                }
            }

            Item {
                anchors.fill: parent
                z: 9000
                visible: integratedWindow.geometrySearching
                MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
                Rectangle {
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.topMargin: 16
                    width: Math.min(parent.width - 32, 600)
                    height: searchRow.implicitHeight + 20
                    radius: 8
                    color: "#ee202a35"
                    RowLayout {
                        id: searchRow
                        anchors.fill: parent
                        anchors.margins: 10
                        BusyIndicator { running: integratedWindow.geometrySearching; Layout.preferredWidth: 28; Layout.preferredHeight: 28 }
                        Label {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            wrapMode: Text.WordWrap
                            color: "white"
                            text: integratedWindow.autoGeometryAvailable ? integratedBackend.autoGeometryStatus : ""
                        }
                        Button { text: "Отмена · Esc"; onClicked: integratedBackend.cancelAutoGeometry() }
                    }
                }
            }

            Shortcut {
                sequence: "Escape"
                enabled: integratedWindow.uranusPicking
                onActivated: {
                    integratedWindow.uranusPicking = false
                    integratedWindow.openControlSettings("center", -1)
                }
            }
            Rectangle {
                visible: integratedWindow.uranusPicking
                z: 9001
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 12
                width: Math.min(parent.width - 24, 680)
                height: pickingRow.implicitHeight + 20
                radius: 8
                color: "#ee202a35"
                RowLayout {
                    id: pickingRow
                    anchors.fill: parent; anchors.margins: 10
                    Label {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        wrapMode: Text.WordWrap
                        color: "white"
                        text: "Где ульта? Перетащите круг «У» на её кнопку."
                    }
                    Button {
                        objectName: "uranusStartHold"
                        text: "Зажать и найти"
                        onClicked: {
                            integratedWindow.uranusPicking = false
                            integratedBackend.beginUranusCenterSearch(
                                integratedWindow.uranusUltimateX, integratedWindow.uranusUltimateY)
                        }
                    }
                    PopupCloseMark {
                        foreground: "white"
                        onTriggered: {
                            integratedWindow.uranusPicking = false
                            integratedWindow.openControlSettings("center", -1)
                        }
                    }
                }
            }
}
