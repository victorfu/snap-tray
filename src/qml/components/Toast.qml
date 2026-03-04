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

    // ---- Layout constants ----
    readonly property int kCornerRadius: 10
    readonly property int kPaddingH: 14
    readonly property int kPaddingV: 10
    readonly property int kDotSize: 8
    readonly property int kDotLeftMargin: 14
    readonly property int kIconTextGap: 10
    readonly property int kScreenToastWidth: 320
    readonly property int kTitleFontSize: 13
    readonly property int kMessageFontSize: 12
    readonly property int kTitleMessageGap: 3

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
    width: fixedWidth ? kScreenToastWidth : computedAutoWidth
    height: contentColumn.height + kPaddingV * 2

    readonly property int computedAutoWidth: {
        var textW = Math.max(titleMetrics.advanceWidth, 80)
        if (root.message.length > 0) {
            textW = Math.max(textW, messageMetrics.advanceWidth)
        }
        return kDotLeftMargin + kDotSize + kIconTextGap + textW + kPaddingH + 4
    }

    TextMetrics {
        id: titleMetrics
        font.pixelSize: root.kTitleFontSize
        font.weight: Font.Medium
        font.family: PrimitiveTokens.fontFamily
        text: root.title
    }

    TextMetrics {
        id: messageMetrics
        font.pixelSize: root.kMessageFontSize
        font.weight: Font.Normal
        font.family: PrimitiveTokens.fontFamily
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

        // Outer shadow glow (very subtle)
        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            radius: kCornerRadius + 1
            color: "transparent"
            border.width: 1
            border.color: isDark ? Qt.rgba(0, 0, 0, 0.5) : Qt.rgba(0, 0, 0, 0.06)
        }

        // Main surface
        Rectangle {
            id: surface
            anchors.fill: parent
            radius: kCornerRadius
            color: surfaceBg

            // Subtle top-edge micro-gradient (Linear's signature depth cue)
            gradient: Gradient {
                GradientStop {
                    position: 0.0
                    color: isDark
                        ? Qt.rgba(1, 1, 1, 0.03) // faint white lift at top
                        : Qt.rgba(1, 1, 1, 0.6)
                }
                GradientStop {
                    position: 0.08
                    color: surfaceBg
                }
                GradientStop {
                    position: 1.0
                    color: surfaceBg
                }
            }

            // 1px hairline border
            border.width: 1
            border.color: surfaceBorder
        }

        // Inner top highlight (1px line — depth illusion)
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: kCornerRadius
            anchors.rightMargin: kCornerRadius
            anchors.topMargin: 1
            height: 1
            color: isDark ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(1, 1, 1, 0.9)
        }

        // Content row: status dot + text
        Row {
            id: contentRow
            anchors.left: parent.left
            anchors.leftMargin: kDotLeftMargin
            anchors.verticalCenter: parent.verticalCenter
            spacing: kIconTextGap

            // Status indicator dot with subtle glow
            Item {
                width: kDotSize
                height: kDotSize
                anchors.verticalCenter: parent.verticalCenter

                // Glow ring behind dot
                Rectangle {
                    anchors.centerIn: parent
                    width: kDotSize + 6
                    height: kDotSize + 6
                    radius: (kDotSize + 6) / 2
                    color: Qt.rgba(root.statusColor.r, root.statusColor.g,
                                   root.statusColor.b, 0.15)
                }

                // Solid dot
                Rectangle {
                    anchors.centerIn: parent
                    width: kDotSize
                    height: kDotSize
                    radius: kDotSize / 2
                    color: root.statusColor
                }
            }

            // Text column
            Column {
                id: contentColumn
                spacing: root.message.length > 0 ? kTitleMessageGap : 0
                width: fixedWidth
                    ? (kScreenToastWidth - kDotLeftMargin - kDotSize - kIconTextGap - kPaddingH)
                    : titleText.implicitWidth

                Text {
                    id: titleText
                    text: root.title
                    color: root.titleColor
                    font.pixelSize: kTitleFontSize
                    font.weight: Font.Medium
                    font.family: PrimitiveTokens.fontFamily
                    width: parent.width
                    wrapMode: fixedWidth ? Text.WordWrap : Text.NoWrap
                    elide: fixedWidth ? Text.ElideNone : Text.ElideRight
                    // Letter spacing for Linear crispness
                    font.letterSpacing: PrimitiveTokens.letterSpacingTight
                }

                Text {
                    id: messageText
                    text: root.message
                    color: root.messageColor
                    font.pixelSize: kMessageFontSize
                    font.weight: Font.Normal
                    font.family: PrimitiveTokens.fontFamily
                    width: parent.width
                    wrapMode: Text.WordWrap
                    visible: root.message.length > 0
                    font.letterSpacing: PrimitiveTokens.letterSpacingTight
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
