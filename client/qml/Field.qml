import QtQuick
import QtQuick.Controls

// 深色输入框: 细边框, 聚焦时信号黄描边。
TextField {
    id: root

    implicitHeight: 40
    color: Theme.text
    placeholderTextColor: Theme.muted
    selectByMouse: true
    selectionColor: Theme.amber
    selectedTextColor: "#0B0E13"

    font.family: Theme.bodyFont
    font.pixelSize: 13

    background: Rectangle {
        color: Theme.panel
        radius: 3
        border.width: 1
        border.color: root.activeFocus ? Theme.amber : Theme.line
        Behavior on border.color { ColorAnimation { duration: 120 } }
    }
}
