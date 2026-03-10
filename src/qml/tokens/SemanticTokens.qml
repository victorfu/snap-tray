pragma Singleton
import QtQuick
import SnapTrayQml

/**
 * SemanticTokens: Maps PrimitiveTokens to purpose, switching by theme.
 * Thin wrapper delegating to the C++ DesignSystem singleton.
 */
QtObject {
    // Theme state
    readonly property bool isDarkMode: DesignSystem.isDarkMode

    // ========================================================================
    // Background
    // ========================================================================
    readonly property color backgroundPrimary: DesignSystem.backgroundPrimary
    readonly property color backgroundElevated: DesignSystem.backgroundElevated
    readonly property color backgroundOverlay: DesignSystem.backgroundOverlay

    // ========================================================================
    // Text
    // ========================================================================
    readonly property color textPrimary: DesignSystem.textPrimary
    readonly property color textSecondary: DesignSystem.textSecondary
    readonly property color textTertiary: DesignSystem.textTertiary
    readonly property color textInverse: DesignSystem.textInverse
    readonly property color textOnAccent: DesignSystem.textOnAccent

    // ========================================================================
    // Border
    // ========================================================================
    readonly property color borderDefault: DesignSystem.borderDefault
    readonly property color borderFocus: DesignSystem.borderFocus
    readonly property color borderActive: DesignSystem.borderActive

    // ========================================================================
    // Accent
    // ========================================================================
    readonly property color accentDefault: DesignSystem.accentDefault
    readonly property color accentHover: DesignSystem.accentHover
    readonly property color accentPressed: DesignSystem.accentPressed

    // ========================================================================
    // Status
    // ========================================================================
    readonly property color statusSuccess: DesignSystem.statusSuccess
    readonly property color statusWarning: DesignSystem.statusWarning
    readonly property color statusError: DesignSystem.statusError
    readonly property color statusInfo: DesignSystem.statusInfo

    // ========================================================================
    // Toolbar
    // ========================================================================
    readonly property color toolbarBackground: DesignSystem.toolbarBackground
    readonly property color toolbarIcon: DesignSystem.toolbarIcon
    readonly property color toolbarIconActive: DesignSystem.toolbarIconActive
    readonly property color toolbarSeparator: DesignSystem.toolbarSeparator

    // ========================================================================
    // Shadows
    // ========================================================================
    readonly property color shadowSmallColor: DesignSystem.shadowSmallColor
    readonly property int shadowSmallBlur: DesignSystem.shadowSmallBlur
    readonly property int shadowSmallY: DesignSystem.shadowSmallY
    readonly property color shadowMediumColor: DesignSystem.shadowMediumColor
    readonly property int shadowMediumBlur: DesignSystem.shadowMediumBlur
    readonly property int shadowMediumY: DesignSystem.shadowMediumY
    readonly property color shadowLargeColor: DesignSystem.shadowLargeColor
    readonly property int shadowLargeBlur: DesignSystem.shadowLargeBlur
    readonly property int shadowLargeY: DesignSystem.shadowLargeY

    // ========================================================================
    // Capture overlay (always dark, theme-independent)
    // ========================================================================
    readonly property color captureOverlayDim: DesignSystem.captureOverlayDim
    readonly property color captureOverlayCrosshair: DesignSystem.captureOverlayCrosshair
    readonly property color captureOverlaySelectionBorder: DesignSystem.captureOverlaySelectionBorder
    readonly property color captureOverlayDimensionLabel: DesignSystem.captureOverlayDimensionLabel

    // ========================================================================
    // Capture overlay panel (always dark, theme-independent)
    // ========================================================================
    readonly property color captureOverlayPanelBg: DesignSystem.captureOverlayPanelBg
    readonly property color captureOverlayPanelBgTop: DesignSystem.captureOverlayPanelBgTop
    readonly property color captureOverlayPanelHighlight: DesignSystem.captureOverlayPanelHighlight
    readonly property color captureOverlayPanelBorder: DesignSystem.captureOverlayPanelBorder
    readonly property int captureOverlayPanelRadius: DesignSystem.captureOverlayPanelRadius
    readonly property color captureOverlayKeycapBg: DesignSystem.captureOverlayKeycapBg
    readonly property color captureOverlayKeycapBorder: DesignSystem.captureOverlayKeycapBorder
    readonly property color captureOverlayKeycapText: DesignSystem.captureOverlayKeycapText
    readonly property color captureOverlayHintText: DesignSystem.captureOverlayHintText
    readonly property int captureOverlayKeycapRadius: DesignSystem.captureOverlayKeycapRadius
    readonly property color captureOverlayIconNormal: DesignSystem.captureOverlayIconNormal
    readonly property color captureOverlayIconActive: DesignSystem.captureOverlayIconActive
    readonly property color captureOverlayIconDimmed: DesignSystem.captureOverlayIconDimmed
    readonly property color captureOverlayControlHoverBg: DesignSystem.captureOverlayControlHoverBg
    readonly property color captureOverlayListItemHover: DesignSystem.captureOverlayListItemHover
    readonly property color captureOverlayListItemActive: DesignSystem.captureOverlayListItemActive
    readonly property color captureOverlayListButtonBg: DesignSystem.captureOverlayListButtonBg
    readonly property color captureOverlayListButtonHover: DesignSystem.captureOverlayListButtonHover
    readonly property color captureOverlaySliderTrack: DesignSystem.captureOverlaySliderTrack
    readonly property color captureOverlaySliderFill: DesignSystem.captureOverlaySliderFill
    readonly property color captureOverlaySliderKnob: DesignSystem.captureOverlaySliderKnob

    // ========================================================================
    // Icon overlay color
    // ========================================================================
    readonly property color iconColor: DesignSystem.iconColor

    // ========================================================================
    // Typography (semantic aliases)
    // ========================================================================
    readonly property string fontFamily: DesignSystem.fontFamily
    readonly property int fontSizeH2: DesignSystem.fontSizeH2
    readonly property int fontSizeH3: DesignSystem.fontSizeH3
    readonly property int fontSizeBody: DesignSystem.fontSizeBody
    readonly property int fontSizeCaption: DesignSystem.fontSizeCaption
    readonly property int fontSizeSmall: DesignSystem.fontSizeSmall
    readonly property real letterSpacingDefault: DesignSystem.letterSpacingTight
    readonly property real letterSpacingWide: DesignSystem.letterSpacingWide
    readonly property int fontWeightRegular: DesignSystem.fontWeightRegular
    readonly property int fontWeightMedium: DesignSystem.fontWeightMedium
    readonly property int fontWeightSemiBold: DesignSystem.fontWeightSemiBold

    // ========================================================================
    // Spacing (semantic aliases)
    // ========================================================================
    readonly property int spacing4: DesignSystem.spacing4
    readonly property int spacing8: DesignSystem.spacing8
    readonly property int spacing12: DesignSystem.spacing12
    readonly property int spacing16: DesignSystem.spacing16
    readonly property int spacing40: DesignSystem.spacing40

    // ========================================================================
    // Radius (semantic aliases)
    // ========================================================================
    readonly property int radiusSmall: DesignSystem.radiusSmall
    readonly property int radiusMedium: DesignSystem.radiusMedium

    // ========================================================================
    // Animation (semantic aliases)
    // ========================================================================
    readonly property int durationFast: DesignSystem.durationFast
    readonly property int durationNormal: DesignSystem.durationNormal
    readonly property int durationInteractionHover: DesignSystem.durationInteractionHover
}
