import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 登录页: 左侧品牌面板(线路装饰), 右侧手机号 + 验证码表单。
Item {
    id: loginView
    objectName: "loginView"

    property bool pendingCode: false
    property bool pendingLogin: false

    function setStatus(text, color) {
        statusLabel.text = text
        statusLabel.color = color
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---- 品牌面板 ----
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 340
            color: Theme.panel
            border.width: 0

            Rectangle { // 右缘分隔线
                anchors.right: parent.right
                width: 1; height: parent.height
                color: Theme.line
            }

            ColumnLayout {
                anchors.left: parent.left
                anchors.leftMargin: 44
                anchors.verticalCenter: parent.verticalCenter
                spacing: 0

                // 竖向线路装饰: 黄线 + 三个站点
                Item {
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 96
                    Rectangle { x: 8; width: 4; height: 96; color: Theme.amber }
                    Repeater {
                        model: [8, 44, 80]
                        Rectangle {
                            x: 4; y: modelData; width: 12; height: 12; radius: 6
                            color: Theme.bg
                            border.width: 3; border.color: Theme.amber
                        }
                    }
                }

                Item { Layout.preferredHeight: 28 }

                Text {
                    text: "BIKE"
                    font.family: Theme.displayFont
                    font.pixelSize: 58
                    font.weight: Font.Bold
                    color: Theme.text
                    opacity: 0
                    NumberAnimation on opacity { to: 1; duration: 500; easing.type: Easing.OutCubic }
                }
                Text {
                    text: "S H A R E D   T R A N S I T"
                    font.family: Theme.displayFont
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    color: Theme.amber
                    opacity: 0
                    SequentialAnimation on opacity {
                        PauseAnimation { duration: 150 }
                        NumberAnimation { to: 1; duration: 500 }
                    }
                }
                Item { Layout.preferredHeight: 18 }
                Text {
                    text: "扫码即走 · 按程计费"
                    font.family: Theme.zhFont
                    font.pixelSize: 13
                    color: Theme.muted
                    opacity: 0
                    SequentialAnimation on opacity {
                        PauseAnimation { duration: 300 }
                        NumberAnimation { to: 1; duration: 500 }
                    }
                }
            }

            // 底部线路编号装饰
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 44
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 28
                text: "LINE 01 · WUDAOKOU"
                font.family: Theme.displayFont
                font.pixelSize: 10
                color: Theme.muted
            }
        }

        // ---- 表单区 ----
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                id: form
                anchors.centerIn: parent
                width: 360
                spacing: 14

                Text {
                    text: "欢迎回来"
                    font.family: Theme.zhFont
                    font.pixelSize: 26
                    font.weight: Font.Bold
                    color: Theme.text
                }
                Text {
                    text: "输入手机号获取验证码，登录后即可使用钱包和查看账单"
                    font.family: Theme.zhFont
                    font.pixelSize: 12
                    color: Theme.muted
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }

                Item { Layout.preferredHeight: 10 }

                Text {
                    text: "手机号 / MOBILE"
                    font.family: Theme.displayFont
                    font.pixelSize: 10
                    color: Theme.muted
                }
                Field {
                    id: mobileField
                    Layout.fillWidth: true
                    placeholderText: "11 位手机号，例如 15600000010"
                    maximumLength: 11
                    inputMethodHints: Qt.ImhDigitsOnly
                }

                Text {
                    text: "验证码 / CODE"
                    font.family: Theme.displayFont
                    font.pixelSize: 10
                    color: Theme.muted
                }
                Field {
                    id: codeField
                    Layout.fillWidth: true
                    placeholderText: "6 位验证码"
                    maximumLength: 6
                    inputMethodHints: Qt.ImhDigitsOnly
                }

                Item { Layout.preferredHeight: 4 }

                BikeButton {
                    id: loginBtn
                    Layout.fillWidth: true
                    implicitHeight: 44
                    text: pendingLogin ? "正在登录…" : "登 录"
                    enabled: !pendingLogin && api.linkUp
                    onClicked: {
                        const mobile = mobileField.text.trim()
                        const code = parseInt(codeField.text, 10)
                        if (mobile.length === 0 || !code) {
                            setStatus("请填写手机号和验证码", Theme.red)
                            return
                        }
                        setStatus("正在登录…", Theme.muted)
                        pendingLogin = true
                        api.login(mobile, code)
                    }
                }
                BikeButton {
                    id: codeBtn
                    Layout.fillWidth: true
                    variant: "ghost"
                    text: pendingCode ? "正在发送…" : "获取验证码"
                    enabled: !pendingCode && api.linkUp
                    onClicked: {
                        const mobile = mobileField.text.trim()
                        if (!/^1\d{10}$/.test(mobile)) {
                            setStatus("请输入 11 位手机号", Theme.red)
                            return
                        }
                        setStatus("正在发送验证码…", Theme.muted)
                        pendingCode = true
                        api.sendMobileCode(mobile)
                    }
                }

                Label {
                    id: statusLabel
                    Layout.fillWidth: true
                    text: "请先获取验证码"
                    color: Theme.muted
                    wrapMode: Text.Wrap
                    font.family: Theme.zhFont
                    font.pixelSize: 12
                }
            }

            // 链路状态指示
            RowLayout {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 20
                spacing: 8
                Rectangle {
                    width: 8; height: 8; radius: 4
                    color: api.linkUp ? Theme.green : Theme.red
                    SequentialAnimation on opacity {
                        running: !api.linkUp
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.25; duration: 500 }
                        NumberAnimation { to: 1.0; duration: 500 }
                    }
                }
                Text {
                    text: api.linkUp ? "LINK UP" : "LINK DOWN"
                    font.family: Theme.displayFont
                    font.pixelSize: 10
                    color: Theme.muted
                }
            }
        }
    }

    Connections {
        target: api
        function onMobileCodeResult(ok, icode, desc) {
            pendingCode = false
            if (ok) {
                const padded = String(icode).padStart(6, "0")
                codeField.text = padded
                setStatus("验证码已发送：" + padded + "（测试环境直接显示）", Theme.green)
            } else {
                setStatus("发送失败：" + desc, Theme.red)
            }
        }
        function onLoginResult(ok, desc) {
            pendingLogin = false
            if (ok) {
                setStatus("登录成功，正在进入…", Theme.green)
            } else {
                setStatus("登录失败：" + desc, Theme.red)
            }
        }
    }

    // 入场: 表单淡入 (表单由 anchors 定位, 不动画 y 避免锚点冲突)
    Component.onCompleted: reveal.start()
    SequentialAnimation {
        id: reveal
        PauseAnimation { duration: 120 }
        NumberAnimation {
            target: form
            property: "opacity"
            from: 0; to: 1
            duration: 420
            easing.type: Easing.OutCubic
        }
    }
}
