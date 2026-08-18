import QtQuick
import QtQuick.Controls as QQC2

import org.kde.kirigami as Kirigami

import "."

/** A small square icon action, sized to sit inside a list row. */
Item {
    id: root

    property alias source: icon.source
    property color hoverColor: Kirigami.Theme.textColor
    property string tooltip

    signal triggered()

    implicitWidth: 22
    implicitHeight: 22

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusChip
        color: hover.hovered ? Qt.alpha(Kirigami.Theme.textColor, 0.14) : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
    }

    Kirigami.Icon {
        id: icon
        anchors.centerIn: parent
        width: 14
        height: 14
        isMask: true
        color: hover.hovered ? root.hoverColor : Kirigami.Theme.disabledTextColor
    }

    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: root.triggered() }

    QQC2.ToolTip.visible: hover.hovered && root.tooltip !== ""
    QQC2.ToolTip.delay: 600
    QQC2.ToolTip.text: root.tooltip
}
