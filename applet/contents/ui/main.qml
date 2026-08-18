pragma ComponentBehavior: Bound

import QtQuick

import org.kde.plasma.plasmoid

import dev.drip

import "."

/*
 * drip -- Taildrop, by drag and drop.
 *
 * The applet is only a view: dripd owns the tailscaled connection, the inbox
 * and the transfer queue, and this talks to it over D-Bus.
 */
PlasmoidItem {
    id: root

    toolTipMainText: "drip"
    toolTipSubText: {
        if (!DripClient.available) {
            return "Engine not running"
        }
        if (!DripClient.connected) {
            return "Tailscale is not running"
        }
        if (DripClient.pendingArrivals.count > 0) {
            const n = DripClient.pendingArrivals.count
            return n === 1 ? "A file is waiting for you to accept it"
                           : n + " files are waiting for you to accept them"
        }
        if (DripClient.activeCount > 0) {
            return DripClient.activeCount + (DripClient.activeCount === 1 ? " transfer" : " transfers") + " in flight"
        }
        return "Drop a file to send"
    }

    compactRepresentation: CompactRepresentation {
        plasmoidItem: root
    }

    fullRepresentation: FullRepresentation {}

    // Keep the popup up while a drag crosses into it.
    hideOnWindowDeactivate: false

    // Sleeping phones rejoin the tailnet without announcing it on the event
    // bus, so re-ask while the panel is open. dripd only republishes on a real
    // change, making this a local socket round trip and nothing more.
    onExpandedChanged: {
        if (root.expanded) {
            DripClient.refresh()
        }
    }

    Timer {
        running: root.expanded && DripClient.available
        interval: 4000
        repeat: true
        onTriggered: DripClient.refresh()
    }

    // The one case where drip interrupts: an unanswered prompt leaves the file
    // stuck in tailscaled's staging area indefinitely.
    Connections {
        target: DripClient
        function onArrivalPending(fileName, senderName) {
            root.expanded = true
        }
    }

    // Settings are stored twice: Plasma's config dialog needs its own keys to
    // drive Apply and Cancel, and dripd needs a copy because it receives files
    // with no widget running. Only the applet pushes, so there is no loop.
    function syncConfiguration() {
        if (!DripClient.available) {
            return
        }
        if (Plasmoid.configuration.destinationRoot) {
            DripClient.destinationRoot = Plasmoid.configuration.destinationRoot
        }
        DripClient.autoAccept = Plasmoid.configuration.autoAccept
        DripClient.groupBySender = Plasmoid.configuration.groupBySender
        DripClient.keepHistory = Plasmoid.configuration.keepHistory
    }

    // ConfigPropertyMap reports edits through valueChanged, not a notify signal.
    Connections {
        target: Plasmoid.configuration
        function onValueChanged(key, value) {
            root.syncConfiguration()
        }
    }

    // On first run nothing is configured yet, so adopt the daemon's default
    // rather than overwriting it with an empty string.
    function adoptDaemonDefaults() {
        if (!Plasmoid.configuration.destinationRoot && DripClient.destinationRoot) {
            Plasmoid.configuration.destinationRoot = DripClient.destinationRoot
        }
        root.syncConfiguration()
    }

    Component.onCompleted: root.adoptDaemonDefaults()

    // The daemon may come up after the applet does.
    Connections {
        target: DripClient
        function onAvailableChanged() {
            if (DripClient.available) {
                root.adoptDaemonDefaults()
            }
        }
    }
}
