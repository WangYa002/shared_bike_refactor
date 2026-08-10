import QtQuick
import QtQuick.Controls

// 根窗口: 登录前 LoginView, 登录后 MainScreen (StackView 切换)。
ApplicationWindow {
    id: root
    width: 1024
    height: 700
    minimumWidth: 880
    minimumHeight: 600
    visible: true
    title: "BIKE · 共享单车"
    color: Theme.bg

    Component { id: loginComp; LoginView {} }
    Component { id: mainComp;  MainScreen {} }

    StackView {
        id: stack
        anchors.fill: parent
        initialItem: loginComp

        replaceEnter: Transition {
            PropertyAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 240 }
        }
        replaceExit: Transition {
            PropertyAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: 120 }
        }
    }

    Connections {
        target: api
        function onSessionChanged() {
            const cur = stack.currentItem
            if (api.loggedIn && (!cur || cur.objectName !== "mainScreen")) {
                stack.replace(mainComp)
            } else if (!api.loggedIn && cur && cur.objectName === "mainScreen") {
                stack.replace(loginComp)
            }
        }
    }
}
