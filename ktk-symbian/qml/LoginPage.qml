// KTK for Symbian - sign-in with the login and password of the college site.
import QtQuick 1.1
import com.nokia.symbian 1.1

Page {
    id: page

    tools: ToolBarLayout {
        ToolButton { iconSource: "toolbar-back"; onClicked: Qt.quit() }
    }

    function signIn() {
        if (app.signingIn) return
        page.forceActiveFocus()   // closes the on-screen keyboard
        var password = passwordField.text
        passwordField.text = ""
        app.signIn(loginField.text, password)
    }

    Connections {
        target: app
        onSignInFailed: window.alert(message)
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.height + 2 * platformStyle.paddingLarge
        clip: true

        Column {
            id: column
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: platformStyle.paddingLarge }
            spacing: platformStyle.paddingLarge

            Item { width: 1; height: platformStyle.paddingLarge }

            Label {
                text: "Вход"
                font.pixelSize: platformStyle.fontSizeLarge * 1.4
            }
            Label {
                text: "ktk.education-system.ru"
                color: platformStyle.colorNormalMid
                font.pixelSize: platformStyle.fontSizeSmall
            }
            TextField {
                id: loginField
                width: parent.width
                placeholderText: "Логин с сайта"
                text: app.lastLogin
                enabled: !app.signingIn
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                Keys.onReturnPressed: passwordField.forceActiveFocus()
            }
            TextField {
                id: passwordField
                width: parent.width
                placeholderText: "Пароль"
                echoMode: TextInput.Password
                enabled: !app.signingIn
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhHiddenText
                Keys.onReturnPressed: page.signIn()
            }
            Button {
                width: parent.width
                text: app.signingIn ? "Вход…" : "Войти"
                enabled: !app.signingIn
                onClicked: page.signIn()
            }
            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: app.signingIn
                visible: app.signingIn
                width: platformStyle.graphicSizeMedium
                height: platformStyle.graphicSizeMedium
            }
            Label {
                width: parent.width
                wrapMode: Text.WordWrap
                color: platformStyle.colorNormalMid
                font.pixelSize: platformStyle.fontSizeSmall
                text: "Логин и пароль те же, что на сайте колледжа. Пароль отправляется только на сайт и не сохраняется: приложение хранит лишь сессию."
            }
        }
    }

    ScrollDecorator { flickableItem: flick }
}
