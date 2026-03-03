pragma Singleton
import QtQuick

/**
 * PrimitiveTokens: Raw design values with no semantic meaning.
 * Colors, sizes, font weights, spacing, and animation durations.
 *
 * These values should NEVER be used directly in components.
 * Always go through SemanticTokens or ComponentTokens.
 */
QtObject {
    // ========================================================================
    // Brand colors
    // ========================================================================
    readonly property color primaryPurple: "#6C5CE7"

    // ========================================================================
    // Annotation palette (colorblind-friendly)
    // ========================================================================
    readonly property color annotationRed: "#FF3B30"
    readonly property color annotationBlue: "#007AFF"
    readonly property color annotationOrange: "#FF9500"
    readonly property color annotationGreen: "#34C759"
    readonly property color annotationPurple: "#AF52DE"
    readonly property color annotationYellow: "#FFCC00"
    readonly property color annotationPink: "#FF2D55"
    readonly property color annotationBlack: "#000000"
    readonly property color annotationWhite: "#FFFFFF"

    // ========================================================================
    // Capture overlay colors (always dark, theme-independent)
    // ========================================================================
    readonly property color dimOverlay: Qt.rgba(0, 0, 0, 0.45)
    readonly property color crosshair: "#00FF00"
    readonly property color selectionBorder: "#FFFFFF"
    readonly property color dimensionLabel: Qt.rgba(0, 0, 0, 0.75)

    // ========================================================================
    // Neutral palette
    // ========================================================================
    readonly property color white: "#FFFFFF"
    readonly property color gray50: "#FAFAFA"
    readonly property color gray100: "#F5F5F5"
    readonly property color gray200: "#EEEEEE"
    readonly property color gray300: "#E0E0E0"
    readonly property color gray400: "#BDBDBD"
    readonly property color gray500: "#9E9E9E"
    readonly property color gray600: "#757575"
    readonly property color gray700: "#616161"
    readonly property color gray800: "#424242"
    readonly property color gray850: "#303030"
    readonly property color gray900: "#212121"
    readonly property color gray950: "#1A1A1A"
    readonly property color black: "#000000"

    // Dark mode surface colors
    readonly property color darkSurface: "#1E1E1E"
    readonly property color darkSurfaceElevated: "#2D2D2D"
    readonly property color darkSurfaceOverlay: Qt.rgba(0, 0, 0, 0.6)

    // Light mode surface colors
    readonly property color lightSurface: "#FFFFFF"
    readonly property color lightSurfaceElevated: "#F8F8F8"
    readonly property color lightSurfaceOverlay: Qt.rgba(0, 0, 0, 0.3)

    // ========================================================================
    // Status colors
    // ========================================================================
    readonly property color green500: "#34C759"
    readonly property color green400: "#52D977"
    readonly property color amber500: "#F59E0B"
    readonly property color amber400: "#FBBF24"
    readonly property color red500: "#EF4444"
    readonly property color red400: "#F87171"
    readonly property color blue500: "#3B82F6"
    readonly property color blue400: "#60A5FA"

    // ========================================================================
    // Accent states
    // ========================================================================
    readonly property color accentDefault: "#6C5CE7"
    readonly property color accentHover: "#5A4BD6"
    readonly property color accentPressed: "#4A3BC5"

    // ========================================================================
    // Spacing scale (px)
    // ========================================================================
    readonly property int spacing2: 2
    readonly property int spacing4: 4
    readonly property int spacing8: 8
    readonly property int spacing12: 12
    readonly property int spacing16: 16
    readonly property int spacing20: 20
    readonly property int spacing24: 24
    readonly property int spacing32: 32
    readonly property int spacing40: 40
    readonly property int spacing48: 48

    // ========================================================================
    // Border radius
    // ========================================================================
    readonly property int radiusSmall: 4
    readonly property int radiusMedium: 8
    readonly property int radiusLarge: 12
    readonly property int radiusXL: 16

    // ========================================================================
    // Font sizes (px) - use pixelSize for cross-platform consistency
    // ========================================================================
    readonly property int fontSizeDisplay: 28
    readonly property int fontSizeH1: 22
    readonly property int fontSizeH2: 18
    readonly property int fontSizeH3: 15
    readonly property int fontSizeBody: 13
    readonly property int fontSizeCaption: 11
    readonly property int fontSizeSmall: 10

    // ========================================================================
    // Font weights
    // ========================================================================
    readonly property int fontWeightRegular: Font.Normal
    readonly property int fontWeightMedium: Font.Medium
    readonly property int fontWeightSemiBold: Font.DemiBold
    readonly property int fontWeightBold: Font.Bold

    // ========================================================================
    // Font families (platform-aware fallback)
    // ========================================================================
    readonly property string fontFamily: Qt.platform.os === "osx"
        ? "Inter, SF Pro, -apple-system, Helvetica Neue, sans-serif"
        : "Inter, Segoe UI, sans-serif"
    readonly property int fontBaseSize: 13

    // ========================================================================
    // Icon sizes
    // ========================================================================
    readonly property int iconSizeSmall: 16
    readonly property int iconSizeMedium: 20
    readonly property int iconSizeLarge: 24

    // ========================================================================
    // Shadow definitions
    // ========================================================================
    // Light mode shadows
    readonly property color lightShadowSmall: Qt.rgba(0, 0, 0, 0.08)
    readonly property int lightShadowSmallBlur: 4
    readonly property int lightShadowSmallY: 1

    readonly property color lightShadowMedium: Qt.rgba(0, 0, 0, 0.12)
    readonly property int lightShadowMediumBlur: 8
    readonly property int lightShadowMediumY: 2

    readonly property color lightShadowLarge: Qt.rgba(0, 0, 0, 0.16)
    readonly property int lightShadowLargeBlur: 16
    readonly property int lightShadowLargeY: 4

    // Dark mode shadows (reduced opacity ~30%)
    readonly property color darkShadowSmall: Qt.rgba(0, 0, 0, 0.24)
    readonly property int darkShadowSmallBlur: 4
    readonly property int darkShadowSmallY: 1

    readonly property color darkShadowMedium: Qt.rgba(0, 0, 0, 0.36)
    readonly property int darkShadowMediumBlur: 8
    readonly property int darkShadowMediumY: 2

    readonly property color darkShadowLarge: Qt.rgba(0, 0, 0, 0.48)
    readonly property int darkShadowLargeBlur: 16
    readonly property int darkShadowLargeY: 4

    // ========================================================================
    // Animation durations (ms)
    // ========================================================================
    readonly property int durationInstant: 0
    readonly property int durationFast: 100
    readonly property int durationNormal: 200
    readonly property int durationSlow: 300
    readonly property int durationEmphasis: 400

    // ========================================================================
    // Icon system (Lucide conventions)
    // ========================================================================
    readonly property int iconStrokeWidth: 2
    readonly property int iconGridSize: 24
    readonly property color iconColorLight: "#404040"
    readonly property color iconColorDark: "#E0E0EE"
}
