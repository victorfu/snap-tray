import QtQuick
import QtQuick.Controls
import SnapTrayQml

ApplicationWindow {
    id: root
    width: 640
    height: 480
    visible: true
    title: qsTr("SnapTray QML")

    AnnotationCanvas {
        anchors.fill: parent
    }
}
