import QtQuick

// Split cold control creation across event-loop iterations on Windows.
Loader {
    active: false
    asynchronous: true
    source: "RecordingControlBar.qml"
    signal loadFinished(bool success)
    onLoaded: loadFinished(true)
    onStatusChanged: if (status === Loader.Error) loadFinished(false)
}
