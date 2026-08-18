pragma Singleton

import QtQuick

/**
 * Metrics and motion. Colours and fonts come from Kirigami.Theme, read directly
 * by each component: Kirigami.Theme is an attached property that resolves
 * against an item's place in the scene, and a singleton is not in one.
 */
QtObject {
    readonly property int radiusRow: 6
    readonly property int radiusChip: 4

    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 16
    readonly property int space5: 20

    readonly property int avatarSize: 56
    readonly property real ringWidth: 2.5
    /** Headroom for the hover glow, which the device row would otherwise clip. */
    readonly property int glowInset: 10
    readonly property int arrivalRowHeight: 56

    readonly property int sizeBody: 11
    readonly property int sizeMeta: 10
    readonly property int weightMedium: Font.Medium

    readonly property int durFast: 120
    readonly property int durBase: 180
    readonly property int durSlow: 240

    readonly property real springStiffness: 4.0
    readonly property real springDamping: 0.30
    readonly property real springMass: 0.6
}
