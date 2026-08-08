import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 骑行页: 计时/计费 HUD + 结束骑行。
// 每秒 trajectory.tick → 地图 appendTrajectory(内部 panTo 跟随) + api.reportPosition 上报。
Item {
    id: ridePane
    objectName: "ridePane"

    property var mapPane: null
    property int elapsedSec: 0
    property double distanceM: 0
    property bool ending: false
    property int beforeEndSec: 0    // 点结束前的计时, 恢复用

    Rectangle { anchors.fill: parent; color: Theme.bg }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        Text {
            text: trajectory.active ? "RIDING" : "STANDBY"
            font.family: Theme.displayFont
            font.pixelSize: 11
            font.weight: Font.DemiBold
            color: trajectory.active ? Theme.green : Theme.muted
        }
        Text {
            text: trajectory.active ? "骑行中 · " + api.activeRide
                                    : "尚未开始骑行"
            font.family: Theme.zhFont
            font.pixelSize: 24
            font.weight: Font.Bold
            color: Theme.text
        }

        Item { Layout.preferredHeight: 12 }

        // 计时大字
        Text {
            id: timerText
            text: Theme.hms(ridePane.elapsedSec)
            font.family: Theme.displayFont
            font.pixelSize: 72
            font.weight: Font.Bold
            color: trajectory.active ? Theme.text : Theme.line
        }

        RowLayout {
            spacing: 28
            Text {
                text: "📏 " + Theme.km(ridePane.distanceM) + " km"
                font.family: Theme.zhFont
                font.pixelSize: 14
                color: Theme.muted
            }
            Text {
                text: "💰 约 " + Theme.money(Theme.feeCent(ridePane.elapsedSec))
                font.family: Theme.zhFont
                font.pixelSize: 14
                color: Theme.muted
            }
        }

        Text {
            id: statusLabel
            text: trajectory.active ? "位置每秒自动上报" : "在地图页点击车辆即可解锁开始骑行"
            font.family: Theme.zhFont
            font.pixelSize: 12
            color: Theme.muted
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        Item { Layout.fillHeight: true }

        BikeButton {
            id: endBtn
            Layout.fillWidth: true
            implicitHeight: 48
            variant: "danger"
            text: ridePane.ending ? "正在结束…" : "结束骑行"
            enabled: trajectory.active && !ridePane.ending && api.linkUp
            onClicked: {
                ridePane.ending = true
                ridePane.beforeEndSec = ridePane.elapsedSec
                // #3: 点击即停模拟(对齐旧 RideView timer_.stop()),
                // 防止等待响应期间继续刷 0x15/覆盖结算显示。
                trajectory.stop()
                statusLabel.text = "正在结束骑行…"
                api.endRide(trajectory.currentLat, trajectory.currentLng)
            }
        }
    }

    Connections {
        target: trajectory
        function onTick(lat, lng, seq, elapsedSec, distanceM) {
            ridePane.elapsedSec = elapsedSec
            ridePane.distanceM = distanceM
            // 地图跟随平移 + 轨迹追加 (appendTrajectory 内部 panTo)
            if (mapPane)
                mapPane.callJs("appendTrajectory(" + lat + ", " + lng + ");")
            // fire-and-forget 位置上报, 替代旧 detach 线程方案
            api.reportPosition(api.activeRide, seq, lat, lng, elapsedSec)
        }
    }

    Connections {
        target: api
        function onRideEnded(ok, amountCent, balanceAfter, desc) {
            ridePane.ending = false
            if (ok) {
                statusLabel.text = "消费 " + Theme.money(amountCent) +
                                   "，余额 " + Theme.money(balanceAfter)
                ridePane.elapsedSec = 0
                ridePane.distanceM = 0
            } else {
                // #3: 结束失败 → 恢复骑行: 以当前位置+新种子重启模拟(start 幂等)。
                if (api.activeRide !== "") {
                    trajectory.start(api.activeRide, trajectory.currentLat,
                                     trajectory.currentLng,
                                     Math.floor(Date.now() / 1000))
                    ridePane.elapsedSec = ridePane.beforeEndSec
                }
                statusLabel.text = "结束失败：" + desc + "，已恢复骑行，请重试"
            }
        }
    }
}
