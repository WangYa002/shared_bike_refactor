import QtQuick
import QtQuick.Controls
import QtWebView

// Android 版地图后端 (任务 #55 Android 移植, 功能降级):
// Qt WebEngine 不支持 Android, 改用 Qt6 WebView 模块(底层为系统 WebView)。
// QtWebView 不支持 runJavaScript / WebChannel, 因此:
//   - runJs() 为空实现(有意 no-op), MapPane 的所有 JS 注入调用
//     (setUserLocation/renderBikes/appendTrajectory/drawFullTrajectory 等)
//     在 Android 上全部跳过, 地图降级为静态展示;
//   - 不注册 WebChannel bridge, map.js 的点车解锁交互在 Android 上不可用。
// 注: qrc:/map.html 的瓦片与 Leaflet 依赖来自公网 CDN, 真机上能否渲染
// 取决于网络环境; 渲染失败不影响应用其余功能。
Item {
    id: mapAndroidRoot

    signal pageLoaded()
    signal pageFailed()

    // Android 降级: QtWebView 无 JS 注入能力, 有意 no-op (保持与 MapWeb 同接口)
    function runJs(code) {}

    WebView {
        id: webView
        anchors.fill: parent
        url: "qrc:/map.html"
    }

    // 静态降级提示条 (顶部悬浮)
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 28
        color: "#CC12161E"
        Text {
            anchors.centerIn: parent
            text: "移动端地图为静态展示模式 (动态标车/轨迹暂不支持)"
            font.family: Theme.zhFont
            font.pixelSize: 11
            color: Theme.muted
        }
    }

    // QtWebView QML 无加载完成信号, 延迟发出 pageLoaded 让 MapPane
    // 走统一的初始化流程(拉车/定位), 其中的 JS 注入调用会被 runJs 静默忽略
    Timer {
        interval: 1500
        running: true
        onTriggered: mapAndroidRoot.pageLoaded()
    }
}
