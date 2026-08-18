pragma Singleton

import QtQuick

/**
 * A drop that is waiting on the folder-zip question.
 *
 * Shared state, because the drop can start on the tray icon while the question
 * is only ever asked in the panel.
 */
QtObject {
    property string deviceId: ""
    property var paths: []
    property var folders: []

    function hold(targetId, droppedPaths, folderNames) {
        deviceId = targetId
        paths = droppedPaths
        folders = folderNames
    }

    function clear() {
        deviceId = ""
        paths = []
        folders = []
    }
}
