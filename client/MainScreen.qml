import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 登录后的主界面: 顶部标识栏 + 线路式页签 + SwipeView 五页。
Item {
    id: mainScreen
    objectName: "mainScreen"

    readonly property var tabTitles: ["地图", "骑行中", "钱包", "账单", "历史"]

    // ---- 顶部标识栏 ----
    Rectangle {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 54
        color: Theme.panel

        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            spacing: 14

            Rectangle { width: 10; height: 10; color: Theme.amber }
            Text {
                text: "BIKE"
                font.family: Theme.displayFont
                font.pixelSize: 19
                font.weight: Font.Bold
                color: Theme.text
            }
            Text {
                text: "共享单车 · 用户端"
                font.family: Theme.zhFont
                font.pixelSize: 11
                color: Theme.muted
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                width: 8; height: 8; radius: 4
                color: api.linkUp ? Theme.green : Theme.red
                SequentialAnimation on opacity {
                    running: !api.linkUp
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.25; duration: 450 }
                    NumberAnimation { to: 1.0; duration: 450 }
                }
            }
            Text {
                text: api.linkUp ? "已连接" : "重连中…"
                font.family: Theme.zhFont
                font.pixelSize: 11
                color: api.linkUp ? Theme.green : Theme.red
            }
            Rectangle { width: 1; height: 18; color: Theme.line }
            Text {
                text: api.mobile
                font.family: Theme.displayFont
                font.pixelSize: 13
                color: Theme.text
            }
        }
    }

    // ---- 线路式页签 ----
    Rectangle {
        id: tabStrip
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 44
        color: Theme.panel

        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            spacing: 0

            Repeater {
                model: mainScreen.tabTitles
                Item {
                    id: tabItem
                    Layout.preferredWidth: 118
                    Layout.fillHeight: true
                    readonly property bool selected: swipe.currentIndex === index
                    readonly property bool tabEnabled: index !== 1 || api.activeRide !== ""

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: tabItem.tabEnabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor
                        onClicked: if (tabItem.tabEnabled) swipe.currentIndex = index
                    }

                    Text {
                        anchors.centerIn: parent
                        text: modelData
                        color: !tabItem.tabEnabled ? Theme.muted
                             : tabItem.selected ? Theme.amber : Theme.text
                        font.family: Theme.zhFont
                        font.pixelSize: 13
                        font.weight: tabItem.selected ? Font.Bold : Font.Normal
                    }
                    // 选中指示: 信号黄横条
                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 56; height: 3
                        color: Theme.amber
                        visible: tabItem.selected
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }
    }

    // ---- 五个面板 ----
    SwipeView {
        id: swipe
        anchors.top: tabStrip.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        interactive: false  // 页签切换, 禁止滑动(与旧 TabWidget 语义一致)

        MapPane { id: mapPane }
        RidePane { id: ridePane; mapPane: mapPane }
        WalletPane {}
        RecordsPane {}
        HistoryPane {}
    }

    // ---- 解锁结果: 启动轨迹模拟 + 跳转骑行页 (旧 MainWindow::on_unlock_requested) ----
    Connections {
        target: api
        function onUnlockResult(ok, rideNo, desc) {
            if (ok) {
                mapPane.setStatus("解锁成功 " + rideNo)
                mapPane.callJs("clearTrajectory();")
                trajectory.start(rideNo, mapPane.myLat, mapPane.myLng,
                                 Math.floor(Date.now() / 1000))
                swipe.currentIndex = 1
            }
        }
        function onRideEnded(ok, amountCent, balanceAfter, desc) {
            if (!ok) return  // 失败分支由 RidePane 恢复骑行, 此处不停模拟
            // #3: 防御性停表 —— 防止结束后每秒以空 ride_no 刷 0x15、
            // 计时显示被 tick 覆盖 (对齐旧 RideView timer_.stop() 语义)。
            trajectory.stop()
            // 旧 MainWindow::on_ride_ended: 清状态 + 跳账单页 + 结算提示
            swipe.currentIndex = 3
            settleDialog.amountCent = amountCent
            settleDialog.open()
        }
    }

    // ---- 结算提示 ----
    Dialog {
        id: settleDialog
        property int amountCent: 0
        modal: true
        anchors.centerIn: parent
        title: "骑行结束"
        width: 360
        background: Rectangle { color: Theme.panel; radius: 6; border.color: Theme.line }
        header: null
        contentItem: ColumnLayout {
            spacing: 16
            Text {
                text: "本次消费"
                font.family: Theme.zhFont
                font.pixelSize: 13
                color: Theme.muted
            }
            Text {
                text: Theme.money(settleDialog.amountCent)
                font.family: Theme.displayFont
                font.pixelSize: 36
                font.weight: Font.Bold
                color: Theme.amber
            }
            BikeButton {
                Layout.fillWidth: true
                text: "好的"
                onClicked: settleDialog.close()
            }
        }
    }

    // ---- 孤儿订单 (旧 MainWindow::check_orphan_ride) ----
    Dialog {
        id: orphanDialog
        modal: true
        anchors.centerIn: parent
        width: 420
        background: Rectangle { color: Theme.panel; radius: 6; border.color: Theme.line }
        header: null
        contentItem: ColumnLayout {
            spacing: 14
            Text {
                text: "检测到未结订单"
                font.family: Theme.zhFont
                font.pixelSize: 16
                font.weight: Font.Bold
                color: Theme.amber
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: "订单 " + api.activeRide + " 上次未结束，是否尝试结束？\n（若服务端已重启，此订单已失效）"
                font.family: Theme.zhFont
                font.pixelSize: 12
                color: Theme.text
            }
            RowLayout {
                spacing: 10
                BikeButton {
                    text: "尝试结束"
                    onClicked: {
                        orphanDialog.close()
                        api.endRide(39.9821, 116.3145)  // 与旧实现相同的兜底坐标
                    }
                }
                BikeButton {
                    variant: "ghost"
                    text: "忽略并清除"
                    onClicked: {
                        orphanDialog.close()
                        api.clearActiveRide()
                    }
                }
            }
        }
    }

    Component.onCompleted: {
        // 上次遗留的进行中订单: 恢复提示(不自动续跑计时, 与旧版一致)。
        if (api.activeRide !== "" && !trajectory.active)
            orphanDialog.open()
    }
}
