import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebChannel
import QtWebEngine

// 地图页: WebEngineView 承载 qrc:/map.html (Leaflet)。
// map.js 全局函数(已核实): setUserLocation(lat,lng) / renderBikes(bikes) /
// appendTrajectory(lat,lng) / clearTrajectory() / drawFullTrajectory(points)。
// appendTrajectory 内部会 panTo, 即骑行跟随平移。
Item {
    id: pane
    objectName: "mapPane"

    property double myLat: 39.9821   // LocationProvider 默认坐标(五道口)
    property double myLng: 116.3145
    property string selectedBike: ""
    property bool mapReady: false
    property int idleCount: 0
    property int damagedCount: 0

    function callJs(code) { if (mapReady) mapWeb.runJavaScript(code) }
    function setStatus(s) { statusLabel.text = s }
    function refreshBikes() {
        if (!api.loggedIn) { setStatus("尚未登录"); return }
        setStatus("正在拉取附近车辆…")
        api.refreshNearbyBikes(myLat, myLng)
    }

    WebChannel { id: channel }

    WebEngineView {
        id: mapWeb
        anchors.fill: parent
        url: "qrc:/map.html"
        webChannel: channel

        onLoadFinished: function(ok) {
            if (!ok) { pane.setStatus("地图加载失败"); return }
            if (pane.mapReady) return
            pane.mapReady = true
            // 与旧 MapView::onLoadFinished 一致: 默认坐标打点 + 首次拉车 + 真实定位
            pane.callJs("setUserLocation(" + pane.myLat + ", " + pane.myLng + ");")
            pane.refreshBikes()
            location.requestOnce()
        }
    }

    // ---- 悬浮操作条 (旧 mapOverlay) ----
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 56
        color: "#EE12161E"

        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.line }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 10

            Label {
                id: statusLabel
                text: "正在加载地图…"
                color: Theme.muted
                font.family: Theme.zhFont
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.maximumWidth: 320
            }

            Rectangle { width: 1; height: 18; color: Theme.line }

            Text {
                text: "可用 " + pane.idleCount
                font.family: Theme.zhFont
                font.pixelSize: 12
                color: Theme.green
            }
            Text {
                text: "故障 " + pane.damagedCount
                font.family: Theme.zhFont
                font.pixelSize: 12
                color: Theme.red
            }

            Item { Layout.fillWidth: true }

            BikeButton { variant: "ghost"; implicitHeight: 34; text: "报修"; enabled: api.linkUp
                onClicked: { damageDialog.bikeNoField.text = pane.selectedBike; damageDialog.open() } }
            BikeButton { variant: "ghost"; implicitHeight: 34; text: "定位"; enabled: api.linkUp
                onClicked: location.requestOnce() }
            BikeButton { variant: "ghost"; implicitHeight: 34; text: "刷新车辆"; enabled: api.linkUp
                onClicked: pane.refreshBikes() }
        }
    }

    // ---- 报修对话框 (旧版有接口无 UI, 此处补齐交互) ----
    Dialog {
        id: damageDialog
        property alias bikeNoField: dmgBikeNo
        modal: true
        anchors.centerIn: parent
        width: 380
        background: Rectangle { color: Theme.panel; radius: 6; border.color: Theme.line }
        header: null
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                text: "车辆报修"
                font.family: Theme.zhFont
                font.pixelSize: 16
                font.weight: Font.Bold
                color: Theme.amber
            }
            Text { text: "车辆编号"; font.family: Theme.zhFont; font.pixelSize: 11; color: Theme.muted }
            Field { id: dmgBikeNo; Layout.fillWidth: true; placeholderText: "例如 BK1001" }
            Text { text: "故障描述"; font.family: Theme.zhFont; font.pixelSize: 11; color: Theme.muted }
            TextArea {
                id: dmgNote
                Layout.fillWidth: true
                Layout.preferredHeight: 72
                placeholderText: "例如: 刹车失灵"
                color: Theme.text
                placeholderTextColor: Theme.muted
                font.family: Theme.zhFont
                wrapMode: TextEdit.Wrap
                background: Rectangle {
                    color: Theme.panelHi; radius: 3
                    border.width: 1; border.color: dmgNote.activeFocus ? Theme.amber : Theme.line
                }
            }
            Label { id: dmgStatus; visible: text !== ""; color: Theme.muted; font.pixelSize: 12 }
            RowLayout {
                spacing: 10
                BikeButton {
                    text: "提交"
                    enabled: api.linkUp && dmgBikeNo.text.trim() !== ""
                    onClicked: {
                        dmgStatus.text = "正在提交…"
                        api.reportDamage(dmgBikeNo.text.trim(), dmgNote.text.trim())
                    }
                }
                BikeButton { variant: "ghost"; text: "取消"; onClicked: damageDialog.close() }
            }
        }
    }

    // ---- C++ 信号接线 ----
    Connections {
        target: api
        function onNearbyBikesReady(ok, bikes, desc) {
            if (!ok) { pane.setStatus("加载失败: " + desc); return }
            // map.js renderBikes 接收对象数组 {bike_no, lat, lng, status}
            pane.callJs("renderBikes(" + JSON.stringify(bikes) + ");")
            pane.setStatus("共 " + bikes.length + " 辆车")
        }
        function onDamageResult(ok, desc) {
            if (!damageDialog.opened) return
            if (ok) {
                damageDialog.close()
                pane.setStatus("报修已提交：" + damageDialog.bikeNoField.text)
            } else {
                dmgStatus.text = "提交失败: " + desc
                dmgStatus.color = Theme.red
            }
        }
    }

    Connections {
        target: mapBridge
        function onBikeClicked(bike_no) {
            pane.selectedBike = bike_no
            pane.setStatus("正在解锁 " + bike_no + "…")
            api.scanUnlock(bike_no, pane.myLat, pane.myLng)
        }
        function onBikeCountsUpdated(idle, damaged) {
            pane.idleCount = idle
            pane.damagedCount = damaged
        }
    }

    Connections {
        target: location
        function onPositionReady(lat, lng) {
            pane.myLat = lat
            pane.myLng = lng
            pane.callJs("setUserLocation(" + lat + ", " + lng + ");")
        }
    }

    Component.onCompleted: {
        // 先注册 WebChannel 对象再让页面加载完成, map.js 依赖 channel.objects.bridge
        channel.registerObject("bridge", mapBridge)
    }
}
