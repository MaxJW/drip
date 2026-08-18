import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM

import dev.drip

// Plasma binds the cfg_* properties to the keys in config/main.xml and drives
// Apply, Cancel and Defaults off them.
KCM.SimpleKCM {
    id: page

    property alias cfg_destinationRoot: destinationField.text
    property alias cfg_autoAccept: autoAcceptBox.checked
    property alias cfg_groupBySender: groupBySenderBox.checked
    property alias cfg_keepHistory: keepHistoryBox.checked

    // Not an alias: the control is a set of buttons, so the value has to be
    // mapped to and from the selected index rather than read off a property.
    property int cfg_avatarSize: 56
    readonly property var avatarSizes: [44, 56, 72]

    Dialogs.FolderDialog {
        id: folderPicker
        title: i18n("Save received files to")
        currentFolder: destinationField.text ? "file://" + destinationField.text : ""
        onAccepted: destinationField.text = selectedFolder.toString().replace(/^file:\/\//, "")
    }

    implicitWidth: Kirigami.Units.gridUnit * 30

    Kirigami.FormLayout {
        anchors.left: parent.left
        anchors.right: parent.right

        RowLayout {
            Kirigami.FormData.label: i18n("Save files to:")
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            QQC2.TextField {
                id: destinationField
                Layout.fillWidth: true
                // A TextField's implicit width tracks its text, so the layout
                // is given a fixed figure to distribute from instead.
                Layout.preferredWidth: Kirigami.Units.gridUnit * 14
                Layout.minimumWidth: Kirigami.Units.gridUnit * 5
                placeholderText: DripClient.destinationRoot
            }

            QQC2.Button {
                icon.name: "document-open-folder"
                text: i18n("Browse…")
                onClicked: folderPicker.open()
            }
        }

        Item { Kirigami.FormData.isSection: true }

        QQC2.CheckBox {
            id: autoAcceptBox
            Kirigami.FormData.label: i18n("Incoming files:")
            text: i18n("Save automatically")
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 22
            visible: !autoAcceptBox.checked
            text: i18n("drip will ask before saving. The file has already reached this machine by "
                     + "that point — Tailscale has no way to ask first — so declining deletes it "
                     + "rather than refusing the transfer.")
            font: Kirigami.Theme.smallFont
            opacity: 0.7
            wrapMode: Text.Wrap
        }

        QQC2.CheckBox {
            id: groupBySenderBox
            text: i18n("Sort into a folder per sender")
        }

        Item { Kirigami.FormData.isSection: true }

        QQC2.CheckBox {
            id: keepHistoryBox
            Kirigami.FormData.label: i18n("History:")
            text: i18n("Remember finished transfers")
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 22
            visible: !keepHistoryBox.checked
            text: i18n("Transfers disappear from the panel as soon as they finish, and anything "
                     + "already recorded is discarded. Files themselves are untouched.")
            font: Kirigami.Theme.smallFont
            opacity: 0.7
            wrapMode: Text.Wrap
        }

        Item { Kirigami.FormData.isSection: true }

        QQC2.ComboBox {
            id: sizeBox
            Kirigami.FormData.label: i18n("Device pictures:")
            model: [i18n("Small"), i18n("Medium"), i18n("Large")]

            currentIndex: {
                const index = page.avatarSizes.indexOf(page.cfg_avatarSize)
                return index >= 0 ? index : 1
            }
            onActivated: index => page.cfg_avatarSize = page.avatarSizes[index]
        }
    }
}
