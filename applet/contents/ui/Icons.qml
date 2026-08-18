pragma Singleton

import QtQuick

/**
 * Maps a Tailscale OS string to a themed icon name.
 *
 * Only names that exist in Breeze and are widely carried by third-party icon
 * themes: an unresolved name draws as a blank square, and this is the fallback
 * shown when a device has no profile picture, so it has to be there.
 */
QtObject {
    /** A device picture, when no avatar is available. */
    readonly property int large: 0
    /** The small badge tucked into the corner of a picture. */
    readonly property int badge: 1

    function forOs(os, role) {
        switch (os) {
        case "iOS":
        case "android":
            return role === badge ? "phone" : "smartphone"
        case "macOS":
        case "linux":
            return role === badge ? "computer" : "computer-laptop"
        case "windows":
            return "computer"
        default:
            return role === badge ? "computer" : "computer-laptop"
        }
    }
}
