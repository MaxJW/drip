import QtQuick

import org.kde.plasma.configuration

// Pages of the standard "Configure drip…" dialog.
ConfigModel {
    ConfigCategory {
        name: i18n("Receiving")
        icon: "folder-download-symbolic"
        source: "configGeneral.qml"
    }
}
