import QtQuick

import org.kde.kirigami as Kirigami

import dev.drip

import "."

/**
 * One offered file, awaiting a decision. Shown only when auto-accept is off.
 * The bytes have already arrived by this point, so Decline discards the file
 * rather than refusing the transfer -- hence "sent you", not "wants to send".
 */
Item {
    id: root

    required property string fileName
    required property string senderName
    required property double size

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
            source: "document-save-symbolic"
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
                text: root.senderName + " sent you"
                color: Kirigami.Theme.disabledTextColor
                font.pixelSize: Theme.sizeMeta
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: root.fileName + "  ·  " + DripClient.formatSize(root.size)
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
                text: "Accept"
                accent: true
                onClicked: root.accepted()
            }

            PillButton {
                text: "Decline"
                onClicked: root.declined()
            }
        }
    }
}
