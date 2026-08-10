import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebChannel
import QtWebEngine

// 订单详情弹窗 (对照旧 RideDetailDialog): 元信息 + 轨迹地图 + 回放。
// map.js 函数: drawFullTrajectory(points) / appendTrajectory(lat,lng) / clearTrajectory()。
Dialog {
    id: dlg
    objectName: "rideDetail"

    property string rideNo: ""
    property var detail: null
    property bool mapLoaded: false
    property bool playing: false
    property int replayIdx: 0

    modal: true
    anchors.centerIn: parent
    width: 760
    height: 620
    background: Rectangle { color: Theme.panel; radius: 6; border.color: Theme.line }
    header: null

    function showRide(no) {
        rideNo = no
        detail = null
        playing = false
        replayIdx = 0
        metaLabel.text = "正在加载…"
        progressLabel.text = ""
        playBtn.enabled = false
        if (api.linkUp) api.rideDetail(no)
        open()
    }

    function tryDraw() {
        if (!mapLoaded || !detail) return
        const pts = detail.points
        if (!pts || pts.length === 0) {
            metaLabel.text = "此订单无轨迹数据"
            return
        }
        dmap.runJavaScript("drawFullTrajectory(" + JSON.stringify(pts) + ");")
        progressLabel.text = "00:00 / " + Theme.hms(detail.duration_sec)
        playBtn.enabled = true
    }

    onOpened: {
        // 先注册 WebChannel 再加载页面 (map.js 依赖 channel.objects.bridge)
        if (dchannel) dchannel.registerObject("bridge", mapBridge)
        const u = dmap.url.toString()
        if (u === "" || u === "about:blank") dmap.url = "qrc:/map.html"
        else dmap.reload()
    }
    onClosed: {
        replayTimer.stop()
        playing = false
    }

    contentItem: ColumnLayout {
        spacing: 10

        RowLayout {
            spacing: 12
            Text {
                text: "订单 " + dlg.rideNo
                font.family: Theme.displayFont
                font.pixelSize: 16
                font.weight: Font.Bold
                color: Theme.text
            }
            Item { Layout.fillWidth: true }
            BikeButton {
                variant: "ghost"
                implicitHeight: 30
                text: "关闭"
                onClicked: dlg.close()
            }
        }

        Label {
            id: metaLabel
            text: "正在加载…"
            color: Theme.muted
            font.family: Theme.zhFont
            font.pixelSize: 12
        }

        WebChannel { id: dchannel }

        WebEngineView {
            id: dmap
            Layout.fillWidth: true
            Layout.fillHeight: true
            webChannel: dchannel

            onLoadingChanged: function(loadRequest) {
                if (loadRequest.status === WebEngineView.LoadFailedStatus) {
                    metaLabel.text = "地图加载失败"; return
                }
                if (loadRequest.status !== WebEngineView.LoadSucceededStatus) return
                dlg.mapLoaded = true
                dlg.tryDraw()
            }
        }

        RowLayout {
            spacing: 12
            BikeButton {
                id: playBtn
                implicitHeight: 34
                text: dlg.playing ? "⏸ 暂停" : "▶ 回放"
                enabled: false
                onClicked: {
                    if (!dlg.detail || !dlg.detail.points || dlg.detail.points.length === 0)
                        return
                    dlg.playing = !dlg.playing
                    if (dlg.playing) {
                        if (dlg.replayIdx >= dlg.detail.points.length) {
                            dlg.replayIdx = 0
                            dmap.runJavaScript("clearTrajectory();")
                        }
                        replayTimer.start()
                    } else {
                        replayTimer.stop()
                    }
                }
            }
            Label {
                id: progressLabel
                text: ""
                color: Theme.muted
                font.family: Theme.displayFont
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
        }
    }

    // 回放定时器: 50ms/点 (与旧 RideDetailDialog 一致)
    Timer {
        id: replayTimer
        interval: 50
        repeat: true
        onTriggered: {
            const pts = dlg.detail ? dlg.detail.points : null
            if (!pts || dlg.replayIdx >= pts.length) {
                stop()
                dlg.playing = false
                return
            }
            const p = pts[dlg.replayIdx]
            dmap.runJavaScript("appendTrajectory(" + p.lat + ", " + p.lng + ");")
            progressLabel.text = Theme.hms(p.elapsed_sec) + " / " +
                                 Theme.hms(dlg.detail.duration_sec)
            ++dlg.replayIdx
        }
    }

    Connections {
        target: api
        function onRideDetailReady(ok, detail, desc) {
            if (detail === undefined) return
            if (!ok) {
                metaLabel.text = "加载失败：" + desc
                return
            }
            if (detail.ride_no !== dlg.rideNo) return
            dlg.detail = detail
            if (!detail.points || detail.points.length === 0) {
                metaLabel.text = "此订单无轨迹数据"
            } else {
                metaLabel.text = "时长 " + Theme.hms(detail.duration_sec) +
                                 "   距离 " + Theme.km(detail.distance_m) + " km" +
                                 "   费用 " + Theme.money(detail.amount_cent)
            }
            dlg.tryDraw()
        }
    }
}
