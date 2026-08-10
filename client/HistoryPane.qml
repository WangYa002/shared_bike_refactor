import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 历史页: 骑行记录列表, 点击行查看订单详情+轨迹回放 (对照旧 RideHistoryView)。
Item {
    id: historyPane
    objectName: "historyPane"

    property var rides: []

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
            text: "历史骑行"
            font.family: Theme.zhFont
            font.pixelSize: 24
            font.weight: Font.Bold
            color: Theme.text
        }
        Text {
            text: "点击任一行查看完整轨迹"
            font.family: Theme.zhFont
            font.pixelSize: 12
            color: Theme.muted
        }

        Item { Layout.preferredHeight: 6 }

        RowLayout {
            Layout.fillWidth: true
            Text { text: "订单号";          Layout.preferredWidth: 200; color: Theme.muted; font.pixelSize: 11; font.family: Theme.zhFont }
            Text { text: "起始时间";        Layout.preferredWidth: 170; color: Theme.muted; font.pixelSize: 11; font.family: Theme.zhFont }
            Text { text: "距离 / 时长";     Layout.preferredWidth: 160; color: Theme.muted; font.pixelSize: 11; font.family: Theme.zhFont }
            Text { text: "金额";            Layout.fillWidth: true;     color: Theme.muted; font.pixelSize: 11; font.family: Theme.zhFont; horizontalAlignment: Text.AlignRight }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: historyPane.rides
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                id: rowBg
                width: list.width
                height: 46
                color: mouse.containsMouse ? Theme.panelHi
                     : index % 2 === 1 ? Theme.panel : "transparent"

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: rideDetail.showRide(modelData.ride_no)
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4
                    Text {
                        Layout.preferredWidth: 200
                        text: modelData.ride_no
                        color: Theme.text
                        font.pixelSize: 12
                        font.family: Theme.displayFont
                    }
                    Text {
                        Layout.preferredWidth: 170
                        text: modelData.start_tm
                        color: Theme.muted
                        font.pixelSize: 12
                        font.family: Theme.zhFont
                    }
                    Text {
                        Layout.preferredWidth: 160
                        text: (modelData.distance_m / 1000).toFixed(2) + " km / " +
                              Math.round((modelData.duration_sec + 30) / 60) + " min"
                        color: Theme.muted
                        font.pixelSize: 12
                        font.family: Theme.displayFont
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignRight
                        text: Theme.money(modelData.amount_cent)
                        color: Theme.red
                        font.pixelSize: 13
                        font.family: Theme.displayFont
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: list.count === 0
                text: "暂无历史骑行"
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
                text: "刷新"
                enabled: api.linkUp
                onClicked: {
                    historyPane.setStatus("正在加载历史…", Theme.muted)
                    api.listRides(50)
                }
            }
            Item { Layout.fillWidth: true }
            Label {
                id: statusLabel
                color: Theme.muted
                font.family: Theme.zhFont
                font.pixelSize: 12
            }
        }
    }

    Connections {
        target: api
        function onRidesReady(ok, rides, desc) {
            if (ok) {
                historyPane.rides = rides
                historyPane.setStatus("共 " + rides.length + " 条", Theme.green)
            } else {
                historyPane.setStatus("加载失败：" + desc, Theme.red)
            }
        }
    }

    Component.onCompleted: if (api.loggedIn) api.listRides(50)

    // 订单详情弹窗 (对照旧 RideDetailDialog)
    RideDetail { id: rideDetail }
}
