import QtQuick
import QtQuick.Window
Window {
    flags: Qt.FramelessWindowHint
    width: 800; height: 600; visible: true
    color: "#506446"
    property bool held: false
    property bool uranus: false
    Rectangle {
        visible: held && !uranus
        x: 480; y: 310; width: 180; height: 180; radius: 90
        color: "#6064a0e1"; border.width: 3; border.color: "#8cc3f5"
    }
    Rectangle {
        visible: held && !uranus
        x: 542; y: 372; width: 56; height: 56; radius: 28
        color: "#9064a0e1"; border.width: 3; border.color: "#8cc3f5"
    }
    Item {
        visible: held && uranus
        Rectangle {
            x: 390; y: 280; width: 80; height: 80; radius: 40
            color: "#3062e7ee"; border.width: 2; border.color: "#62e7ee"
            transform: Scale { yScale: .55 }
        }
        Rectangle { x: 428; y: 300; width: 5; height: 4; radius: 2; color: "#70ffff" }
    }
    MultiPointTouchArea {
        anchors.fill: parent
        onPressed: (points) => { uranus = points[0].x < 400; held = true; console.log("TEST_TOUCH_DOWN", points[0].x, points[0].y) }
        onReleased: { held = false; console.log("TEST_TOUCH_UP") }
        onCanceled: { held = false; console.log("TEST_TOUCH_CANCEL") }
    }
}
