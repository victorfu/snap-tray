import QtQuick
import SnapTrayQml

/**
 * Toast notification — Linear app aesthetic.
 *
 * Ultra-minimal, crisp dark surface with subtle 1px borders, micro-gradients,
 * and a gentle status-colored indicator dot. No heavy accent strips.
 *
 * Controlled from C++ via properties: level, title, message, anchorMode.
 * Call show() to display and startHide() to dismiss.
 */
Item {
    id: root

    // ---- Public properties set from C++ ----
    property int level: 3              // 0=Success, 1=Error, 2=Warning, 3=Info
    property string title: ""
    property string message: ""
    property int anchorMode: 0         // 0=ScreenTopRight, 1=ParentTopCenter, 2=NearRect
    property int displayDuration: ComponentTokens.toastDisplayDuration
    property bool fixedWidth: false    // true for screen-level (320px), false for auto-width

    // ---- Theme ----
    readonly property bool isDark: ThemeManager ? ThemeManager.isDarkMode : false

    // ---- Signals ----
    signal hideFinished()

    // ---- Status dot color (from token system) ----
    readonly property color statusColor: {
        switch (level) {
        case 0: return SemanticTokens.statusSuccess
        case 1: return SemanticTokens.statusError
        case 2: return SemanticTokens.statusWarning
        case 3: return SemanticTokens.statusInfo
        }
        return SemanticTokens.statusInfo
    }

    // ---- Surface colors (from token system) ----
    readonly property color surfaceBg: ComponentTokens.toastBackground
    readonly property color surfaceBorder: ComponentTokens.toastBorder
    readonly property color titleColor: ComponentTokens.toastTitleText
    readonly property color messageColor: ComponentTokens.toastMessageText

    // ---- Computed width ----
    width: fixedWidth ? ComponentTokens.toastScreenWidth : computedAutoWidth
    height: contentColumn.height + ComponentTokens.toastPaddingV * 2

    readonly property int computedAutoWidth: {
        var textW = Math.max(titleMetrics.advanceWidth, 80)
        if (root.message.length > 0) {
            textW = Math.max(textW, messageMetrics.advanceWidth)
        }
        return ComponentTokens.toastDotLeftMargin + ComponentTokens.toastDotSize + ComponentTokens.toastIconTextGap + textW + ComponentTokens.toastPaddingH + 4
    }

    TextMetrics {
        id: titleMetrics
        font.pixelSize: ComponentTokens.toastTitleFontSize
        font.weight: Font.Medium
        font.family: SemanticTokens.fontFamily
        text: root.title
    }

    TextMetrics {
        id: messageMetrics
        font.pixelSize: ComponentTokens.toastMessageFontSize
        font.weight: Font.Normal
        font.family: SemanticTokens.fontFamily
        text: root.message
    }

    // ---- Visual state ----
    opacity: 0
    visible: false
    property real slideOffset: 0

    // ---- Toast body ----
    Item {
        id: toastBody
        anchors.fill: parent
        transform: Translate { y: root.slideOffset }

        // Main surface
        Rectangle {
            id: surface
            anchors.fill: parent
            radius: ComponentTokens.toastRadius
            color: surfaceBg

            // 1px hairline border
            border.width: 1
            border.color: surfaceBorder
        }

        // Content row: status dot + text
        Row {
            id: contentRow
            anchors.left: parent.left
            anchors.leftMargin: ComponentTokens.toastDotLeftMargin
            anchors.verticalCenter: parent.verticalCenter
            spacing: ComponentTokens.toastIconTextGap

            // Status indicator dot with subtle glow
            Item {
                width: ComponentTokens.toastDotSize
                height: ComponentTokens.toastDotSize
                anchors.verticalCenter: parent.verticalCenter

                // Glow ring behind dot
                Rectangle {
                    anchors.centerIn: parent
                    width: ComponentTokens.toastDotSize + 6
                    height: ComponentTokens.toastDotSize + 6
                    radius: (ComponentTokens.toastDotSize + 6) / 2
                    color: Qt.rgba(root.statusColor.r, root.statusColor.g,
                                   root.statusColor.b, 0.15)
                }

                // Solid dot
                Rectangle {
                    anchors.centerIn: parent
                    width: ComponentTokens.toastDotSize
                    height: ComponentTokens.toastDotSize
                    radius: ComponentTokens.toastDotSize / 2
                    color: root.statusColor
                }
            }

            // Text column
            Column {
                id: contentColumn
                spacing: root.message.length > 0 ? ComponentTokens.toastTitleMessageGap : 0
                width: fixedWidth
                    ? (ComponentTokens.toastScreenWidth - ComponentTokens.toastDotLeftMargin - ComponentTokens.toastDotSize - ComponentTokens.toastIconTextGap - ComponentTokens.toastPaddingH)
                    : titleText.implicitWidth

                Text {
                    id: titleText
                    text: root.title
                    color: root.titleColor
                    font.pixelSize: ComponentTokens.toastTitleFontSize
                    font.weight: Font.Medium
                    font.family: SemanticTokens.fontFamily
                    width: parent.width
                    wrapMode: fixedWidth ? Text.WordWrap : Text.NoWrap
                    elide: fixedWidth ? Text.ElideNone : Text.ElideRight
                    // Letter spacing for Linear crispness
                    font.letterSpacing: SemanticTokens.letterSpacingDefault
                }

                Text {
                    id: messageText
                    text: root.message
                    color: root.messageColor
                    font.pixelSize: ComponentTokens.toastMessageFontSize
                    font.weight: Font.Normal
                    font.family: SemanticTokens.fontFamily
                    width: parent.width
                    wrapMode: Text.WordWrap
                    visible: root.message.length > 0
                    font.letterSpacing: SemanticTokens.letterSpacingDefault
                }
            }
        }
    }

    // ---- Display timer ----
    Timer {
        id: displayTimer
        interval: root.displayDuration
        repeat: false
        onTriggered: root.startHide()
    }

    // ---- Show animation (snappy, Linear-style) ----
    ParallelAnimation {
        id: showAnimation
        NumberAnimation {
            target: root
            property: "opacity"
            from: 0; to: 1
            duration: ComponentTokens.toastShowDuration
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: root
            property: "slideOffset"
            from: -12; to: 0
            duration: ComponentTokens.toastShowDuration
            easing.type: Easing.OutCubic
        }
    }

    // ---- Hide animation ----
    ParallelAnimation {
        id: hideAnimation
        NumberAnimation {
            target: root
            property: "opacity"
            to: 0
            duration: ComponentTokens.toastHideDuration
            easing.type: Easing.InCubic
        }
        NumberAnimation {
            target: root
            property: "slideOffset"
            to: -8
            duration: ComponentTokens.toastHideDuration
            easing.type: Easing.InCubic
        }
        onFinished: {
            root.visible = false
            root.hideFinished()
        }
    }

    // ---- Public methods ----
    function show() {
        hideAnimation.stop()
        visible = true
        showAnimation.start()
        displayTimer.restart()
    }

    function startHide() {
        displayTimer.stop()
        showAnimation.stop()
        hideAnimation.start()
    }
}
