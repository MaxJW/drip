import QtQuick

import dev.drip

import "."

/**
 * One offered file, awaiting a decision. Shown only when auto-accept is off.
 * The bytes have already arrived by this point, so Decline discards the file
 * rather than refusing the transfer -- hence "sent you", not "wants to send".
 */
PromptRow {
    id: root

    required property string fileName
    required property string senderName
    required property double size

    iconSource: "document-save-symbolic"
    title: root.senderName + " sent you"
    subtitle: root.fileName + "  ·  " + DripClient.formatSize(root.size)
    acceptText: "Accept"
    declineText: "Decline"
}
