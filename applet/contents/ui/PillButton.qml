import QtQuick

import org.kde.kirigami as Kirigami

import "."

/** A small text button, painted to match the panel rather than the Plasma style. */
Rectangle {
    id: root

    property alias text: label.text
    /** Filled and prominent, for the affirmative choice. */
    property bool accent: false

    signal clicked()

    implicitWidth: label.implicitWidth + Theme.space3 * 2
    implicitHeight: 26
    radius: height / 2

    color: root.accent
        ? (hover.hovered ? Qt.lighter(Kirigami.Theme.highlightColor, 1.12) : Kirigami.Theme.highlightColor)
        : (hover.hovered ? Qt.alpha(Kirigami.Theme.textColor, 0.16) : Qt.alpha(Kirigami.Theme.textColor, 0.10))

    Behavior on color { ColorAnimation { duration: Theme.durFast } }

    scale: tap.pressed ? 0.95 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.durFast } }

    Text {
        id: label
        anchors.centerIn: parent
        color: root.accent ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
        font.pixelSize: Theme.sizeMeta
        font.weight: Theme.weightMedium
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        id: tap
        onTapped: root.clicked()
    }
}
