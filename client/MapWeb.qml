import QtQuick
import QtWebChannel
import QtWebEngine

// 桌面版地图后端: WebEngineView 承载 qrc:/map.html (Leaflet)。
// 从 MapPane.qml 拆分而来(任务 #55 Android 移植), 桌面路径保持零回归:
//   - WebChannel 注册 bridge 对象, map.js 经 window.bridge 回调 C++
//   - runJs() 封装 runJavaScript, 供 MapPane 注入 setUserLocation/renderBikes 等调用
// map.js 全局函数(已核实): setUserLocation(lat,lng) / renderBikes(bikes) /
// appendTrajectory(lat,lng) / clearTrajectory() / drawFullTrajectory(points)。
Item {
    id: mapWebRoot

    // 页面加载成功/失败, 由 MapPane 统一驱动后续初始化
    signal pageLoaded()
    signal pageFailed()

    // JS 注入入口 (MapPane.callJs 转发到此)
    function runJs(code) { mapWeb.runJavaScript(code) }

    WebChannel { id: channel }

    WebEngineView {
        id: mapWeb
        anchors.fill: parent
        url: "qrc:/map.html"
        webChannel: channel

        onLoadingChanged: function(loadRequest) {
            if (loadRequest.status === WebEngineView.LoadFailedStatus) {
                mapWebRoot.pageFailed(); return
            }
            if (loadRequest.status !== WebEngineView.LoadSucceededStatus) return
            mapWebRoot.pageLoaded()
        }
    }

    Component.onCompleted: {
        // 先注册 WebChannel 对象再让页面加载完成, map.js 依赖 channel.objects.bridge
        channel.registerObject("bridge", mapBridge)
    }
}
