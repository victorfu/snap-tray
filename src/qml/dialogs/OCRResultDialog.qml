import QtQuick
import QtQuick.Controls.Basic
import SnapTrayQml

DialogBase {
    id: root
    property var viewModel: null

    title: qsTr("OCR Result")
    subtitle: viewModel ? viewModel.characterCountText : ""
    iconText: "OCR"
    dialogWidth: 420

    onCloseRequested: if (viewModel) viewModel.close()

    contentItem: Component {
        Item {
            readonly property bool showVerticalRow: viewModel ? viewModel.verticalRetryAvailable : false
            implicitHeight: Math.min(Math.max(180, textArea.contentHeight + 28), 340)
                            + (showVerticalRow ? verticalRow.height + 4 : 0)

            ScrollView {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: showVerticalRow ? verticalRow.top : parent.bottom
                anchors.margins: 14

                TextArea {
                    id: textArea
                    text: viewModel ? viewModel.text : ""
                    onTextChanged: if (viewModel) viewModel.text = text
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    color: SemanticTokens.textPrimary
                    selectionColor: ComponentTokens.inputSelectionBackground
                    selectedTextColor: ComponentTokens.inputSelectionText
                    placeholderText: qsTr("No text recognized")
                    placeholderTextColor: SemanticTokens.textTertiary
                    font.pixelSize: SemanticTokens.fontSizeBody
                    font.family: Qt.platform.os === "osx" ? "Menlo" : "Consolas"

                    background: Rectangle {
                        radius: DesignSystem.inputRadius
                        color: DesignSystem.inputBackground
                        border.width: textArea.activeFocus ? 2 : 1
                        border.color: textArea.activeFocus
                            ? DesignSystem.inputBorderFocus
                            : DesignSystem.inputBorder
                    }

                    Component.onCompleted: {
                        selectAll()
                        forceActiveFocus()
                    }
                }
            }

            // Opt-in retry for a single column of upright CJK glyphs. It only
            // replaces text; the original result can always be restored.
            Item {
                id: verticalRow
                visible: showVerticalRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                height: 32

                Text {
                    anchors.left: parent.left
                    anchors.right: verticalButton.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: viewModel ? viewModel.verticalStatusText : ""
                    color: SemanticTokens.textSecondary
                    font.pixelSize: SemanticTokens.fontSizeCaption
                    font.family: SemanticTokens.fontFamily
                    elide: Text.ElideRight
                }

                DialogButton {
                    id: verticalButton
                    objectName: "verticalRetryButton"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 32
                    style: "ghost"
                    text: !viewModel ? ""
                        : viewModel.verticalRetryInProgress ? qsTr("Cancel")
                        : viewModel.showingVerticalResult ? qsTr("Show Original")
                        : qsTr("Retry as Vertical Text")
                    onClicked: {
                        if (!viewModel) return
                        if (viewModel.verticalRetryInProgress) viewModel.cancelVerticalRetry()
                        else if (viewModel.showingVerticalResult) viewModel.showOriginalResult()
                        else viewModel.retryAsVerticalText()
                    }
                }
            }
        }
    }

    buttonBar: Component {
        Item {
            implicitHeight: 56

            Row {
                anchors.fill: parent
                anchors.leftMargin: 12; anchors.rightMargin: 12
                anchors.topMargin: 8; anchors.bottomMargin: 8
                spacing: 8

                DialogButton {
                    text: qsTr("Close")
                    style: "secondary"
                    width: (parent.width - 16) / 3
                    onClicked: if (viewModel) viewModel.close()
                }

                DialogButton {
                    id: copyBtn
                    // An empty result (e.g. before a vertical retry) has nothing to copy.
                    enabled: viewModel ? viewModel.text.length > 0 : false
                    text: qsTr("Copy")
                    style: "secondary"
                    feedbackText: qsTr("\u2713 Copied!")
                    width: (parent.width - 16) / 3
                    onClicked: {
                        if (viewModel) viewModel.copyText()
                        copyBtn.showFeedback()
                    }
                }

                DialogButton {
                    id: tsvBtn
                    // Reserve the slot so async completion cannot move a click target.
                    enabled: viewModel ? viewModel.hasTsv : false
                    text: qsTr("Copy as TSV")
                    style: "secondary"
                    feedbackText: qsTr("\u2713 Copied!")
                    width: (parent.width - 16) / 3
                    onClicked: {
                        if (viewModel) viewModel.copyAsTsv()
                        tsvBtn.showFeedback()
                    }
                }
            }
        }
    }
}
