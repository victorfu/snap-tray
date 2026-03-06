pragma Singleton
import QtQuick

/**
 * ComponentTokens: UI component-specific design values.
 *
 * References SemanticTokens for theme-aware colors and
 * PrimitiveTokens for fixed sizing/spacing values.
 * Components should import ComponentTokens (or SemanticTokens) rather
 * than PrimitiveTokens directly.
 */
QtObject {
    // ========================================================================
    // Button: Primary
    // ========================================================================
    readonly property color buttonPrimaryBackground: SemanticTokens.accentDefault
    readonly property color buttonPrimaryBackgroundHover: SemanticTokens.accentHover
    readonly property color buttonPrimaryBackgroundPressed: SemanticTokens.accentPressed
    readonly property color buttonPrimaryText: PrimitiveTokens.white
    readonly property int buttonPrimaryRadius: PrimitiveTokens.radiusMedium

    // ========================================================================
    // Button: Secondary
    // ========================================================================
    readonly property color buttonSecondaryBackground: SemanticTokens.backgroundElevated
    readonly property color buttonSecondaryBackgroundHover: SemanticTokens.isDarkMode
        ? PrimitiveTokens.gray800 : PrimitiveTokens.gray200
    readonly property color buttonSecondaryBackgroundPressed: SemanticTokens.isDarkMode
        ? PrimitiveTokens.gray700 : PrimitiveTokens.gray300
    readonly property color buttonSecondaryText: SemanticTokens.textPrimary
    readonly property color buttonSecondaryBorder: SemanticTokens.borderDefault
    readonly property int buttonSecondaryRadius: PrimitiveTokens.radiusMedium

    // ========================================================================
    // Button: Ghost
    // ========================================================================
    readonly property color buttonGhostBackground: "transparent"
    readonly property color buttonGhostBackgroundHover: SemanticTokens.isDarkMode
        ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0, 0, 0, 0.05)
    readonly property color buttonGhostBackgroundPressed: SemanticTokens.isDarkMode
        ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(0, 0, 0, 0.08)
    readonly property color buttonGhostText: SemanticTokens.textPrimary
    readonly property int buttonGhostRadius: PrimitiveTokens.radiusMedium

    // ========================================================================
    // Button: Disabled
    // ========================================================================
    readonly property color buttonDisabledBackground: SemanticTokens.isDarkMode
        ? PrimitiveTokens.gray800 : PrimitiveTokens.gray200
    readonly property color buttonDisabledText: SemanticTokens.isDarkMode
        ? PrimitiveTokens.gray600 : PrimitiveTokens.gray400
    readonly property color buttonDisabledBorder: SemanticTokens.isDarkMode
        ? PrimitiveTokens.gray700 : PrimitiveTokens.gray300

    // ========================================================================
    // Toolbar
    // ========================================================================
    readonly property int toolbarHeight: 44
    readonly property int toolbarIconSize: PrimitiveTokens.iconSizeMedium
    readonly property int toolbarPadding: PrimitiveTokens.spacing8
    readonly property int toolbarRadius: PrimitiveTokens.radiusLarge
    readonly property color toolbarBackground: SemanticTokens.toolbarBackground
    readonly property color toolbarSeparator: SemanticTokens.toolbarSeparator
    readonly property color toolbarIcon: SemanticTokens.toolbarIcon
    readonly property color toolbarIconActive: SemanticTokens.toolbarIconActive

    // ========================================================================
    // Tooltip
    // ========================================================================
    readonly property color tooltipBackground: SemanticTokens.isDarkMode
        ? Qt.rgba(0.12, 0.12, 0.12, 0.90) : Qt.rgba(1, 1, 1, 0.94)
    readonly property color tooltipBackgroundTop: SemanticTokens.isDarkMode
        ? Qt.rgba(0.12, 0.12, 0.12, 0.98) : PrimitiveTokens.white
    readonly property color tooltipBorder: SemanticTokens.isDarkMode
        ? PrimitiveTokens.gray800 : PrimitiveTokens.gray300
    readonly property color tooltipHighlight: SemanticTokens.isDarkMode
        ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(1, 1, 1, 0.32)
    readonly property color tooltipText: SemanticTokens.textPrimary
    readonly property int tooltipRadius: PrimitiveTokens.radiusSmall
    readonly property int tooltipPadding: PrimitiveTokens.spacing8

    // ========================================================================
    // Toast
    // ========================================================================
    readonly property color toastBackground: SemanticTokens.isDarkMode
        ? Qt.rgba(0.12, 0.12, 0.12, 0.90) : Qt.rgba(1, 1, 1, 0.92)
    readonly property color toastBorder: SemanticTokens.isDarkMode
        ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.06)
    readonly property color toastTitleText: SemanticTokens.isDarkMode
        ? PrimitiveTokens.gray100 : PrimitiveTokens.gray900
    readonly property color toastMessageText: SemanticTokens.isDarkMode
        ? PrimitiveTokens.gray400 : PrimitiveTokens.gray600
    readonly property int toastRadius: PrimitiveTokens.radiusLarge
    readonly property int toastPadding: PrimitiveTokens.spacing16
    readonly property int toastShowDuration: 180
    readonly property int toastHideDuration: PrimitiveTokens.durationInteractionState
    readonly property int toastDisplayDuration: 2500
    readonly property int toastPaddingH: 14
    readonly property int toastPaddingV: 10
    readonly property int toastDotSize: 8
    readonly property int toastDotLeftMargin: 14
    readonly property int toastIconTextGap: 10
    readonly property int toastScreenWidth: 320
    readonly property int toastTitleFontSize: PrimitiveTokens.fontSizeBody
    readonly property int toastMessageFontSize: PrimitiveTokens.fontSizeSmallBody
    readonly property int toastTitleMessageGap: 3

    // ========================================================================
    // Panel
    // ========================================================================
    readonly property color panelBackground: SemanticTokens.backgroundElevated
    readonly property color panelBorder: SemanticTokens.borderDefault
    readonly property int panelRadius: PrimitiveTokens.radiusLarge
    readonly property int panelPadding: PrimitiveTokens.spacing16

    // ========================================================================
    // Dialog
    // ========================================================================
    readonly property color dialogBackground: SemanticTokens.backgroundPrimary
    readonly property color dialogBorder: SemanticTokens.borderDefault
    readonly property color dialogOverlay: SemanticTokens.backgroundOverlay
    readonly property int dialogRadius: PrimitiveTokens.radiusXL
    readonly property int dialogPadding: PrimitiveTokens.spacing24

    // ========================================================================
    // Badge (transient zoom/opacity indicators)
    // ========================================================================
    readonly property color badgeBackground: SemanticTokens.isDarkMode
        ? Qt.rgba(0, 0, 0, 0.71) : Qt.rgba(1, 1, 1, 0.78)
    readonly property color badgeHighlight: Qt.rgba(1, 1, 1, 0.04)
    readonly property color badgeBorder: toastBorder
    readonly property color badgeText: SemanticTokens.isDarkMode
        ? PrimitiveTokens.white : Qt.rgba(0.16, 0.16, 0.16, 1.0)
    readonly property int badgeRadius: 6
    readonly property int badgePaddingH: 10
    readonly property int badgePaddingV: 5
    readonly property int badgeFontSize: 12
    readonly property int badgeFadeInDuration: 150
    readonly property int badgeFadeOutDuration: PrimitiveTokens.durationInteractionState
    readonly property int badgeDisplayDuration: 1500

    // ========================================================================
    // Settings
    // ========================================================================
    readonly property color settingsSidebarBg: SemanticTokens.isDarkMode
        ? Qt.rgba(0.07, 0.07, 0.08, 1.0)
        : Qt.rgba(0.96, 0.96, 0.97, 1.0)
    readonly property color settingsSidebarActiveItem: SemanticTokens.isDarkMode
        ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(0, 0, 0, 0.04)
    readonly property color settingsSidebarHoverItem: SemanticTokens.isDarkMode
        ? Qt.rgba(1, 1, 1, 0.03) : Qt.rgba(0, 0, 0, 0.02)
    readonly property color settingsSectionText: SemanticTokens.textSecondary
    readonly property int settingsSidebarWidth: 200
    readonly property int settingsSidebarMinWidth: 160
    readonly property int settingsSidebarMaxWidth: 220
    readonly property int settingsContentPadding: 24
    readonly property int settingsSectionTopPadding: PrimitiveTokens.spacing16
    readonly property int settingsColumnSpacing: PrimitiveTokens.spacing4
    readonly property int settingsBottomBarHeight: 52
    readonly property int settingsBottomBarPaddingH: PrimitiveTokens.spacing16
    readonly property int settingsToastTopMargin: PrimitiveTokens.spacing12

    // Form Controls
    readonly property color toggleTrackOn: SemanticTokens.accentDefault
    readonly property color toggleTrackOff: SemanticTokens.isDarkMode
        ? PrimitiveTokens.gray700 : PrimitiveTokens.gray300
    readonly property color toggleKnob: PrimitiveTokens.white
    readonly property int toggleWidth: 36
    readonly property int toggleHeight: 20
    readonly property int toggleKnobSize: 16
    readonly property int toggleKnobRadius: PrimitiveTokens.radiusMedium
    readonly property int toggleKnobInset: PrimitiveTokens.spacing2
    readonly property int toggleLabelIndent: 140

    readonly property color inputBackground: SemanticTokens.isDarkMode
        ? Qt.rgba(1, 1, 1, 0.04) : Qt.rgba(0, 0, 0, 0.03)
    readonly property color inputBorder: SemanticTokens.borderDefault
    readonly property color inputBorderFocus: SemanticTokens.borderFocus
    readonly property int inputRadius: PrimitiveTokens.radiusSmall
    readonly property int inputHeight: 32

    readonly property color sliderTrack: SemanticTokens.isDarkMode
        ? PrimitiveTokens.gray700 : PrimitiveTokens.gray300
    readonly property color sliderFill: SemanticTokens.accentDefault
    readonly property color sliderKnob: PrimitiveTokens.white
    readonly property int sliderHeight: 4
    readonly property int sliderKnobSize: 14

    // ========================================================================
    // Watermark Settings
    // ========================================================================
    readonly property int watermarkPreviewColumnWidth: 140
    readonly property int watermarkPreviewSize: 120

    // ========================================================================
    // Combo Popup
    // ========================================================================
    readonly property int comboPopupMaxHeight: 300

    // ========================================================================
    // Info Panel (used in settings for contextual information)
    // ========================================================================
    readonly property color infoPanelBg: SemanticTokens.isDarkMode
        ? Qt.rgba(0.15, 0.22, 0.35, 1.0) : Qt.rgba(0.93, 0.96, 1.0, 1.0)
    readonly property color infoPanelBorder: SemanticTokens.isDarkMode
        ? Qt.rgba(0.25, 0.35, 0.55, 1.0) : Qt.rgba(0.7, 0.8, 0.95, 1.0)
    readonly property color infoPanelText: SemanticTokens.isDarkMode
        ? Qt.rgba(0.7, 0.82, 1.0, 1.0) : Qt.rgba(0.15, 0.25, 0.5, 1.0)

    // ========================================================================
    // Focus Ring
    // ========================================================================
    readonly property color focusRingColor: SemanticTokens.borderFocus
    readonly property int focusRingWidth: 2
    readonly property int focusRingOffset: 2
    readonly property int focusRingRadius: PrimitiveTokens.radiusMedium

    // ========================================================================
    // Recording Preview
    // ========================================================================
    readonly property color recordingPreviewBgStart: SemanticTokens.isDarkMode
        ? PrimitiveTokens.gray950 : PrimitiveTokens.gray100
    readonly property color recordingPreviewBgEnd: SemanticTokens.isDarkMode
        ? PrimitiveTokens.black : PrimitiveTokens.gray200
    readonly property color recordingPreviewPanel: SemanticTokens.isDarkMode
        ? PrimitiveTokens.gray900 : PrimitiveTokens.white
    readonly property color recordingPreviewPanelHover: SemanticTokens.isDarkMode
        ? PrimitiveTokens.gray850 : PrimitiveTokens.gray100
    readonly property color recordingPreviewPanelPressed: SemanticTokens.isDarkMode
        ? PrimitiveTokens.gray800 : PrimitiveTokens.gray200
    readonly property color recordingPreviewBorder: SemanticTokens.isDarkMode
        ? PrimitiveTokens.gray800 : PrimitiveTokens.gray300
    readonly property color recordingPreviewPlayOverlay: SemanticTokens.isDarkMode
        ? Qt.rgba(0, 0, 0, 0.6) : Qt.rgba(0, 0, 0, 0.35)
    readonly property color recordingPreviewTrimOverlay: SemanticTokens.isDarkMode
        ? Qt.rgba(0, 0, 0, 0.55) : Qt.rgba(0, 0, 0, 0.20)
    readonly property color recordingPreviewPlayhead: PrimitiveTokens.white
    readonly property color recordingPreviewPlayheadBorder: SemanticTokens.isDarkMode
        ? PrimitiveTokens.gray700 : PrimitiveTokens.gray400
    readonly property color recordingPreviewControlHighlight: SemanticTokens.isDarkMode
        ? Qt.rgba(PrimitiveTokens.accentDefault.r, PrimitiveTokens.accentDefault.g,
                  PrimitiveTokens.accentDefault.b, 0.18)
        : Qt.rgba(PrimitiveTokens.accentDefault.r, PrimitiveTokens.accentDefault.g,
                  PrimitiveTokens.accentDefault.b, 0.12)
    readonly property color recordingPreviewDangerHover: SemanticTokens.isDarkMode
        ? Qt.rgba(PrimitiveTokens.red500.r, PrimitiveTokens.red500.g, PrimitiveTokens.red500.b, 0.14)
        : Qt.rgba(PrimitiveTokens.red500.r, PrimitiveTokens.red500.g, PrimitiveTokens.red500.b, 0.10)
    readonly property color recordingPreviewDangerPressed: SemanticTokens.isDarkMode
        ? Qt.rgba(PrimitiveTokens.red500.r, PrimitiveTokens.red500.g, PrimitiveTokens.red500.b, 0.24)
        : Qt.rgba(PrimitiveTokens.red500.r, PrimitiveTokens.red500.g, PrimitiveTokens.red500.b, 0.18)
    readonly property color recordingPreviewPrimaryButton: SemanticTokens.accentDefault
    readonly property color recordingPreviewPrimaryButtonHover: SemanticTokens.accentHover
    readonly property color recordingPreviewPrimaryButtonPressed: SemanticTokens.accentPressed
    readonly property color recordingPreviewPrimaryButtonIcon: PrimitiveTokens.white

    readonly property int recordingPreviewControlButtonSize: 36
    readonly property int recordingPreviewControlButtonHeight: 30
    readonly property int recordingPreviewIconSize: iconSizeToolbar
    readonly property int recordingPreviewActionIconSize: iconSizeAction

    // ========================================================================
    // Recording Boundary
    // ========================================================================
    readonly property color recordingBoundaryGradientStart: PrimitiveTokens.boundaryBlue
    readonly property color recordingBoundaryGradientMid1: PrimitiveTokens.indigo500
    readonly property color recordingBoundaryGradientMid2: PrimitiveTokens.purple400
    readonly property color recordingBoundaryGradientEnd: PrimitiveTokens.boundaryPurple
    readonly property color recordingBoundaryPaused: PrimitiveTokens.amber600
    readonly property int recordingBoundaryLoopDuration: PrimitiveTokens.durationBoundaryLoop

    // ========================================================================
    // Countdown Overlay
    // ========================================================================
    readonly property color countdownOverlay: SemanticTokens.backgroundOverlay
    readonly property color countdownText: PrimitiveTokens.white
    readonly property int countdownFontSize: 200
    readonly property int countdownScaleDuration: 800
    readonly property int countdownFadeDuration: 267

    // ========================================================================
    // Icon sizes (component-level aliases with semantic names)
    // ========================================================================
    readonly property int iconSizeToolbar: PrimitiveTokens.iconSizeMedium
    readonly property int iconSizeMenu: PrimitiveTokens.iconSizeSmall
    readonly property int iconSizeAction: PrimitiveTokens.iconSizeLarge

    // ========================================================================
    // Recording indicator
    // ========================================================================
    readonly property int recordingIndicatorDuration: 800
    readonly property color recordingAudioActive: PrimitiveTokens.green500

    // ========================================================================
    // Recording Control Bar (Overlay — always dark, theme-independent)
    // ========================================================================
    // Glass background
    readonly property color recordingControlBarBg: Qt.rgba(0, 0, 0, 0.6)
    readonly property color recordingControlBarBgTop: Qt.rgba(0, 0, 0, 0.68)
    readonly property color recordingControlBarHighlight: Qt.rgba(1, 1, 1, 0.06)
    readonly property color recordingControlBarBorder: Qt.rgba(1, 1, 1, 0.12)
    // Text & separators
    readonly property color recordingControlBarText: PrimitiveTokens.white
    readonly property color recordingControlBarSeparator: Qt.rgba(1, 1, 1, 0.2)
    // Interactive states
    readonly property color recordingControlBarHoverBg: Qt.rgba(1, 1, 1, 0.12)
    readonly property color recordingControlBarIconNormal: "#CCCCCC"
    readonly property color recordingControlBarIconActive: PrimitiveTokens.white
    readonly property color recordingControlBarIconRecord: PrimitiveTokens.annotationRed
    readonly property color recordingControlBarIconCancel: "#FF453A"
    // Sizing
    readonly property int recordingControlBarRadius: PrimitiveTokens.radiusMedium + 2
    readonly property int recordingControlBarHeight: PrimitiveTokens.spacing32
    readonly property int recordingControlBarButtonSize: PrimitiveTokens.spacing24
    readonly property int recordingControlBarButtonSpacing: PrimitiveTokens.spacing2
    readonly property int recordingControlBarMarginH: PrimitiveTokens.radiusMedium + 2
    readonly property int recordingControlBarMarginV: PrimitiveTokens.spacing4
    readonly property int recordingControlBarItemSpacing: 6
    readonly property int recordingControlBarIndicatorSize: PrimitiveTokens.spacing12
    // Typography
    readonly property int recordingControlBarFontSize: PrimitiveTokens.fontSizeCaption
    readonly property int recordingControlBarFontSizeSmall: PrimitiveTokens.fontSizeSmall
    readonly property int recordingControlBarIconSize: PrimitiveTokens.iconSizeSmall
    readonly property int recordingControlBarButtonRadius: 6

    // ========================================================================
    // Recording Region Selector (Overlay — always dark, theme-independent)
    // ========================================================================
    // Overlay dimming
    readonly property color recordingRegionOverlayDim: Qt.rgba(0, 0, 0, 0.39)
    // Crosshair
    readonly property color recordingRegionCrosshair: Qt.rgba(1, 1, 1, 200.0 / 255.0)
    // Selection border gradient (reuse boundary tokens)
    readonly property color recordingRegionGradientStart: PrimitiveTokens.boundaryBlue
    readonly property color recordingRegionGradientMid: PrimitiveTokens.indigo500
    readonly property color recordingRegionGradientEnd: PrimitiveTokens.purple400
    // Selection glow
    readonly property color recordingRegionGlowColor: PrimitiveTokens.indigo500
    // Dragging dashed border
    readonly property color recordingRegionDashColor: PrimitiveTokens.boundaryBlue
    // Glass panel (always dark)
    readonly property color recordingRegionGlassBg: Qt.rgba(0, 0, 0, 0.85)
    readonly property color recordingRegionGlassBgTop: Qt.rgba(0, 0, 0, 0.92)
    readonly property color recordingRegionGlassHighlight: Qt.rgba(1, 1, 1, 0.06)
    readonly property color recordingRegionGlassBorder: Qt.rgba(1, 1, 1, 0.12)
    readonly property color recordingRegionText: PrimitiveTokens.white
    readonly property color recordingRegionHoverBg: Qt.rgba(1, 1, 1, 0.12)
    readonly property color recordingRegionIconNormal: "#CCCCCC"
    readonly property color recordingRegionIconCancel: "#FF453A"
    readonly property color recordingRegionIconRecord: PrimitiveTokens.annotationRed
    // Sizing
    readonly property int recordingRegionActionBarWidth: 80
    readonly property int recordingRegionActionBarHeight: PrimitiveTokens.spacing32
    readonly property int recordingRegionActionBarButtonSize: 28
    readonly property int recordingRegionActionBarButtonHeight: PrimitiveTokens.spacing24
    readonly property int recordingRegionActionBarRadius: PrimitiveTokens.radiusMedium + 2
    readonly property int recordingRegionGlassRadius: 6
    readonly property int recordingRegionDimensionRadius: PrimitiveTokens.radiusSmall
    // Typography
    readonly property int recordingRegionDimensionFontSize: PrimitiveTokens.fontSizeSmallBody
    readonly property int recordingRegionHelpFontSize: PrimitiveTokens.fontSizeBody

    // ========================================================================
    // Spinner
    // ========================================================================
    readonly property int spinnerDuration: 900
}
