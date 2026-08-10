import QtQuick
import QtQuick.Controls

// 交通标识风格按钮: primary(信号黄) / ghost(描边) / danger(警示红)。
Button {
    id: root
    property string variant: "primary"

    implicitHeight: 40
    implicitWidth: Math.max(120, label.implicitWidth + 48)
    horizontalPadding: 24

    font.family: Theme.bodyFont
    font.pixelSize: 13
    font.weight: Font.DemiBold

    Behavior on scale { NumberAnimation { duration: 90 } }
    scale: pressed ? 0.97 : 1.0

    contentItem: Text {
        id: label
        text: root.text
        font: root.font
        color: !root.enabled ? Theme.muted
             : root.variant === "primary" ? "#0B0E13"
             : root.variant === "danger"  ? Theme.text
             : Theme.amber
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: 3
        color: {
            if (!root.enabled) return Theme.panelHi
            if (root.variant === "primary")
                return root.hovered ? "#FFC94D" : Theme.amber
            if (root.variant === "danger")
                return root.hovered ? "#E64A4A" : Theme.red
            return root.hovered ? Theme.panelHi : "transparent"
        }
        border.width: root.variant === "ghost" ? 1 : 0
        border.color: root.variant === "ghost"
            ? (root.enabled ? Theme.amber : Theme.line) : "transparent"
    }
}
