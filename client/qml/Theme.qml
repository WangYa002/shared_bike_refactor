pragma Singleton
import QtQuick

// 视觉系统: "Night Line / 夜行线" —— 交通标识牌美学。
// 深色沥青底 + 信号黄主色 + Bahnschrift(DIN 系交通字体) 数字展示。
QtObject {
    // ---- 色板 ----
    readonly property color bg:      "#0B0E13"
    readonly property color panel:   "#12161E"
    readonly property color panelHi: "#1A212C"
    readonly property color line:    "#242C38"
    readonly property color text:    "#EDF2F8"
    readonly property color muted:   "#77839A"
    readonly property color amber:   "#FFB81C"
    readonly property color green:   "#31D98C"
    readonly property color red:     "#FF5C5C"
    readonly property color blue:    "#58A6FF"

    // ---- 字体 ----
    readonly property string displayFont: "Bahnschrift"
    readonly property string bodyFont: "Segoe UI"
    readonly property string zhFont: "Microsoft YaHei UI"

    // ---- 格式化 (与旧 C++ 视图语义一致) ----
    function money(cents) { return "¥ " + (cents / 100).toFixed(2) }
    function km(meters) { return (meters / 1000).toFixed(2) }
    function hms(sec) {
        function p(v) { return (v < 10 ? "0" : "") + v }
        return p(Math.floor(sec / 3600)) + ":" +
               p(Math.floor(sec / 60) % 60) + ":" + p(sec % 60)
    }
    // 预估费用(分): 15 分钟内 1.00 元, 之后每 15 分钟 +0.50 元 (旧 RideView::estimate_fee_cent)
    function feeCent(sec) {
        var base = 15 * 60, step = 15 * 60
        if (sec <= base) return 100
        return 100 + Math.ceil((sec - base) / step) * 50
    }
    function fmtTs(ts) {
        return new Date(ts * 1000).toLocaleString(Qt.locale("zh_CN"),
                                                  "yyyy-MM-dd HH:mm:ss")
    }
}
