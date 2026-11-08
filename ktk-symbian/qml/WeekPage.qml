// KTK for Symbian - the week: days with their pairs, or only the graded pairs.
import QtQuick 1.1
import com.nokia.symbian 1.1

Page {
    id: page

    tools: ToolBarLayout {
        ToolButton { iconSource: "toolbar-back"; onClicked: Qt.quit() }
        ToolButton { iconSource: "toolbar-previous"; onClicked: app.previousWeek() }
        ToolButton { iconSource: "toolbar-refresh"; enabled: !app.loading; onClicked: app.reload() }
        ToolButton { iconSource: "toolbar-next"; onClicked: app.nextWeek() }
        ToolButton { iconSource: "toolbar-menu"; onClicked: menu.open() }
    }

    Menu {
        id: menu
        MenuLayout {
            MenuItem { text: "Текущая неделя"; onClicked: app.showCurrentWeek() }
            MenuItem { text: "Обновить"; onClicked: app.reload() }
            MenuItem { text: "Выйти"; onClicked: signOutDialog.open() }
        }
    }

    QueryDialog {
        id: signOutDialog
        titleText: "Выход"
        message: app.login == "" ? "Выйти из аккаунта?" : "Выйти из аккаунта " + app.login + "?"
        acceptButtonText: "Выйти"
        rejectButtonText: "Отмена"
        onAccepted: app.signOut()
    }

    function openLesson(index) {
        var title = app.openLesson(index)
        if (title != "") pageStack.push(lessonPage, { title: title })
    }

    // The rows are set by hand rather than bound, so a reload of the same week keeps the list
    // where it was instead of jumping to the top.
    property string shownWeek: ""
    function refreshRows() {
        var sameWeek = shownWeek == app.weekStart
        var y = list.contentY
        list.model = app.rows
        shownWeek = app.weekStart
        if (sameWeek) list.contentY = Math.max(0, Math.min(y, list.contentHeight - list.height))
    }
    Component.onCompleted: refreshRows()

    Connections {
        target: app
        onWeekChanged: page.refreshRows()
        onScrollToRow: list.positionViewAtIndex(row, ListView.Beginning)
    }

    // -- header: the week, tap for the current one --
    Item {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: title.height + 2 * platformStyle.paddingLarge

        Label {
            id: title
            anchors { left: parent.left; leftMargin: platformStyle.paddingLarge; right: spinner.left; rightMargin: platformStyle.paddingMedium; verticalCenter: parent.verticalCenter }
            text: app.weekTitle
            font.pixelSize: platformStyle.fontSizeLarge * 1.2
            elide: Text.ElideRight
        }
        BusyIndicator {
            id: spinner
            anchors { right: parent.right; rightMargin: platformStyle.paddingLarge; verticalCenter: parent.verticalCenter }
            width: platformStyle.graphicSizeSmall
            height: platformStyle.graphicSizeSmall
            running: app.loading
            visible: app.loading
        }
        MouseArea {
            anchors.fill: parent
            onClicked: app.showCurrentWeek()
        }
    }

    ButtonRow {
        id: tabs
        anchors { left: parent.left; right: parent.right; top: header.bottom; margins: platformStyle.paddingSmall }
        Button {
            text: "Пары"
            checked: !app.gradesMode
            onClicked: app.gradesMode = false
        }
        Button {
            text: "Оценки"
            checked: app.gradesMode
            onClicked: app.gradesMode = true
        }
    }

    // Shown from the saved copy because the site could not be reached.
    Rectangle {
        id: banner
        anchors { left: parent.left; right: parent.right; top: tabs.bottom; topMargin: platformStyle.paddingSmall }
        height: visible ? bannerText.height + 2 * platformStyle.paddingMedium : 0
        visible: app.banner != ""
        color: "#5a3a12"
        Label {
            id: bannerText
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: platformStyle.paddingMedium }
            text: app.banner
            wrapMode: Text.WordWrap
            font.pixelSize: platformStyle.fontSizeSmall
        }
    }

    ListView {
        id: list
        anchors { left: parent.left; right: parent.right; top: banner.bottom; topMargin: platformStyle.paddingSmall; bottom: parent.bottom }
        clip: true
        delegate: rowDelegate
    }

    ScrollDecorator { flickableItem: list }

    Component {
        id: rowDelegate

        Item {
            id: row
            property variant d: modelData
            width: list.width
            height: d.kind == "header" ? heading.height : d.kind == "lesson" ? lessonItem.height : message.height + 2 * platformStyle.paddingLarge

            ListHeading {
                id: heading
                visible: row.d.kind == "header"
                width: parent.width
                ListItemText {
                    anchors.fill: parent.paddingItem
                    role: "Heading"
                    text: row.d.kind == "header" ? row.d.title : ""
                }
            }

            ListItem {
                id: lessonItem
                visible: row.d.kind == "lesson"
                width: parent.width
                height: Math.max(platformStyle.graphicSizeLarge, texts.height + 2 * platformStyle.paddingLarge)
                onClicked: page.openLesson(row.d.lessonIndex)

                Column {
                    id: texts
                    anchors { left: parent.paddingItem.left; right: badge.left; rightMargin: platformStyle.paddingMedium; verticalCenter: parent.verticalCenter }
                    spacing: platformStyle.paddingSmall
                    ListItemText {
                        width: parent.width
                        mode: lessonItem.mode
                        role: "Title"
                        text: row.d.kind == "lesson" ? row.d.title : ""
                    }
                    Label {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        font.pixelSize: platformStyle.fontSizeSmall
                        color: platformStyle.colorNormalMid
                        text: row.d.kind == "lesson" ? row.d.details : ""
                        visible: text != ""
                    }
                }
                Label {
                    id: badge
                    anchors { right: parent.paddingItem.right; verticalCenter: parent.verticalCenter }
                    width: platformStyle.graphicSizeMedium
                    horizontalAlignment: Text.AlignRight
                    text: row.d.kind == "lesson" ? row.d.badge : ""
                    color: row.d.kind == "lesson" ? row.d.badgeColor : "#888888"
                    font.bold: true
                    font.pixelSize: row.d.kind == "lesson" && row.d.badgeSmall ? platformStyle.fontSizeMedium : platformStyle.fontSizeLarge * 1.3
                }
            }

            Label {
                id: message
                visible: row.d.kind == "message"
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: platformStyle.paddingLarge }
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: platformStyle.colorNormalMid
                text: row.d.kind == "message" ? row.d.text : ""
            }
        }
    }
}
