import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as Controls
import SnapTrayQml

Rectangle {
    id: root
    required property var backend
    readonly property var controller: backend.longshot
    readonly property var choice: controller.candidate
    readonly property bool result: controller.phase === "result"
    property bool alternativesOpen: false
    property int reviewIndex: -1
    property real zoom: 1
    property int observedPart: -1
    signal adjustRequested()
    signal backRequested()
    color: ComponentTokens.recordingPreviewPanel
    objectName: "longshotWorkspace"

    function timeRange(item) { return backend.formatTime(item.startMs || 0) + "–" + backend.formatTime(item.endMs || 0) }
    function review(index) {
        if (!controller.markers.length) return
        reviewIndex = Math.max(0, Math.min(index, controller.markers.length - 1))
        var marker = controller.markers[reviewIndex]
        controller.selectedPart = marker.part
        resultViewport.contentY = Math.max(0, Math.min((marker.row - controller.partStartRow) * imageCanvas.width / Math.max(1, controller.imageSize.width) - 40,
            resultViewport.contentHeight - resultViewport.height))
    }
    function handleKey(event) {
        if (event.key === Qt.Key_Escape) {
            if (moreMenu.opened) moreMenu.close()
            else if (controller.busy) controller.cancel()
            else if (result) controller.showRecommendation()
            else root.backRequested()
        } else if (!controller.busy && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
            if (result) controller.save()
            else if (controller.candidates.length) controller.generate()
        } else if (!controller.busy && result && (event.modifiers & Qt.ControlModifier)) {
            if (event.key === Qt.Key_S) controller.save()
            else if (event.key === Qt.Key_C) controller.copy()
            else if (event.key === Qt.Key_0) zoom = 1
        }
        event.accepted = true
    }
    Connections {
        target: root.controller
        function onChanged() {
            if (root.observedPart !== root.controller.selectedPart) {
                root.observedPart = root.controller.selectedPart
                resultViewport.contentY = 0
                root.zoom = 1
            }
            if (!root.result) { root.reviewIndex = -1; root.zoom = 1 }
            if (root.controller.phase === "analyzing") root.alternativesOpen = false
        }
        function onResultReady() { resultViewport.contentY = 0; root.reviewIndex = -1; root.zoom = 1 }
    }
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: SemanticTokens.spacing16
        spacing: SemanticTokens.spacing12
        RowLayout {
            visible: !root.controller.busy
            Layout.fillWidth: true
            DialogButton {
                objectName: "longshotBack"
                text: root.result ? qsTr("Choose Content") : qsTr("Recording Preview")
                style: "ghost"
                onClicked: root.result ? root.controller.showRecommendation() : root.backRequested()
            }
            Item { Layout.fillWidth: true }
            Text {
                text: root.result ? qsTr("Long Screenshot Ready") : qsTr("Create Long Screenshot")
                color: SemanticTokens.textPrimary
                font.family: SemanticTokens.fontFamily
                font.pixelSize: SemanticTokens.fontSizeBody
                font.bold: true
            }
        }
        ColumnLayout {
            visible: root.controller.busy
            Layout.fillWidth: true
            Layout.fillHeight: true
            Item { Layout.fillHeight: true }
            Text {
                Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                text: root.controller.status
                color: SemanticTokens.textPrimary; font.pixelSize: SemanticTokens.fontSizeBody
            }
            Text {
                Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                text: root.controller.phase === "analyzing" ? qsTr("We will recommend a suitable range when analysis is complete.") : qsTr("Creating only your selected content.")
                color: SemanticTokens.textSecondary
            }
            Controls.BusyIndicator { Layout.alignment: Qt.AlignHCenter; running: root.controller.busy }
            DialogButton {
                objectName: "longshotCancel"; Layout.alignment: Qt.AlignHCenter
                text: qsTr("Cancel"); onClicked: root.controller.cancel()
            }
            Item { Layout.fillHeight: true }
        }
        Controls.ScrollView {
            id: recommendations
            visible: !root.controller.busy && !root.result
            Layout.fillWidth: true; Layout.fillHeight: true
            contentWidth: availableWidth; clip: true
            ColumnLayout {
                width: recommendations.availableWidth
                spacing: SemanticTokens.spacing16
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    font.pixelSize: SemanticTokens.fontSizeBody + 6; font.bold: true; color: SemanticTokens.textPrimary
                    text: !root.controller.candidates.length ? qsTr("This recording cannot produce a long screenshot yet")
                        : root.choice.reason === "review" ? qsTr("Content found; some seams need a look")
                        : root.choice.partial ? qsTr("We recommend this content") : qsTr("This content can become a long screenshot")
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.WordWrap; color: SemanticTokens.textSecondary
                    text: !root.controller.candidates.length
                        ? root.controller.failureReason === "noScrolling" ? qsTr("There is not enough scrolling content. Choose a range that includes scrolling.")
                        : root.controller.failureReason === "tooLong" ? qsTr("This recording is too long to analyze. Select a shorter range and try again.")
                        : root.controller.failureReason === "noContent" ? qsTr("No continuous content was found. Crop to one content area, or record again while scrolling slowly.")
                        : root.controller.message
                        : root.choice.partial ? qsTr("Some content could not be joined. This selection produces a separate long screenshot and does not include the entire recording.")
                        : qsTr("Review the selected source range, then generate your screenshot.")
                }
                Rectangle {
                    visible: root.controller.candidates.length > 0
                    Layout.fillWidth: true; implicitHeight: choiceLayout.implicitHeight + 32
                    radius: SemanticTokens.radiusSmall; color: ComponentTokens.recordingPreviewPanelHover
                    border.color: ComponentTokens.recordingPreviewBorder
                    ColumnLayout {
                        id: choiceLayout
                        anchors.fill: parent; anchors.margins: SemanticTokens.spacing16
                        spacing: SemanticTokens.spacing12
                        Text {
                            text: root.choice.recommended ? qsTr("Recommended range") : qsTr("Selected range")
                            color: SemanticTokens.textPrimary; font.bold: true
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Repeater {
                                model: [root.choice.startPreview || "", root.choice.endPreview || ""]
                                ColumnLayout {
                                    required property var modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    Image {
                                        Layout.fillWidth: true; Layout.preferredHeight: Math.min(180, recommendations.height * 0.22)
                                        source: modelData; fillMode: Image.PreserveAspectFit; asynchronous: true; cache: false
                                    }
                                    Text {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: index === 0 ? qsTr("Source preview · Start") : qsTr("Source preview · End")
                                        color: SemanticTokens.textSecondary
                                    }
                                }
                            }
                        }
                        Text { text: root.timeRange(root.choice); color: SemanticTokens.textPrimary }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                            text: root.choice.imageCount > 1 ? qsTr("This content is long and will be saved as %1 images.").arg(root.choice.imageCount) : qsTr("Produces 1 image")
                            color: SemanticTokens.textPrimary
                        }
                        Text {
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                            text: root.choice.needsReview ? qsTr("Some seams will be marked for review in the preview.")
                                : root.choice.recommended ? qsTr("The largest continuous range with reliable content.") : qsTr("A continuous range of content.")
                            color: SemanticTokens.textSecondary
                        }
                    }
                }
                Flow {
                    Layout.fillWidth: true; spacing: SemanticTokens.spacing8
                    DialogButton {
                        objectName: "longshotAlternatives"; visible: root.controller.candidates.length > 1
                        text: root.alternativesOpen ? qsTr("Hide other ranges") : qsTr("Other available ranges (%1)").arg(root.controller.candidates.length - 1)
                        style: "ghost"; onClicked: root.alternativesOpen = !root.alternativesOpen
                    }
                    DialogButton { objectName: "longshotAdjust"; text: qsTr("Adjust Analysis Range"); style: !root.controller.candidates.length && root.controller.failureReason !== "error" ? "primary" : "ghost"; onClicked: root.adjustRequested() }
                }
                Repeater {
                    model: root.alternativesOpen ? root.controller.candidates : []
                    Rectangle {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true; implicitHeight: 88
                        color: index === root.controller.selectedCandidate ? ComponentTokens.recordingPreviewPanelPressed : ComponentTokens.recordingPreviewPanelHover
                        radius: SemanticTokens.radiusSmall
                        border.color: index === root.controller.selectedCandidate ? SemanticTokens.accentDefault : ComponentTokens.recordingPreviewBorder
                        RowLayout {
                            anchors.fill: parent; anchors.margins: SemanticTokens.spacing8
                            Image { source: modelData.startPreview; Layout.preferredWidth: 100; Layout.fillHeight: true; fillMode: Image.PreserveAspectFit; asynchronous: true }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Text { text: root.timeRange(modelData) + (modelData.recommended ? " · " + qsTr("Recommended") : ""); color: SemanticTokens.textPrimary }
                                Text { text: modelData.needsReview ? qsTr("Seams need review") : qsTr("Continuous content"); color: SemanticTokens.textSecondary }
                            }
                        }
                        MouseArea { anchors.fill: parent; cursorShape: CursorTokens.clickable; onClicked: root.controller.selectedCandidate = index }
                    }
                }
            }
        }
        Text {
            visible: root.result; Layout.fillWidth: true; wrapMode: Text.WordWrap
            text: qsTr("Selected source range: %1").arg(root.timeRange(root.choice)) + (root.choice.partial ? " · " + qsTr("Includes only part of the recording") : "")
            color: SemanticTokens.textSecondary
        }
        RowLayout {
            visible: root.result && root.controller.markers.length > 0; Layout.fillWidth: true
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: qsTr("%1 areas have seams worth checking.").arg(root.controller.markers.length)
                color: SemanticTokens.textSecondary
            }
            DialogButton { text: qsTr("Review"); visible: root.reviewIndex < 0; onClicked: root.review(0) }
            DialogButton { text: "‹"; visible: root.reviewIndex >= 0; enabled: root.reviewIndex > 0; onClicked: root.review(root.reviewIndex - 1) }
            Text { visible: root.reviewIndex >= 0; text: (root.reviewIndex + 1) + " / " + root.controller.markers.length; color: SemanticTokens.textSecondary }
            DialogButton { text: "›"; visible: root.reviewIndex >= 0; enabled: root.reviewIndex + 1 < root.controller.markers.length; onClicked: root.review(root.reviewIndex + 1) }
            DialogButton { text: qsTr("Done"); visible: root.reviewIndex >= 0; onClicked: root.reviewIndex = -1 }
        }
        RowLayout {
            visible: root.result && root.controller.partCount > 1
            DialogButton { text: "‹"; enabled: root.controller.selectedPart > 0; onClicked: root.controller.selectedPart-- }
            Text { text: qsTr("Image %1 / %2").arg(root.controller.selectedPart + 1).arg(root.controller.partCount); color: SemanticTokens.textPrimary }
            DialogButton { text: "›"; enabled: root.controller.selectedPart + 1 < root.controller.partCount; onClicked: root.controller.selectedPart++ }
        }
        Flickable {
            id: resultViewport; objectName: "longshotViewport"; visible: root.result
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: Math.max(width, imageCanvas.width); contentHeight: imageCanvas.height
            Controls.ScrollBar.vertical: Controls.ScrollBar { }
            Controls.ScrollBar.horizontal: Controls.ScrollBar { }
            Item {
                id: imageCanvas
                width: Math.min(resultViewport.width, Math.max(1, root.controller.imageSize.width)) * root.zoom
                height: width * root.controller.imageSize.height / Math.max(1, root.controller.imageSize.width)
                x: Math.max(0, (resultViewport.width - width) / 2)
                Repeater {
                    model: root.result ? Math.ceil(root.controller.imageSize.height / 1024) : 0
                    Image {
                        required property int index
                        readonly property real ratio: imageCanvas.width / Math.max(1, root.controller.imageSize.width)
                        y: index * 1024 * ratio; width: imageCanvas.width
                        height: Math.min(1024, root.controller.imageSize.height - index * 1024) * ratio
                        source: y + height < resultViewport.contentY - resultViewport.height || y > resultViewport.contentY + resultViewport.height * 2 ? "" : root.controller.preview.toString().indexOf("image://") === 0 ? root.controller.preview + "/" + index : root.controller.preview
                        sourceSize.width: Math.min(2048, Math.ceil(width))
                        fillMode: Image.Stretch; asynchronous: true; cache: false
                    }
                }
                Rectangle {
                    readonly property var marker: root.controller.markers[root.reviewIndex] || {row: 0, endRow: 0, part: -1}
                    visible: root.reviewIndex >= 0 && marker.part === root.controller.selectedPart
                    y: (marker.row - root.controller.partStartRow) * imageCanvas.width / Math.max(1, root.controller.imageSize.width)
                    width: imageCanvas.width
                    height: Math.max(4, (marker.endRow - marker.row) * imageCanvas.width / Math.max(1, root.controller.imageSize.width))
                    color: "transparent"; border.color: SemanticTokens.accentDefault; border.width: 2
                }
            }
            WheelHandler {
                acceptedModifiers: Qt.ControlModifier
                onWheel: event => { root.zoom = Math.max(0.25, Math.min(8, root.zoom * (event.angleDelta.y > 0 ? 1.2 : 1 / 1.2))); event.accepted = true }
            }
        }
        Text {
            visible: !root.controller.busy && root.controller.message !== "" && root.controller.phase !== "error"
            Layout.fillWidth: true; wrapMode: Text.WordWrap; text: root.controller.message; color: SemanticTokens.textSecondary
        }
        RowLayout {
            visible: !root.controller.busy; Layout.fillWidth: true
            DialogButton {
                id: moreButton; objectName: "longshotMore"; visible: root.result; text: qsTr("More…")
                onClicked: {
                    var point = moreButton.mapToItem(root, 0, 0)
                    moreMenu.x = point.x
                    moreMenu.y = Math.max(0, point.y - moreMenu.height)
                    moreMenu.open()
                }
            }
            Item { Layout.fillWidth: true }
            DialogButton {
                objectName: "longshotCopy"; visible: root.result
                text: root.controller.partCount > 1 ? qsTr("Copy Current Image") : qsTr("Copy"); onClicked: root.controller.copy()
            }
            DialogButton {
                objectName: "longshotGenerate"; visible: !root.result && root.controller.candidates.length > 0; style: "primary"
                text: root.controller.message ? qsTr("Retry") : qsTr("Generate Long Screenshot"); onClicked: root.controller.generate()
            }
            DialogButton {
                objectName: "longshotRetryAnalysis"; visible: root.controller.phase === "error" && root.controller.failureReason === "error"; style: "primary"
                text: qsTr("Retry Analysis"); onClicked: root.backend.startLongshot()
            }
            DialogButton {
                objectName: "longshotSave"; visible: root.result; style: "primary"
                text: root.controller.partCount > 1 ? qsTr("Save %1 PNG Images").arg(root.controller.partCount) : qsTr("Save PNG")
                onClicked: root.controller.save()
            }
        }
    }
    ContextActionMenu {
        id: moreMenu
        objectName: "longshotMenu"
        menuWidth: 280
        model: [
            { id: "pin", text: root.controller.partCount > 1 ? qsTr("Pin Current Image") : qsTr("Pin") },
            { id: "annotate", text: root.controller.partCount > 1 ? qsTr("Annotate Current Image in Pin") : qsTr("Annotate in Pin") }
        ]
        onActionTriggered: function(actionId) {
            if (actionId === "pin") root.controller.pin()
            else if (actionId === "annotate") root.controller.annotate()
        }
    }
}
