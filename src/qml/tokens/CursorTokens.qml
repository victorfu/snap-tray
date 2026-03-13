pragma Singleton

import QtQuick

QtObject {
    readonly property int defaultCursor: Qt.ArrowCursor
    readonly property int toolbarControl: Qt.ArrowCursor
    readonly property int clickable: Qt.PointingHandCursor
    readonly property int captureSelection: Qt.CrossCursor
    readonly property int dragReady: Qt.OpenHandCursor
    readonly property int panelDragIdle: Qt.ArrowCursor
    readonly property int panelDragActive: Qt.ClosedHandCursor
    readonly property int dialogMove: Qt.SizeAllCursor
}
