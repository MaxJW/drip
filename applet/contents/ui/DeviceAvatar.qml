import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Effects
import QtQuick.Shapes

import org.kde.kirigami as Kirigami

import "."

/** One device: a circular picture inside a ring carrying its state, plus an OS badge. */
Item {
    id: root

    required property string deviceId
    required property string deviceName
    required property string os
    required property string owner
    required property string avatarUrl
    required property bool online
    required property bool canReceive
    required property string reason

    property int avatarSize: Theme.avatarSize
    /** 0..1 while a transfer to this device is running, else -1. */
    property real progress: -1
    property bool hovered: false
    /** True briefly after a drop lands, to play the absorb pulse. */
    readonly property bool absorbing: absorb.running

    signal clicked()
    signal filesDropped(var urls)

    Timer { id: absorb; interval: Theme.durBase }

    // Extra height is headroom for the hover glow, which the row would clip.
    implicitWidth: root.avatarSize + Theme.space5
    implicitHeight: Theme.glowInset + root.avatarSize + Theme.space3 + nameLabel.implicitHeight

    readonly property bool live: root.hovered && root.canReceive

    opacity: canReceive ? 1.0 : 0.42
    Behavior on opacity { NumberAnimation { duration: Theme.durBase } }

    Item {
        id: puck

        width: root.avatarSize
        height: root.avatarSize
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.glowInset

        scale: root.absorbing ? 0.88 : (root.live ? 1.06 : 1.0)
        Behavior on scale {
            SpringAnimation {
                spring: Theme.springStiffness
                damping: root.absorbing ? 0.18 : Theme.springDamping
                mass: Theme.springMass
            }
        }

        Rectangle {
            anchors.centerIn: parent
            width: parent.width + 16
            height: parent.height + 16
            radius: width / 2
            color: Kirigami.Theme.highlightColor
            opacity: root.live ? 0.18 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.durFast } }
        }

        Rectangle {
            id: avatarWell
            anchors.fill: parent
            anchors.margins: Theme.ringWidth + 2
            radius: width / 2
            color: Kirigami.Theme.alternateBackgroundColor

            Image {
                id: avatarImage
                anchors.fill: parent
                source: root.avatarUrl ? "image://dripavatar/" + encodeURIComponent(root.avatarUrl) : ""
                sourceSize: Qt.size(width * 2, height * 2)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                visible: false
            }

            MultiEffect {
                anchors.fill: parent
                source: avatarImage
                maskEnabled: true
                maskSource: circleMask
                visible: avatarImage.status === Image.Ready
                saturation: root.online ? 0 : -0.85
            }

            Item {
                id: circleMask
                anchors.fill: parent
                layer.enabled: true
                visible: false
                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: "black"
                }
            }

            Kirigami.Icon {
                anchors.centerIn: parent
                width: parent.width * 0.5
                height: width
                source: Icons.forOs(root.os, Icons.large)
                isMask: true
                color: Kirigami.Theme.disabledTextColor
                visible: avatarImage.status !== Image.Ready
            }
        }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            layer.enabled: true
            layer.samples: 4

            ShapePath {
                strokeColor: root.canReceive ? Qt.alpha(Kirigami.Theme.textColor, 0.16)
                                             : Qt.alpha(Kirigami.Theme.textColor, 0.08)
                strokeWidth: Theme.ringWidth
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                PathAngleArc {
                    centerX: puck.width / 2
                    centerY: puck.height / 2
                    radiusX: (puck.width - Theme.ringWidth) / 2
                    radiusY: (puck.height - Theme.ringWidth) / 2
                    startAngle: 0
                    sweepAngle: 360
                }
            }

            // Progress sweep, or a full ring when simply online.
            ShapePath {
                strokeColor: (root.progress >= 0 || root.live)
                    ? Kirigami.Theme.highlightColor
                    : Qt.alpha(Kirigami.Theme.highlightColor, 0.45)
                strokeWidth: Theme.ringWidth
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap

                PathAngleArc {
                    centerX: puck.width / 2
                    centerY: puck.height / 2
                    radiusX: (puck.width - Theme.ringWidth) / 2
                    radiusY: (puck.height - Theme.ringWidth) / 2
                    startAngle: -90
                    sweepAngle: root.progress >= 0 ? root.progress * 360 : (root.online ? 360 : 0)
                    Behavior on sweepAngle {
                        NumberAnimation { duration: Theme.durSlow; easing.type: Easing.OutCubic }
                    }
                }
            }
        }

        Rectangle {
            width: Math.round(root.avatarSize * 0.36)
            height: width
            radius: width / 2
            x: parent.width - width - 1
            y: parent.height - height - 3
            color: Kirigami.Theme.backgroundColor
            border.width: 2
            border.color: Qt.darker(Kirigami.Theme.backgroundColor, 1.4)

            Kirigami.Icon {
                anchors.centerIn: parent
                width: parent.width * 0.62
                height: width
                isMask: true
                color: root.canReceive ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                source: Icons.forOs(root.os, Icons.badge)
            }
        }

        // Only unreachable devices carry a reason; the name is already on show.
        QQC2.ToolTip.visible: root.hovered && root.reason !== ""
        QQC2.ToolTip.delay: 400
        QQC2.ToolTip.text: root.reason

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: root.canReceive ? Qt.PointingHandCursor : Qt.ArrowCursor
            onEntered: root.hovered = true
            onExited: root.hovered = false
            onClicked: {
                if (root.canReceive) {
                    root.clicked()
                }
            }
        }

        DropArea {
            anchors.fill: parent
            enabled: root.canReceive
            onEntered: root.hovered = true
            onExited: root.hovered = false
            onDropped: drop => {
                if (drop.hasUrls) {
                    root.filesDropped(drop.urls)
                    drop.accepted = true
                    absorb.restart()
                }
                root.hovered = false
            }
        }
    }

    Text {
        id: nameLabel
        anchors.top: puck.bottom
        anchors.topMargin: Theme.space2
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width

        text: root.deviceName
        color: root.canReceive ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
        font.pixelSize: Theme.sizeMeta
        font.weight: Theme.weightMedium
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        maximumLineCount: 1
    }
}
