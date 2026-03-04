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
    readonly property color tooltipBorder: SemanticTokens.isDarkMode
        ? PrimitiveTokens.gray800 : PrimitiveTokens.gray300
    readonly property color tooltipText: SemanticTokens.textPrimary
    readonly property int tooltipRadius: PrimitiveTokens.radiusSmall
    readonly property int tooltipPadding: PrimitiveTokens.spacing8

    // ========================================================================
    // Toast
    // ========================================================================
    readonly property color toastBackground: SemanticTokens.isDarkMode
        ? Qt.rgba(0.12, 0.12, 0.12, 0.90) : Qt.rgba(1, 1, 1, 0.92)
    readonly property color toastBorder: SemanticTokens.isDarkMode
        ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.08)
    readonly property color toastTitleText: SemanticTokens.isDarkMode
        ? PrimitiveTokens.gray100 : PrimitiveTokens.gray900
    readonly property color toastMessageText: SemanticTokens.isDarkMode
        ? PrimitiveTokens.gray400 : PrimitiveTokens.gray600
    readonly property int toastRadius: PrimitiveTokens.radiusLarge
    readonly property int toastPadding: PrimitiveTokens.spacing16
    readonly property int toastShowDuration: 180
    readonly property int toastHideDuration: PrimitiveTokens.durationNormal
    readonly property int toastDisplayDuration: 2500

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
    readonly property color badgeBorder: toastBorder  // reuse toast border for consistency
    readonly property color badgeText: SemanticTokens.isDarkMode
        ? PrimitiveTokens.white : Qt.rgba(0.16, 0.16, 0.16, 1.0)
    readonly property int badgeRadius: 6
    readonly property int badgePaddingH: 10
    readonly property int badgePaddingV: 5
    readonly property int badgeFontSize: 12
    readonly property int badgeFadeInDuration: 150
    readonly property int badgeFadeOutDuration: PrimitiveTokens.durationNormal
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
    readonly property int settingsContentPadding: 24

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
    // Icon sizes (component-level aliases with semantic names)
    // ========================================================================
    readonly property int iconSizeToolbar: PrimitiveTokens.iconSizeMedium
    readonly property int iconSizeMenu: PrimitiveTokens.iconSizeSmall
    readonly property int iconSizeAction: PrimitiveTokens.iconSizeLarge
}
