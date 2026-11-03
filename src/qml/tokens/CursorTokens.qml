pragma Singleton

import QtQuick

QtObject {
    readonly property int defaultCursor: Qt.ArrowCursor
    readonly property int toolbarControl: Qt.ArrowCursor
    readonly property int clickable: Qt.PointingHandCursor
    readonly property int captureSelection: Qt.CrossCursor
    readonly property int dragReady: Qt.ArrowCursor
    readonly property int panelDragIdle: Qt.ArrowCursor
    readonly property int panelDragActive: Qt.ClosedHandCursor
    readonly property int dialogMove: Qt.ArrowCursor
    // Recording preview editing: pick a point on the video (a zoom's centre).
    readonly property int pickPoint: Qt.CrossCursor
    // Moving and resizing a rectangle on the video (a mask).
    readonly property int moveItem: Qt.SizeAllCursor
    readonly property int resizeDiagonal: Qt.SizeFDiagCursor
    // Moving a split point along the recording timeline.
    readonly property int resizeHorizontal: Qt.SizeHorCursor
}
