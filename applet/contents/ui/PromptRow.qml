import QtQuick

import org.kde.kirigami as Kirigami

import "."

/** A band that asks a yes/no question, above the divider so it reads as a question. */
Item {
    id: root

    property string iconSource
    /** Quiet line, for context: who, or why. */
    property string title
    /** The thing being decided on. */
    property string subtitle
    property string acceptText
    property string declineText

    signal accepted()
    signal declined()

    implicitHeight: Theme.arrivalRowHeight

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: Theme.space4
        anchors.rightMargin: Theme.space4
        anchors.topMargin: 2
        anchors.bottomMargin: 2
        radius: Theme.radiusRow
        color: Qt.alpha(Kirigami.Theme.highlightColor, 0.20)
        border.width: 1
        border.color: Kirigami.Theme.highlightColor

        Kirigami.Icon {
            id: mark
            anchors.left: parent.left
            anchors.leftMargin: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
            width: 20
            height: 20
            source: root.iconSource
            isMask: true
            color: Kirigami.Theme.highlightColor
        }

        Column {
            anchors.left: mark.right
            anchors.leftMargin: Theme.space3
            anchors.right: buttons.left
            anchors.rightMargin: Theme.space2
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Text {
                width: parent.width
                text: root.title
                color: Kirigami.Theme.disabledTextColor
                font.pixelSize: Theme.sizeMeta
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: root.subtitle
                color: Kirigami.Theme.textColor
                font.pixelSize: Theme.sizeBody
                font.weight: Theme.weightMedium
                elide: Text.ElideMiddle
            }
        }

        Row {
            id: buttons
            anchors.right: parent.right
            anchors.rightMargin: Theme.space3
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space2

            PillButton {
                text: root.acceptText
                accent: true
                onClicked: root.accepted()
            }

            PillButton {
                text: root.declineText
                onClicked: root.declined()
            }
        }
    }
}
