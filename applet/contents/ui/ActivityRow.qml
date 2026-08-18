import QtQuick

import org.kde.kirigami as Kirigami

import dev.drip

import "."

/** One line of history: direction, name, and what happened to it. */
Item {
    id: root

    required property string transferId
    required property string fileName
    required property string deviceName
    required property string path
    required property real size
    required property real progress
    required property bool incoming
    // Not "state": that is a built-in Item property, and shadowing it breaks
    // the delegate.
    required property string transferState
    required property string error
    required property string queuedAt

    signal activated()
    signal cancelRequested()
    signal showFolderRequested()

    height: 44

    readonly property bool running: transferState === "active" || transferState === "queued"
    readonly property bool failed: transferState === "failed" || transferState === "cancelled"
    readonly property bool hasFile: !running && transferState === "completed" && path !== ""

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: Theme.space2
        anchors.rightMargin: Theme.space2
        radius: Theme.radiusRow
        color: hover.hovered ? Qt.alpha(Kirigami.Theme.textColor, 0.10) : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
    }

    // Determinate fill behind a running transfer, so the row is its own bar.
    Rectangle {
        visible: root.running && root.progress >= 0
        anchors.left: parent.left
        anchors.leftMargin: Theme.space2
        anchors.verticalCenter: parent.verticalCenter
        height: parent.height - 4
        width: Math.max(0, (parent.width - Theme.space4) * root.progress)
        radius: Theme.radiusRow
        color: Qt.alpha(Kirigami.Theme.highlightColor, 0.20)
        Behavior on width { NumberAnimation { duration: Theme.durSlow; easing.type: Easing.OutCubic } }
    }

    Row {
        anchors.fill: parent
        anchors.leftMargin: Theme.space4
        anchors.rightMargin: Theme.space4
        spacing: Theme.space3

        Kirigami.Icon {
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            isMask: true
            source: root.incoming ? "arrow-down" : "arrow-up"
            color: root.failed ? Kirigami.Theme.negativeTextColor
                 : root.running ? Kirigami.Theme.highlightColor
                 : root.incoming ? Kirigami.Theme.positiveTextColor
                 : Qt.alpha(Kirigami.Theme.textColor, 0.72)
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 16 - Theme.space3 * 2 - actions.width
            spacing: 1

            Text {
                width: parent.width
                text: root.fileName
                color: Kirigami.Theme.textColor
                font.pixelSize: Theme.sizeBody
                font.weight: Theme.weightMedium
                elide: Text.ElideMiddle
            }

            Text {
                width: parent.width
                color: root.failed ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.disabledTextColor
                font.pixelSize: Theme.sizeMeta
                elide: Text.ElideRight
                text: {
                    if (root.transferState === "failed") {
                        return root.error || "failed"
                    }
                    if (root.transferState === "cancelled") {
                        return "cancelled"
                    }
                    const who = (root.incoming ? "from " : "to ") + root.deviceName
                    if (root.running) {
                        return who + " · " + DripClient.formatSize(root.size * Math.max(0, root.progress))
                             + " of " + DripClient.formatSize(root.size)
                    }
                    return who + " · " + DripClient.formatSize(root.size)
                         + " · " + DripClient.formatRelativeTime(root.queuedAt)
                }
            }
        }

        // Grouped so the label column has one width to subtract regardless of
        // which action is showing.
        Row {
            id: actions
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.space1

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                width: root.hasFile ? implicitWidth : 0
                visible: root.hasFile
                source: "folder-open-symbolic"
                tooltip: root.incoming ? "Show where it was saved" : "Show the file you sent"
                onTriggered: root.showFolderRequested()
            }

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                width: root.running ? implicitWidth : 0
                visible: root.running
                source: "dialog-close"
                hoverColor: Kirigami.Theme.negativeTextColor
                tooltip: "Cancel"
                onTriggered: root.cancelRequested()
            }
        }
    }

    HoverHandler { id: hover }

    TapHandler {
        enabled: root.hasFile
        onTapped: root.activated()
    }
}
