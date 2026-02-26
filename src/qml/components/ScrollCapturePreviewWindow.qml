import QtQuick
import SnapTrayQml

/**
 * ScrollCapturePreviewWindow.qml
 *
 * Live preview card for scroll capture, showing the stitched image as it grows.
 * Always dark surface (theme-independent) to render cleanly over arbitrary
 * screen content. Uses the same glass aesthetic as the scroll control bar.
 *
 * Designed to be loaded in a frameless, transparent QQuickView.
 * The C++ bridge sets `previewSource` to an image-provider URL whenever
 * a new stitched frame is available.
 */
Item {
    id: root

    // Set from C++ bridge — image-provider URL updated on each stitch
    property string previewSource: ""

    // Total size includes margin for the soft shadow behind the card
    width: ComponentTokens.scrollPreviewCardWidth + 2 * ComponentTokens.scrollPreviewMargin
    height: ComponentTokens.scrollPreviewCardHeight + 2 * ComponentTokens.scrollPreviewMargin

    // ---- Soft shadow (manual Rectangle, no GraphicalEffects) ----
    Rectangle {
        id: shadow
        anchors.horizontalCenter: card.horizontalCenter
        y: card.y + 6
        width: card.width + 4
        height: card.height + 4
        radius: ComponentTokens.scrollPreviewCardRadius + 2
        color: Qt.rgba(0, 0, 0, 0.30)
    }

    // ---- Card ----
    Rectangle {
        id: card
        anchors.centerIn: parent
        width: ComponentTokens.scrollPreviewCardWidth
        height: ComponentTokens.scrollPreviewCardHeight
        radius: ComponentTokens.scrollPreviewCardRadius
        color: ComponentTokens.scrollPreviewCardBackground

        // Hairline border
        border.width: 1
        border.color: ComponentTokens.scrollPreviewCardBorder

        // ---- Image frame ----
        Rectangle {
            id: imageFrame
            anchors.fill: parent
            anchors.margins: ComponentTokens.scrollPreviewCardInset
            radius: ComponentTokens.scrollPreviewImageRadius
            color: ComponentTokens.scrollPreviewImageBackground
            clip: true

            // Inner hairline border
            border.width: 1
            border.color: ComponentTokens.scrollPreviewImageBorder

            Image {
                id: previewImage
                anchors.fill: parent
                anchors.margins: 1  // inset past the border
                source: root.previewSource
                fillMode: Image.PreserveAspectFit
                smooth: true
                asynchronous: true
                cache: false

                // Fade in smoothly when a new image arrives
                opacity: status === Image.Ready ? 1.0 : 0.0

                Behavior on opacity {
                    NumberAnimation {
                        duration: PrimitiveTokens.durationFast
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }
    }
}
