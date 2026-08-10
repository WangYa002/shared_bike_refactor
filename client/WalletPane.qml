import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 钱包页: 余额展示 + 充值 + 刷新 (对照旧 WalletView)。
Item {
    id: walletPane
    objectName: "walletPane"

    property var balanceCent: null   // null = 未加载
    property bool pending: false

    function setStatus(s, color) {
        statusLabel.text = s
        statusLabel.color = color
    }

    Rectangle { anchors.fill: parent; color: Theme.bg }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        Text {
            text: "我的钱包"
            font.family: Theme.zhFont
            font.pixelSize: 24
            font.weight: Font.Bold
            color: Theme.text
        }
        Text {
            text: "ACCOUNT BALANCE"
            font.family: Theme.displayFont
            font.pixelSize: 10
            color: Theme.muted
        }

        Item { Layout.preferredHeight: 8 }

        Text {
            text: walletPane.balanceCent === null ? "¥ —"
                                                  : Theme.money(walletPane.balanceCent)
            font.family: Theme.displayFont
            font.pixelSize: 52
            font.weight: Font.Bold
            color: Theme.amber
        }

        Item { Layout.preferredHeight: 12 }

        Text {
            text: "充值金额（元）"
            font.family: Theme.zhFont
            font.pixelSize: 12
            color: Theme.muted
        }
        RowLayout {
            spacing: 10
            Field {
                id: amountField
                Layout.preferredWidth: 240
                placeholderText: "输入充值金额，例如 5.00"
                inputMethodHints: Qt.ImhFormattedNumbersOnly
            }
            BikeButton {
                text: walletPane.pending ? "充值中…" : "充值"
                enabled: !walletPane.pending && api.linkUp &&
                         amountField.text.trim() !== ""
                onClicked: {
                    const yuan = parseFloat(amountField.text)
                    if (!(yuan > 0)) { setStatus("请输入有效金额", Theme.red); return }
                    const cents = Math.round(yuan * 100)
                    if (cents <= 0) { setStatus("金额必须大于 0", Theme.red); return }
                    walletPane.pending = true
                    setStatus("正在充值 " + Theme.money(cents) + " …", Theme.muted)
                    api.recharge(cents)
                }
            }
        }

        BikeButton {
            variant: "ghost"
            implicitHeight: 32
            text: "刷新余额"
            enabled: api.linkUp
            onClicked: {
                setStatus("正在查询余额…", Theme.muted)
                api.refreshBalance()
            }
        }

        Label {
            id: statusLabel
            color: Theme.muted
            font.family: Theme.zhFont
            font.pixelSize: 12
        }

        Item { Layout.fillHeight: true }
    }

    Connections {
        target: api
        function onBalanceReady(ok, balance_cent, desc) {
            const wasRecharge = walletPane.pending
            walletPane.pending = false
            if (ok) {
                walletPane.balanceCent = balance_cent
                if (wasRecharge) amountField.text = ""
                setStatus(desc !== "" ? desc : "已更新", Theme.green)
            } else {
                setStatus((wasRecharge ? "充值失败：" : "查询失败：") + desc, Theme.red)
            }
        }
        // 骑行扣费后刷新余额 (旧版在 MainWindow::on_ride_ended 里做)
        function onRideEnded(ok, amountCent, balanceAfter, desc) {
            if (ok) walletPane.balanceCent = balanceAfter
        }
    }

    Component.onCompleted: if (api.loggedIn) api.refreshBalance()
}
