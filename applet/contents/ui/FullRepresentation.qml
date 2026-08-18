pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.plasma.extras as PlasmaExtras
import org.kde.plasma.plasmoid

import dev.drip

import "."

/**
 * The panel.
 *
 * The root must be a PlasmaExtras.Representation: a bare Item has no implicit
 * size, and the popup opens 0x0.
 */
PlasmaExtras.Representation {
    id: root

    readonly property bool keepHistory: Plasmoid.configuration.keepHistory
    readonly property int transferCount: DripClient.transfers.count
    /** With history off the section appears only while something is in flight. */
    readonly property bool showHistory: keepHistory || transferCount > 0

    readonly property int devicesHeight: Theme.space3 + deviceRow.implicitHeight
    readonly property int arrivalsHeight: DripClient.pendingArrivals.count * Theme.arrivalRowHeight
    readonly property int historyHeight: showHistory
        ? 1 + Theme.space2 + Math.min(300, Math.max(96, transferCount * 45 + Theme.space2 * 2))
        : 0
    readonly property int footerHeight: 34

    Layout.minimumWidth: 340
    Layout.preferredWidth: 380
    Layout.minimumHeight: devicesHeight + footerHeight
    Layout.preferredHeight: devicesHeight + arrivalsHeight + historyHeight + footerHeight

    Behavior on Layout.preferredHeight {
        NumberAnimation { duration: Theme.durSlow; easing.type: Easing.OutCubic }
    }

    collapseMarginsHint: true

    property string pickerDeviceId: ""
    property string pickerDeviceName: ""

    Dialogs.FileDialog {
        id: filePicker
        title: root.pickerDeviceName ? "Send to " + root.pickerDeviceName : "Send files"
        fileMode: Dialogs.FileDialog.OpenFiles
        onAccepted: {
            if (root.pickerDeviceId) {
                DripClient.send(root.pickerDeviceId, selectedFiles.map(u => u.toString()))
            }
        }
    }

    contentItem: Item {
        // Panel-wide drop target. Avatars claim the drop themselves; this only
        // tells the row that a drag is in progress.
        DropArea {
            anchors.fill: parent
            onEntered: drag => deviceRow.dragActive = drag.hasUrls
            onExited: deviceRow.dragActive = false
            onDropped: deviceRow.dragActive = false
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            DeviceRow {
                id: deviceRow
                Layout.fillWidth: true
                // With history hidden the row takes the space the section left.
                Layout.fillHeight: !root.showHistory
                Layout.topMargin: Theme.space3
                avatarSize: Plasmoid.configuration.avatarSize
                roomy: !root.showHistory

                onDeviceActivated: (deviceId, deviceName) => {
                    root.pickerDeviceId = deviceId
                    root.pickerDeviceName = deviceName
                    filePicker.open()
                }

                onFilesDroppedOn: (deviceId, urls) => {
                    DripClient.send(deviceId, urls.map(u => u.toString()))
                    deviceRow.dragActive = false
                }
            }

            // Arrivals awaiting a decision, above the divider: the one thing
            // here that is asking a question.
            Column {
                Layout.fillWidth: true
                Layout.topMargin: DripClient.pendingArrivals.count > 0 ? Theme.space2 : 0
                visible: DripClient.pendingArrivals.count > 0
                spacing: 0

                Repeater {
                    model: DripClient.pendingArrivals

                    delegate: ArrivalRow {
                        required property var model

                        width: parent.width
                        fileName: model.name
                        senderName: model.senderName
                        size: model.size

                        onAccepted: DripClient.acceptArrival(model.name)
                        onDeclined: DripClient.declineArrival(model.name)
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space4
                Layout.rightMargin: Theme.space4
                Layout.topMargin: Theme.space2
                Layout.preferredHeight: 1
                visible: root.showHistory
                color: Qt.alpha(Kirigami.Theme.textColor, 0.13)
            }

            // History. Not a drop target.
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.showHistory
                opacity: deviceRow.dragActive ? 0.35 : 1.0
                Behavior on opacity { NumberAnimation { duration: Theme.durBase } }

                ListView {
                    id: activity
                    anchors.fill: parent
                    anchors.topMargin: Theme.space2
                    anchors.bottomMargin: Theme.space2
                    model: DripClient.transfers
                    clip: true
                    spacing: 1
                    boundsBehavior: Flickable.StopAtBounds

                    QQC2.ScrollBar.vertical: QQC2.ScrollBar {
                        policy: QQC2.ScrollBar.AsNeeded
                    }

                    delegate: ActivityRow {
                        required property var model

                        width: activity.width
                        transferId: model.id
                        fileName: model.fileName
                        deviceName: model.deviceName
                        path: model.path
                        size: model.size
                        progress: model.progress
                        incoming: model.incoming
                        transferState: model.state
                        error: model.error
                        queuedAt: model.queuedAt

                        onActivated: DripClient.openPath(model.path)
                        onCancelRequested: DripClient.cancel(model.id)
                        onShowFolderRequested: DripClient.showInFolder(model.path)
                    }

                    add: Transition {
                        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durBase }
                        NumberAnimation { property: "y"; from: -8; duration: Theme.durBase; easing.type: Easing.OutCubic }
                    }
                    displaced: Transition {
                        NumberAnimation { properties: "y"; duration: Theme.durBase; easing.type: Easing.OutCubic }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: root.transferCount === 0
                    text: "Nothing sent or received yet"
                    color: Kirigami.Theme.disabledTextColor
                    font.pixelSize: Theme.sizeBody
                }
            }

            // Status line: connection, destination, and clearing history.
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: root.footerHeight

                Rectangle {
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - Theme.space4 * 2
                    height: 1
                    color: Qt.alpha(Kirigami.Theme.textColor, 0.13)
                }

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.space4
                    anchors.right: clearLabel.visible ? clearLabel.left : parent.right
                    anchors.rightMargin: Theme.space3
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.space2

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 6
                        height: 6
                        radius: 3
                        color: DripClient.connected ? Kirigami.Theme.positiveTextColor
                                                    : Kirigami.Theme.negativeTextColor
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: !DripClient.available ? "engine offline"
                            : DripClient.connected ? "connected" : "tailscale down"
                        color: Kirigami.Theme.disabledTextColor
                        font.pixelSize: Theme.sizeMeta
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "·"
                        visible: destination.text !== ""
                        color: Kirigami.Theme.disabledTextColor
                        font.pixelSize: Theme.sizeMeta
                    }

                    Text {
                        id: destination
                        anchors.verticalCenter: parent.verticalCenter
                        text: DripClient.displayPath(DripClient.destinationRoot)
                        color: destinationHover.hovered ? Kirigami.Theme.highlightColor
                                                        : Kirigami.Theme.disabledTextColor
                        font.pixelSize: Theme.sizeMeta
                        font.underline: destinationHover.hovered
                        elide: Text.ElideMiddle

                        HoverHandler { id: destinationHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: DripClient.openPath(DripClient.destinationRoot) }
                    }
                }

                Text {
                    id: clearLabel
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.space4
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Clear"
                    visible: root.keepHistory && root.transferCount > 0
                    color: clearHover.hovered ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                    font.pixelSize: Theme.sizeMeta

                    HoverHandler { id: clearHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: DripClient.clearHistory() }
                }
            }
        }
    }
}
