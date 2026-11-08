// KTK for Symbian - one pair: when and where, the grade, the task, the webinar and the files.
import QtQuick 1.1
import com.nokia.symbian 1.1

Page {
    id: page
    property string title: ""
    property variant items: []

    Component.onCompleted: items = app.lessonItems()

    tools: ToolBarLayout {
        ToolButton { iconSource: "toolbar-back"; onClicked: pageStack.pop() }
    }

    ContextMenu {
        id: linkMenu
        property string link: ""
        MenuLayout {
            MenuItem { text: "Открыть в браузере"; onClicked: app.openUrl(linkMenu.link) }
            MenuItem { text: "Скопировать ссылку"; onClicked: app.copyText(linkMenu.link) }
        }
    }

    Label {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: platformStyle.paddingLarge }
        text: page.title
        font.pixelSize: platformStyle.fontSizeLarge * 1.2
    }

    Flickable {
        id: flick
        anchors { left: parent.left; right: parent.right; top: header.bottom; topMargin: platformStyle.paddingMedium; bottom: parent.bottom }
        contentHeight: column.height + 2 * platformStyle.paddingLarge
        clip: true

        Column {
            id: column
            anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: platformStyle.paddingLarge; rightMargin: platformStyle.paddingLarge }
            spacing: platformStyle.paddingMedium

            Repeater {
                model: page.items

                // One item; only the part for its kind is visible (a Column skips the others).
                Column {
                    id: entry
                    property variant d: modelData
                    width: column.width

                    Label {
                        visible: entry.d.kind == "title"
                        width: parent.width
                        wrapMode: Text.WordWrap
                        font.bold: true
                        font.pixelSize: platformStyle.fontSizeLarge
                        text: entry.d.kind == "title" ? entry.d.text : ""
                    }

                    Item {
                        visible: entry.d.kind == "row"
                        width: parent.width
                        height: Math.max(rowLabel.height, rowValue.height) + platformStyle.paddingSmall
                        Label {
                            id: rowLabel
                            width: parent.width * 0.32
                            font.bold: true
                            font.pixelSize: platformStyle.fontSizeSmall
                            color: platformStyle.colorNormalMid
                            text: entry.d.kind == "row" ? entry.d.label : ""
                        }
                        Label {
                            id: rowValue
                            anchors { left: rowLabel.right; right: parent.right }
                            wrapMode: Text.WordWrap
                            font.bold: entry.d.kind == "row" && entry.d.bold
                            color: entry.d.kind == "row" && entry.d.color != "" ? entry.d.color : platformStyle.colorNormalLight
                            text: entry.d.kind == "row" ? entry.d.value : ""
                        }
                    }

                    ListHeading {
                        visible: entry.d.kind == "header"
                        width: parent.width
                        ListItemText {
                            anchors.fill: parent.paddingItem
                            role: "Heading"
                            text: entry.d.kind == "header" ? entry.d.text : ""
                        }
                    }

                    Label {
                        id: taskText
                        visible: entry.d.kind == "text"
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: entry.d.kind == "text" ? entry.d.text : ""
                        MouseArea {
                            anchors.fill: parent
                            onPressAndHold: app.copyText(taskText.text)
                        }
                    }

                    Label {
                        visible: entry.d.kind == "note"
                        width: parent.width
                        wrapMode: Text.WordWrap
                        font.pixelSize: platformStyle.fontSizeSmall
                        color: platformStyle.colorNormalMid
                        text: entry.d.kind == "note" ? entry.d.text : ""
                    }

                    Label {
                        id: link
                        visible: entry.d.kind == "link"
                        width: parent.width
                        wrapMode: Text.WrapAnywhere
                        color: "#6aa8ff"
                        font.underline: true
                        text: entry.d.kind == "link" ? entry.d.text : ""
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                linkMenu.link = link.text
                                linkMenu.open()
                            }
                        }
                    }

                    Item {
                        visible: entry.d.kind == "file"
                        width: parent.width
                        height: Math.max(fileName.height, platformStyle.graphicSizeSmall) + 2 * platformStyle.paddingMedium
                        Label {
                            id: fileName
                            anchors { left: parent.left; right: fileSpinner.left; rightMargin: platformStyle.paddingMedium; verticalCenter: parent.verticalCenter }
                            wrapMode: Text.WrapAnywhere
                            color: "#6aa8ff"
                            text: entry.d.kind == "file" ? entry.d.text : ""
                        }
                        BusyIndicator {
                            id: fileSpinner
                            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                            width: platformStyle.graphicSizeSmall
                            height: platformStyle.graphicSizeSmall
                            running: visible
                            visible: entry.d.kind == "file" && app.downloadingFile == entry.d.fileIndex
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: app.downloadFile(entry.d.fileIndex)
                        }
                    }
                }
            }
        }
    }

    ScrollDecorator { flickableItem: flick }
}
