pragma Singleton
import QtQuick
import SnapTrayQml

/**
 * ComponentTokens: UI component-specific design values.
 * Thin wrapper delegating to the C++ DesignSystem singleton.
 */
QtObject {
    // ========================================================================
    // Button: Primary
    // ========================================================================
    readonly property color buttonPrimaryBackground: DesignSystem.buttonPrimaryBackground
    readonly property color buttonPrimaryBackgroundHover: DesignSystem.buttonPrimaryBackgroundHover
    readonly property color buttonPrimaryBackgroundPressed: DesignSystem.buttonPrimaryBackgroundPressed
    readonly property color buttonPrimaryText: DesignSystem.buttonPrimaryText
    readonly property int buttonPrimaryRadius: DesignSystem.buttonPrimaryRadius

    // ========================================================================
    // Button: Secondary
    // ========================================================================
    readonly property color buttonSecondaryBackground: DesignSystem.buttonSecondaryBackground
    readonly property color buttonSecondaryBackgroundHover: DesignSystem.buttonSecondaryBackgroundHover
    readonly property color buttonSecondaryBackgroundPressed: DesignSystem.buttonSecondaryBackgroundPressed
    readonly property color buttonSecondaryText: DesignSystem.buttonSecondaryText
    readonly property color buttonSecondaryBorder: DesignSystem.buttonSecondaryBorder
    readonly property int buttonSecondaryRadius: DesignSystem.buttonSecondaryRadius

    // ========================================================================
    // Button: Ghost
    // ========================================================================
    readonly property color buttonGhostBackground: DesignSystem.buttonGhostBackground
    readonly property color buttonGhostBackgroundHover: DesignSystem.buttonGhostBackgroundHover
    readonly property color buttonGhostBackgroundPressed: DesignSystem.buttonGhostBackgroundPressed
    readonly property color buttonGhostText: DesignSystem.buttonGhostText
    readonly property int buttonGhostRadius: DesignSystem.buttonGhostRadius

    // ========================================================================
    // Button: Disabled
    // ========================================================================
    readonly property color buttonDisabledBackground: DesignSystem.buttonDisabledBackground
    readonly property color buttonDisabledText: DesignSystem.buttonDisabledText
    readonly property color buttonDisabledBorder: DesignSystem.buttonDisabledBorder

    // ========================================================================
    // Toolbar
    // ========================================================================
    readonly property int toolbarHeight: DesignSystem.toolbarHeight
    readonly property int toolbarIconSize: DesignSystem.toolbarIconSize
    readonly property int toolbarPadding: DesignSystem.toolbarPadding
    readonly property int toolbarRadius: DesignSystem.toolbarRadius
    readonly property color toolbarControlBackground: DesignSystem.toolbarControlBackground
    readonly property color toolbarControlBackgroundHover: DesignSystem.toolbarControlBackgroundHover
    readonly property color toolbarControlBackgroundPressed: DesignSystem.toolbarControlBackgroundPressed
    readonly property color toolbarBackground: DesignSystem.toolbarBackground
    readonly property color toolbarSeparator: DesignSystem.toolbarSeparator
    readonly property color toolbarIcon: DesignSystem.toolbarIcon
    readonly property color toolbarIconActive: DesignSystem.toolbarIconActive
    readonly property color toolbarPopupBackground: DesignSystem.toolbarPopupBackground
    readonly property color toolbarPopupBackgroundTop: DesignSystem.toolbarPopupBackgroundTop
    readonly property color toolbarPopupHighlight: DesignSystem.toolbarPopupHighlight
    readonly property color toolbarPopupBorder: DesignSystem.toolbarPopupBorder
    readonly property color toolbarPopupHover: DesignSystem.toolbarPopupHover
    readonly property color toolbarPopupSelected: DesignSystem.toolbarPopupSelected
    readonly property color toolbarPopupSelectedBorder: DesignSystem.toolbarPopupSelectedBorder

    // ========================================================================
    // Tooltip
    // ========================================================================
    readonly property color tooltipBackground: DesignSystem.tooltipBackground
    readonly property color tooltipBackgroundTop: DesignSystem.tooltipBackgroundTop
    readonly property color tooltipBorder: DesignSystem.tooltipBorder
    readonly property color tooltipHighlight: DesignSystem.tooltipHighlight
    readonly property color tooltipText: DesignSystem.tooltipText
    readonly property int tooltipRadius: DesignSystem.tooltipRadius
    readonly property int tooltipPadding: DesignSystem.tooltipPadding

    // ========================================================================
    // Toast
    // ========================================================================
    readonly property color toastBackground: DesignSystem.toastBackground
    readonly property color toastBorder: DesignSystem.toastBorder
    readonly property color toastTitleText: DesignSystem.toastTitleText
    readonly property color toastMessageText: DesignSystem.toastMessageText
    readonly property int toastRadius: DesignSystem.toastRadius
    readonly property int toastPadding: DesignSystem.toastPadding
    readonly property int toastShowDuration: DesignSystem.toastShowDuration
    readonly property int toastHideDuration: DesignSystem.toastHideDuration
    readonly property int toastDisplayDuration: DesignSystem.toastDisplayDuration
    readonly property int toastPaddingH: DesignSystem.toastPaddingH
    readonly property int toastPaddingV: DesignSystem.toastPaddingV
    readonly property int toastDotSize: DesignSystem.toastDotSize
    readonly property int toastDotLeftMargin: DesignSystem.toastDotLeftMargin
    readonly property int toastIconTextGap: DesignSystem.toastIconTextGap
    readonly property int toastScreenWidth: DesignSystem.toastScreenWidth
    readonly property int toastTitleFontSize: DesignSystem.toastTitleFontSize
    readonly property int toastMessageFontSize: DesignSystem.toastMessageFontSize
    readonly property int toastTitleMessageGap: DesignSystem.toastTitleMessageGap

    // ========================================================================
    // Panel
    // ========================================================================
    readonly property color panelBackground: DesignSystem.panelBackground
    readonly property color panelBorder: DesignSystem.panelBorder
    readonly property int panelRadius: DesignSystem.panelRadius
    readonly property int panelPadding: DesignSystem.panelPadding

    // ========================================================================
    // Dialog
    // ========================================================================
    readonly property color dialogBackground: DesignSystem.dialogBackground
    readonly property color dialogBorder: DesignSystem.dialogBorder
    readonly property color dialogOverlay: DesignSystem.dialogOverlay
    readonly property int dialogRadius: DesignSystem.dialogRadius
    readonly property int dialogPadding: DesignSystem.dialogPadding
    readonly property int dialogTitleBarHeight: DesignSystem.dialogTitleBarHeight
    readonly property int dialogButtonBarHeight: DesignSystem.dialogButtonBarHeight

    // ========================================================================
    // Badge
    // ========================================================================
    readonly property color badgeBackground: DesignSystem.badgeBackground
    readonly property color badgeHighlight: DesignSystem.badgeHighlight
    readonly property color badgeBorder: DesignSystem.badgeBorder
    readonly property color badgeText: DesignSystem.badgeText
    readonly property int badgeRadius: DesignSystem.badgeRadius
    readonly property int badgePaddingH: DesignSystem.badgePaddingH
    readonly property int badgePaddingV: DesignSystem.badgePaddingV
    readonly property int badgeFontSize: DesignSystem.badgeFontSize
    readonly property int badgeFadeInDuration: DesignSystem.badgeFadeInDuration
    readonly property int badgeFadeOutDuration: DesignSystem.badgeFadeOutDuration
    readonly property int badgeDisplayDuration: DesignSystem.badgeDisplayDuration

    // ========================================================================
    // Settings
    // ========================================================================
    readonly property color settingsSidebarBg: DesignSystem.settingsSidebarBg
    readonly property color settingsSidebarActiveItem: DesignSystem.settingsSidebarActiveItem
    readonly property color settingsSidebarHoverItem: DesignSystem.settingsSidebarHoverItem
    readonly property color settingsSectionText: DesignSystem.settingsSectionText
    readonly property int settingsSidebarWidth: DesignSystem.settingsSidebarWidth
    readonly property int settingsSidebarMinWidth: DesignSystem.settingsSidebarMinWidth
    readonly property int settingsSidebarMaxWidth: DesignSystem.settingsSidebarMaxWidth
    readonly property int settingsContentPadding: DesignSystem.settingsContentPadding
    readonly property int settingsSectionTopPadding: DesignSystem.settingsSectionTopPadding
    readonly property int settingsColumnSpacing: DesignSystem.settingsColumnSpacing
    readonly property int settingsBottomBarHeight: DesignSystem.settingsBottomBarHeight
    readonly property int settingsBottomBarPaddingH: DesignSystem.settingsBottomBarPaddingH
    readonly property int settingsToastTopMargin: DesignSystem.settingsToastTopMargin

    // Form Controls
    readonly property color toggleTrackOn: DesignSystem.toggleTrackOn
    readonly property color toggleTrackOff: DesignSystem.toggleTrackOff
    readonly property color toggleKnob: DesignSystem.toggleKnob
    readonly property int toggleWidth: DesignSystem.toggleWidth
    readonly property int toggleHeight: DesignSystem.toggleHeight
    readonly property int toggleKnobSize: DesignSystem.toggleKnobSize
    readonly property int toggleKnobRadius: DesignSystem.toggleKnobRadius
    readonly property int toggleKnobInset: DesignSystem.toggleKnobInset
    readonly property int toggleLabelIndent: DesignSystem.toggleLabelIndent

    readonly property color inputBackground: DesignSystem.inputBackground
    readonly property color inputBorder: DesignSystem.inputBorder
    readonly property color inputBorderFocus: DesignSystem.inputBorderFocus
    readonly property int inputRadius: DesignSystem.inputRadius
    readonly property int inputHeight: DesignSystem.inputHeight

    readonly property color sliderTrack: DesignSystem.sliderTrack
    readonly property color sliderFill: DesignSystem.sliderFill
    readonly property color sliderKnob: DesignSystem.sliderKnob
    readonly property int sliderHeight: DesignSystem.sliderHeight
    readonly property int sliderKnobSize: DesignSystem.sliderKnobSize

    // ========================================================================
    // Watermark Settings
    // ========================================================================
    readonly property int watermarkPreviewColumnWidth: DesignSystem.watermarkPreviewColumnWidth
    readonly property int watermarkPreviewSize: DesignSystem.watermarkPreviewSize

    // ========================================================================
    // Combo Popup
    // ========================================================================
    readonly property int comboPopupMaxHeight: DesignSystem.comboPopupMaxHeight

    // ========================================================================
    // Info Panel
    // ========================================================================
    readonly property color infoPanelBg: DesignSystem.infoPanelBg
    readonly property color infoPanelBorder: DesignSystem.infoPanelBorder
    readonly property color infoPanelText: DesignSystem.infoPanelText

    // ========================================================================
    // Focus Ring
    // ========================================================================
    readonly property color focusRingColor: DesignSystem.focusRingColor
    readonly property int focusRingWidth: DesignSystem.focusRingWidth
    readonly property int focusRingOffset: DesignSystem.focusRingOffset
    readonly property int focusRingRadius: DesignSystem.focusRingRadius

    // ========================================================================
    // Recording Preview
    // ========================================================================
    readonly property color recordingPreviewBgStart: DesignSystem.recordingPreviewBgStart
    readonly property color recordingPreviewBgEnd: DesignSystem.recordingPreviewBgEnd
    readonly property color recordingPreviewPanel: DesignSystem.recordingPreviewPanel
    readonly property color recordingPreviewPanelHover: DesignSystem.recordingPreviewPanelHover
    readonly property color recordingPreviewPanelPressed: DesignSystem.recordingPreviewPanelPressed
    readonly property color recordingPreviewBorder: DesignSystem.recordingPreviewBorder
    readonly property color recordingPreviewPlayOverlay: DesignSystem.recordingPreviewPlayOverlay
    readonly property color recordingPreviewTrimOverlay: DesignSystem.recordingPreviewTrimOverlay
    readonly property color recordingPreviewPlayhead: DesignSystem.recordingPreviewPlayhead
    readonly property color recordingPreviewPlayheadBorder: DesignSystem.recordingPreviewPlayheadBorder
    readonly property color recordingPreviewControlHighlight: DesignSystem.recordingPreviewControlHighlight
    readonly property color recordingPreviewDangerHover: DesignSystem.recordingPreviewDangerHover
    readonly property color recordingPreviewDangerPressed: DesignSystem.recordingPreviewDangerPressed
    readonly property color recordingPreviewPrimaryButton: DesignSystem.recordingPreviewPrimaryButton
    readonly property color recordingPreviewPrimaryButtonHover: DesignSystem.recordingPreviewPrimaryButtonHover
    readonly property color recordingPreviewPrimaryButtonPressed: DesignSystem.recordingPreviewPrimaryButtonPressed
    readonly property color recordingPreviewPrimaryButtonIcon: DesignSystem.recordingPreviewPrimaryButtonIcon
    readonly property int recordingPreviewControlButtonSize: DesignSystem.recordingPreviewControlButtonSize
    readonly property int recordingPreviewControlButtonHeight: DesignSystem.recordingPreviewControlButtonHeight
    readonly property int recordingPreviewIconSize: DesignSystem.recordingPreviewIconSize
    readonly property int recordingPreviewActionIconSize: DesignSystem.recordingPreviewActionIconSize

    // ========================================================================
    // Recording Boundary
    // ========================================================================
    readonly property color recordingBoundaryGradientStart: DesignSystem.recordingBoundaryGradientStart
    readonly property color recordingBoundaryGradientMid1: DesignSystem.recordingBoundaryGradientMid1
    readonly property color recordingBoundaryGradientMid2: DesignSystem.recordingBoundaryGradientMid2
    readonly property color recordingBoundaryGradientEnd: DesignSystem.recordingBoundaryGradientEnd
    readonly property color recordingBoundaryPaused: DesignSystem.recordingBoundaryPaused
    readonly property int recordingBoundaryLoopDuration: DesignSystem.recordingBoundaryLoopDuration

    // ========================================================================
    // Countdown Overlay
    // ========================================================================
    readonly property color countdownOverlay: DesignSystem.countdownOverlay
    readonly property color countdownText: DesignSystem.countdownText
    readonly property int countdownFontSize: DesignSystem.countdownFontSize
    readonly property int countdownScaleDuration: DesignSystem.countdownScaleDuration
    readonly property int countdownFadeDuration: DesignSystem.countdownFadeDuration

    // ========================================================================
    // Icon sizes
    // ========================================================================
    readonly property int iconSizeToolbar: DesignSystem.iconSizeToolbar
    readonly property int iconSizeMenu: DesignSystem.iconSizeMenu
    readonly property int iconSizeAction: DesignSystem.iconSizeAction

    // ========================================================================
    // Recording indicator
    // ========================================================================
    readonly property int recordingIndicatorDuration: DesignSystem.recordingIndicatorDuration
    readonly property color recordingAudioActive: DesignSystem.recordingAudioActive

    // ========================================================================
    // Recording Control Bar (always dark, theme-independent)
    // ========================================================================
    readonly property color recordingControlBarBg: DesignSystem.recordingControlBarBg
    readonly property color recordingControlBarBgTop: DesignSystem.recordingControlBarBgTop
    readonly property color recordingControlBarHighlight: DesignSystem.recordingControlBarHighlight
    readonly property color recordingControlBarBorder: DesignSystem.recordingControlBarBorder
    readonly property color recordingControlBarText: DesignSystem.recordingControlBarText
    readonly property color recordingControlBarSeparator: DesignSystem.recordingControlBarSeparator
    readonly property color recordingControlBarHoverBg: DesignSystem.recordingControlBarHoverBg
    readonly property color recordingControlBarIconNormal: DesignSystem.recordingControlBarIconNormal
    readonly property color recordingControlBarIconActive: DesignSystem.recordingControlBarIconActive
    readonly property color recordingControlBarIconRecord: DesignSystem.recordingControlBarIconRecord
    readonly property color recordingControlBarIconCancel: DesignSystem.recordingControlBarIconCancel
    readonly property int recordingControlBarRadius: DesignSystem.recordingControlBarRadius
    readonly property int recordingControlBarHeight: DesignSystem.recordingControlBarHeight
    readonly property int recordingControlBarButtonSize: DesignSystem.recordingControlBarButtonSize
    readonly property int recordingControlBarButtonSpacing: DesignSystem.recordingControlBarButtonSpacing
    readonly property int recordingControlBarMarginH: DesignSystem.recordingControlBarMarginH
    readonly property int recordingControlBarMarginV: DesignSystem.recordingControlBarMarginV
    readonly property int recordingControlBarItemSpacing: DesignSystem.recordingControlBarItemSpacing
    readonly property int recordingControlBarIndicatorSize: DesignSystem.recordingControlBarIndicatorSize
    readonly property int recordingControlBarFontSize: DesignSystem.recordingControlBarFontSize
    readonly property int recordingControlBarFontSizeSmall: DesignSystem.recordingControlBarFontSizeSmall
    readonly property int recordingControlBarIconSize: DesignSystem.recordingControlBarIconSize
    readonly property int recordingControlBarButtonRadius: DesignSystem.recordingControlBarButtonRadius

    // ========================================================================
    // Recording Region Selector (always dark, theme-independent)
    // ========================================================================
    readonly property color recordingRegionOverlayDim: DesignSystem.recordingRegionOverlayDim
    readonly property color recordingRegionCrosshair: DesignSystem.recordingRegionCrosshair
    readonly property color recordingRegionGradientStart: DesignSystem.recordingRegionGradientStart
    readonly property color recordingRegionGradientMid: DesignSystem.recordingRegionGradientMid
    readonly property color recordingRegionGradientEnd: DesignSystem.recordingRegionGradientEnd
    readonly property color recordingRegionGlowColor: DesignSystem.recordingRegionGlowColor
    readonly property color recordingRegionDashColor: DesignSystem.recordingRegionDashColor
    readonly property color recordingRegionGlassBg: DesignSystem.recordingRegionGlassBg
    readonly property color recordingRegionGlassBgTop: DesignSystem.recordingRegionGlassBgTop
    readonly property color recordingRegionGlassHighlight: DesignSystem.recordingRegionGlassHighlight
    readonly property color recordingRegionGlassBorder: DesignSystem.recordingRegionGlassBorder
    readonly property color recordingRegionText: DesignSystem.recordingRegionText
    readonly property color recordingRegionHoverBg: DesignSystem.recordingRegionHoverBg
    readonly property color recordingRegionIconNormal: DesignSystem.recordingRegionIconNormal
    readonly property color recordingRegionIconCancel: DesignSystem.recordingRegionIconCancel
    readonly property color recordingRegionIconRecord: DesignSystem.recordingRegionIconRecord
    readonly property int recordingRegionActionBarWidth: DesignSystem.recordingRegionActionBarWidth
    readonly property int recordingRegionActionBarHeight: DesignSystem.recordingRegionActionBarHeight
    readonly property int recordingRegionActionBarButtonSize: DesignSystem.recordingRegionActionBarButtonSize
    readonly property int recordingRegionActionBarButtonHeight: DesignSystem.recordingRegionActionBarButtonHeight
    readonly property int recordingRegionActionBarRadius: DesignSystem.recordingRegionActionBarRadius
    readonly property int recordingRegionGlassRadius: DesignSystem.recordingRegionGlassRadius
    readonly property int recordingRegionDimensionRadius: DesignSystem.recordingRegionDimensionRadius
    readonly property int recordingRegionDimensionFontSize: DesignSystem.recordingRegionDimensionFontSize
    readonly property int recordingRegionHelpFontSize: DesignSystem.recordingRegionHelpFontSize

    // ========================================================================
    // Spinner
    // ========================================================================
    readonly property int spinnerDuration: DesignSystem.spinnerDuration

    // ========================================================================
    // Capture Overlay Panel (always dark, theme-independent)
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
}
