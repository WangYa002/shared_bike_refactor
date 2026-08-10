import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 账单页: 流水列表 (对照旧 RecordsView 的三列表格)。
Item {
    id: recordsPane
    objectName: "recordsPane"

    property var rows: []

    function setStatus(s, color) {
        statusLabel.text = s
        statusLabel.color = color
    }

    Rectangle { anchors.fill: parent; color: Theme.bg }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 10

        Text {
            text: "账单流水"
            font.family: Theme.zhFont
            font.pixelSize: 24
            font.weight: Font.Bold
            color: Theme.text
        }
        Text {
            text: "最近的账户变动记录"
            font.family: Theme.zhFont
            font.pixelSize: 12
            color: Theme.muted
        }

        Item { Layout.preferredHeight: 6 }

        // 表头
        RowLayout {
            Layout.fillWidth: true
            Text { text: "类型";   Layout.preferredWidth: 90;  color: Theme.muted; font.pixelSize: 11; font.family: Theme.zhFont }
            Text { text: "金额";   Layout.preferredWidth: 140; color: Theme.muted; font.pixelSize: 11; font.family: Theme.zhFont; horizontalAlignment: Text.AlignRight }
            Text { text: "时间";   Layout.fillWidth: true;     color: Theme.muted; font.pixelSize: 11; font.family: Theme.zhFont; horizontalAlignment: Text.AlignRight }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: recordsPane.rows
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                width: list.width
                height: 42
                color: index % 2 === 0 ? "transparent" : Theme.panel

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4
                    Text {
                        Layout.preferredWidth: 90
                        text: modelData.type === 1 ? "充值" : "消费"
                        color: Theme.text
                        font.pixelSize: 12
                        font.family: Theme.zhFont
                    }
                    Text {
                        Layout.preferredWidth: 140
                        horizontalAlignment: Text.AlignRight
                        text: (modelData.type === 1 ? "+ " : "") +
                              Theme.money(modelData.amount)
                        color: modelData.type === 1 ? Theme.green : Theme.red
                        font.pixelSize: 13
                        font.family: Theme.displayFont
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignRight
                        text: Theme.fmtTs(modelData.timestamp)
                        color: Theme.muted
                        font.pixelSize: 12
                        font.family: Theme.displayFont
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: list.count === 0
                text: "暂无记录，点击刷新"
                color: Theme.muted
                font.pixelSize: 12
                font.family: Theme.zhFont
            }
        }

        RowLayout {
            spacing: 10
            BikeButton {
                variant: "ghost"
                implicitHeight: 32
                text: "刷新流水"
                enabled: api.linkUp
                onClicked: {
                    recordsPane.setStatus("正在加载…", Theme.muted)
                    api.listRecords()
                }
            }
            Item { Layout.fillWidth: true }
            Label {
                id: statusLabel
                text: "点击刷新查看记录"
                color: Theme.muted
                font.family: Theme.zhFont
                font.pixelSize: 12
            }
        }
    }

    Connections {
        target: api
        function onRecordsReady(ok, records, desc) {
            if (ok) {
                recordsPane.rows = records
                recordsPane.setStatus("共 " + records.length + " 条记录", Theme.green)
            } else {
                recordsPane.setStatus("加载失败：" + desc, Theme.red)
            }
        }
        // 骑行结束后自动刷新流水 (旧 MainWindow::on_ride_ended 语义)
        function onRideEnded(ok, amountCent, balanceAfter, desc) {
            if (ok) api.listRecords()
        }
    }

    Component.onCompleted: if (api.loggedIn) api.listRecords()
}
