pragma Singleton
import QtQuick

/**
 * SemanticTokens: Maps PrimitiveTokens to purpose, switching by theme.
 *
 * References PrimitiveTokens for all raw values and binds to
 * ThemeManager.isDarkMode for live light/dark switching.
 */
QtObject {
    // Theme state - bound to ThemeManager singleton for live theme switching
    readonly property bool isDarkMode: ThemeManager ? ThemeManager.isDarkMode : false

    // ========================================================================
    // Background
    // ========================================================================
    readonly property color backgroundPrimary: isDarkMode
        ? PrimitiveTokens.darkSurface : PrimitiveTokens.lightSurface
    readonly property color backgroundElevated: isDarkMode
        ? PrimitiveTokens.darkSurfaceElevated : PrimitiveTokens.lightSurfaceElevated
    readonly property color backgroundOverlay: isDarkMode
        ? PrimitiveTokens.darkSurfaceOverlay : PrimitiveTokens.lightSurfaceOverlay

    // ========================================================================
    // Text
    // ========================================================================
    readonly property color textPrimary: isDarkMode
        ? PrimitiveTokens.gray200 : PrimitiveTokens.gray900
    readonly property color textSecondary: isDarkMode
        ? PrimitiveTokens.gray500 : PrimitiveTokens.gray600
    readonly property color textTertiary: isDarkMode
        ? PrimitiveTokens.gray600 : PrimitiveTokens.gray500
    readonly property color textInverse: isDarkMode
        ? PrimitiveTokens.gray900 : PrimitiveTokens.white

    // ========================================================================
    // Border
    // ========================================================================
    readonly property color borderDefault: isDarkMode
        ? PrimitiveTokens.gray800 : PrimitiveTokens.gray300
    readonly property color borderFocus: isDarkMode
        ? PrimitiveTokens.blue400 : PrimitiveTokens.blue500
    readonly property color borderActive: PrimitiveTokens.accentDefault  // theme-independent

    // ========================================================================
    // Accent
    // ========================================================================
    readonly property color accentDefault: PrimitiveTokens.accentDefault
    readonly property color accentHover: PrimitiveTokens.accentHover
    readonly property color accentPressed: PrimitiveTokens.accentPressed

    // ========================================================================
    // Status
    // ========================================================================
    readonly property color statusSuccess: isDarkMode
        ? PrimitiveTokens.green400 : PrimitiveTokens.green500
    readonly property color statusWarning: isDarkMode
        ? PrimitiveTokens.amber400 : PrimitiveTokens.amber500
    readonly property color statusError: isDarkMode
        ? PrimitiveTokens.red400 : PrimitiveTokens.red500
    readonly property color statusInfo: isDarkMode
        ? PrimitiveTokens.blue400 : PrimitiveTokens.blue500

    // ========================================================================
    // Toolbar
    // ========================================================================
    readonly property color toolbarBackground: isDarkMode
        ? Qt.rgba(PrimitiveTokens.gray850.r, PrimitiveTokens.gray850.g,
                  PrimitiveTokens.gray850.b, 0.85)
        : Qt.rgba(PrimitiveTokens.white.r, PrimitiveTokens.white.g,
                  PrimitiveTokens.white.b, 0.90)
    readonly property color toolbarIcon: isDarkMode
        ? PrimitiveTokens.gray300 : PrimitiveTokens.gray700
    readonly property color toolbarIconActive: PrimitiveTokens.white  // always white on accent bg
    readonly property color toolbarSeparator: isDarkMode
        ? PrimitiveTokens.gray800 : PrimitiveTokens.gray300

    // ========================================================================
    // Shadows
    // ========================================================================
    readonly property color shadowSmallColor: isDarkMode
        ? PrimitiveTokens.darkShadowSmall : PrimitiveTokens.lightShadowSmall
    readonly property int shadowSmallBlur: isDarkMode
        ? PrimitiveTokens.darkShadowSmallBlur : PrimitiveTokens.lightShadowSmallBlur
    readonly property int shadowSmallY: isDarkMode
        ? PrimitiveTokens.darkShadowSmallY : PrimitiveTokens.lightShadowSmallY

    readonly property color shadowMediumColor: isDarkMode
        ? PrimitiveTokens.darkShadowMedium : PrimitiveTokens.lightShadowMedium
    readonly property int shadowMediumBlur: isDarkMode
        ? PrimitiveTokens.darkShadowMediumBlur : PrimitiveTokens.lightShadowMediumBlur
    readonly property int shadowMediumY: isDarkMode
        ? PrimitiveTokens.darkShadowMediumY : PrimitiveTokens.lightShadowMediumY

    readonly property color shadowLargeColor: isDarkMode
        ? PrimitiveTokens.darkShadowLarge : PrimitiveTokens.lightShadowLarge
    readonly property int shadowLargeBlur: isDarkMode
        ? PrimitiveTokens.darkShadowLargeBlur : PrimitiveTokens.lightShadowLargeBlur
    readonly property int shadowLargeY: isDarkMode
        ? PrimitiveTokens.darkShadowLargeY : PrimitiveTokens.lightShadowLargeY

    // ========================================================================
    // Capture overlay (always dark, theme-independent)
    // ========================================================================
    readonly property color captureOverlayDim: PrimitiveTokens.dimOverlay
    readonly property color captureOverlayCrosshair: PrimitiveTokens.crosshair
    readonly property color captureOverlaySelectionBorder: PrimitiveTokens.selectionBorder
    readonly property color captureOverlayDimensionLabel: PrimitiveTokens.dimensionLabel

    // ========================================================================
    // Icon overlay color (for ColorOverlay on SVG icons)
    // ========================================================================
    readonly property color iconColor: isDarkMode
        ? PrimitiveTokens.iconColorDark : PrimitiveTokens.iconColorLight
}
