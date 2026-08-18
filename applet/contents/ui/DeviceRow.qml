pragma ComponentBehavior: Bound

import QtQuick

import org.kde.kirigami as Kirigami

import dev.drip

import "."

/**
 * The horizontal row of devices.
 *
 * A Row inside a Flickable rather than a ListView: a horizontal ListView sizing
 * itself to its own contentWidth is a binding loop, and with a handful of
 * devices virtualisation buys nothing.
 */
Item {
    id: root

    property bool dragActive: false
    property int avatarSize: Theme.avatarSize
    /** True when the row owns the whole popup rather than sharing it with history. */
    property bool roomy: false

    readonly property int deviceCount: DripClient.devices.count
    /** Gap between pictures: opened up while dragging, and while roomy. */
    readonly property int gap: (root.dragActive || root.roomy) ? Theme.space3 : 0

    /*
     * The System Tray gives every applet the same fixed popup, so with history
     * hidden there is room to spare. Grow the pictures into whichever of width
     * or height runs out first, never below the size the user chose.
     */
    readonly property int pictureSize: {
        if (!root.roomy || root.deviceCount === 0) {
            return root.avatarSize
        }
        const usable = root.width - root.gap * (root.deviceCount - 1)
        const byWidth = Math.floor(usable / root.deviceCount) - Theme.space5
        const byHeight = root.height - Theme.glowInset - Theme.space3 - Theme.space4 - 16
        return Math.max(root.avatarSize, Math.min(Math.round(root.avatarSize * 1.5), byWidth, byHeight))
    }

    signal deviceActivated(string deviceId, string deviceName)
    signal filesDroppedOn(string deviceId, var urls)

    implicitHeight: Theme.glowInset + root.avatarSize + Theme.space4 + 16

    /** Outbound progress for a device, or -1 when nothing is in flight. */
    function progressFor(deviceId) {
        for (let i = 0; i < DripClient.transfers.count; ++i) {
            const t = DripClient.transfers.get(i)
            if (t.deviceId === deviceId && (t.state === "active" || t.state === "queued")) {
                return t.progress >= 0 ? t.progress : 0
            }
        }
        return -1
    }

    /** Bumped whenever a transfer moves, so the rings re-evaluate. */
    property int progressTick: 0
    Connections {
        target: DripClient.transfers
        function onDataChanged(topLeft, bottomRight, roles) { root.progressTick++ }
        function onCountChanged() { root.progressTick++ }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: row.width
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Row {
            id: row

            // Centred while everything fits; hugs the left once it scrolls.
            x: width < flick.width ? (flick.width - width) / 2 : 0
            y: Math.round((flick.height - height) / 2)
            spacing: root.gap

            Behavior on spacing {
                SpringAnimation {
                    spring: Theme.springStiffness
                    damping: Theme.springDamping
                    mass: Theme.springMass
                }
            }

            Repeater {
                model: DripClient.devices

                delegate: DeviceAvatar {
                    required property var model

                    avatarSize: root.pictureSize
                    deviceId: model.id
                    deviceName: model.name
                    os: model.os
                    owner: model.owner
                    avatarUrl: model.avatarUrl
                    online: model.online
                    canReceive: model.canReceive
                    reason: model.reason

                    progress: {
                        root.progressTick
                        return root.progressFor(model.id)
                    }

                    onClicked: root.deviceActivated(model.id, model.name)
                    onFilesDropped: urls => root.filesDroppedOn(model.id, urls)
                }
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: DripClient.devices.count === 0
        text: DripClient.available ? "No devices in your tailnet" : "drip engine not running"
        color: Kirigami.Theme.disabledTextColor
        font.pixelSize: Theme.sizeBody
    }
}
