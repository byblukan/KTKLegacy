// KTK for Symbian - the window: the sign-in page or the week, following app.signedIn, plus the
// alert dialog and the notice banner every page shares.
import QtQuick 1.1
import com.nokia.symbian 1.1
import com.nokia.extras 1.1

PageStackWindow {
    id: window
    showStatusBar: true
    showToolBar: true
    platformSoftwareInputPanelEnabled: true

    Component { id: loginPage; LoginPage {} }
    Component { id: weekPage; WeekPage {} }
    Component { id: lessonPage; LessonPage {} }

    function route() {
        pageStack.clear()
        pageStack.push(app.signedIn ? weekPage : loginPage)
    }

    function alert(text) {
        alertDialog.message = text
        alertDialog.open()
    }

    Component.onCompleted: route()

    Connections {
        target: app
        onSessionChanged: window.route()
        onLoginRequired: window.alert(message)
        onNotice: {
            banner.text = message
            banner.open()
        }
    }

    QueryDialog {
        id: alertDialog
        titleText: "KTKLegacy"
        acceptButtonText: "OK"
    }

    InfoBanner {
        id: banner
        timeout: 3500
    }
}
