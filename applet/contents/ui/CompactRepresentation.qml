import QtQuick

import org.kde.kirigami as Kirigami
import org.kde.plasma.plasmoid

import dev.drip

import "."

/**
 * The tray icon. Dragging a file onto it springs the panel open; dropping
 * directly on it sends, when exactly one device is reachable.
 */
DropArea {
    id: root

    required property PlasmoidItem plasmoidItem

    property bool dragHovering: false
    property bool hasNews: false

    // A dwell, so merely crossing the tray does not open the panel.
    Timer {
        id: dwell
        interval: 160
        onTriggered: root.plasmoidItem.expanded = true
    }

    onEntered: drag => {
        if (!drag.hasUrls) {
            return
        }
        root.dragHovering = true
        DripClient.refresh()
        dwell.restart()
    }

    onExited: {
        root.dragHovering = false
        dwell.stop()
    }

    onDropped: drop => {
        dwell.stop()
        root.dragHovering = false
        if (!drop.hasUrls) {
            return
        }

        const targets = DripClient.reachableIds
        if (targets.length === 1) {
            DripClient.send(targets[0], drop.urls.map(u => u.toString()))
            drop.accepted = true
        } else {
            root.plasmoidItem.expanded = true
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onClicked: {
            root.plasmoidItem.expanded = !root.plasmoidItem.expanded
            root.hasNews = false
        }
    }

    Kirigami.Icon {
        id: icon
        anchors.fill: parent
        source: Qt.resolvedUrl("../icons/drip.svg")
        isMask: true
        active: root.dragHovering

        scale: root.dragHovering ? 1.18 : 1.0
        Behavior on scale {
            SpringAnimation {
                spring: Theme.springStiffness
                damping: Theme.springDamping
                mass: Theme.springMass
            }
        }
    }

    // Unread dot for something that arrived while the panel was shut.
    Rectangle {
        width: Math.max(6, parent.width * 0.22)
        height: width
        radius: width / 2
        anchors.right: parent.right
        anchors.top: parent.top
        color: Kirigami.Theme.highlightColor
        visible: root.hasNews && !root.plasmoidItem.expanded
        border.width: 1
        border.color: Qt.rgba(0, 0, 0, 0.45)
    }

    // Pulses while anything is in flight.
    Rectangle {
        anchors.centerIn: parent
        width: parent.width + 4
        height: width
        radius: width / 2
        color: "transparent"
        border.width: 1.5
        border.color: Kirigami.Theme.highlightColor
        opacity: DripClient.activeCount > 0 ? 0.9 : 0
        visible: opacity > 0

        Behavior on opacity { NumberAnimation { duration: Theme.durBase } }

        SequentialAnimation on scale {
            running: DripClient.activeCount > 0
            loops: Animation.Infinite
            NumberAnimation { from: 0.86; to: 1.06; duration: 900; easing.type: Easing.InOutSine }
            NumberAnimation { from: 1.06; to: 0.86; duration: 900; easing.type: Easing.InOutSine }
        }
    }

    Connections {
        target: DripClient
        function onFileReceived(fileName, deviceName) {
            if (!root.plasmoidItem.expanded) {
                root.hasNews = true
            }
        }
    }
}
