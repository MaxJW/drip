pragma Singleton

import QtQuick

/** Maps a Tailscale OS string to a themed icon name. */
QtObject {
    /** A device picture, when no avatar is available. */
    readonly property int large: 0
    /** The small badge tucked into the corner of a picture. */
    readonly property int badge: 1

    function forOs(os, role) {
        switch (os) {
        case "iOS":
            return role === badge ? "phone-apple-iphone" : "computer-apple-ipad"
        case "macOS":
            return role === badge ? "computer-apple" : "computer-apple-ipad"
        case "android":
            return "phone"
        case "windows":
            return "computer"
        default:
            return "computer-laptop"
        }
    }
}
