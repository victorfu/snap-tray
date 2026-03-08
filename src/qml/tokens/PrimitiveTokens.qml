pragma Singleton
import QtQuick
import SnapTrayQml

/**
 * PrimitiveTokens: Raw design values with no semantic meaning.
 * Thin wrapper delegating to the C++ DesignSystem singleton.
 *
 * These values should NEVER be used directly in components.
 * Always go through SemanticTokens or ComponentTokens.
 */
QtObject {
    // ========================================================================
    // Brand colors
    // ========================================================================
    readonly property color primaryPurple: DesignSystem.primaryPurple

    // ========================================================================
    // Annotation palette (colorblind-friendly)
    // ========================================================================
    readonly property color annotationRed: DesignSystem.annotationRed
    readonly property color annotationBlue: DesignSystem.annotationBlue
    readonly property color annotationOrange: DesignSystem.annotationOrange
    readonly property color annotationGreen: DesignSystem.annotationGreen
    readonly property color annotationPurple: DesignSystem.annotationPurple
    readonly property color annotationYellow: DesignSystem.annotationYellow
    readonly property color annotationPink: DesignSystem.annotationPink
    readonly property color annotationBlack: DesignSystem.annotationBlack
    readonly property color annotationWhite: DesignSystem.annotationWhite

    // ========================================================================
    // Capture overlay colors (always dark, theme-independent)
    // ========================================================================
    readonly property color dimOverlay: DesignSystem.dimOverlay
    readonly property color crosshair: DesignSystem.crosshair
    readonly property color selectionBorder: DesignSystem.selectionBorder
    readonly property color dimensionLabel: DesignSystem.dimensionLabel

    // ========================================================================
    // Neutral palette
    // ========================================================================
    readonly property color white: DesignSystem.white
    readonly property color gray50: DesignSystem.gray50
    readonly property color gray100: DesignSystem.gray100
    readonly property color gray200: DesignSystem.gray200
    readonly property color gray300: DesignSystem.gray300
    readonly property color gray400: DesignSystem.gray400
    readonly property color gray500: DesignSystem.gray500
    readonly property color gray600: DesignSystem.gray600
    readonly property color gray700: DesignSystem.gray700
    readonly property color gray800: DesignSystem.gray800
    readonly property color gray850: DesignSystem.gray850
    readonly property color gray900: DesignSystem.gray900
    readonly property color gray950: DesignSystem.gray950
    readonly property color black: DesignSystem.black

    // Dark mode surface colors
    readonly property color darkSurface: DesignSystem.darkSurface
    readonly property color darkSurfaceElevated: DesignSystem.darkSurfaceElevated
    readonly property color darkSurfaceOverlay: DesignSystem.darkSurfaceOverlay

    // Light mode surface colors
    readonly property color lightSurface: DesignSystem.lightSurface
    readonly property color lightSurfaceElevated: DesignSystem.lightSurfaceElevated
    readonly property color lightSurfaceOverlay: DesignSystem.lightSurfaceOverlay

    // ========================================================================
    // Status colors
    // ========================================================================
    readonly property color green500: DesignSystem.green500
    readonly property color green400: DesignSystem.green400
    readonly property color amber500: DesignSystem.amber500
    readonly property color amber400: DesignSystem.amber400
    readonly property color red500: DesignSystem.red500
    readonly property color red400: DesignSystem.red400
    readonly property color blue500: DesignSystem.blue500
    readonly property color blue400: DesignSystem.blue400
    readonly property color indigo500: DesignSystem.indigo500
    readonly property color purple400: DesignSystem.purple400
    readonly property color amber600: DesignSystem.amber600

    // ========================================================================
    // Recording boundary gradient colors
    // ========================================================================
    readonly property color boundaryBlue: DesignSystem.boundaryBlue
    readonly property color boundaryPurple: DesignSystem.boundaryPurple

    // ========================================================================
    // Accent states
    // ========================================================================
    readonly property color accentDefault: DesignSystem.accentDefault
    readonly property color accentHover: DesignSystem.accentHover
    readonly property color accentPressed: DesignSystem.accentPressed

    // ========================================================================
    // Spacing scale (px)
    // ========================================================================
    readonly property int spacing2: DesignSystem.spacing2
    readonly property int spacing4: DesignSystem.spacing4
    readonly property int spacing8: DesignSystem.spacing8
    readonly property int spacing12: DesignSystem.spacing12
    readonly property int spacing16: DesignSystem.spacing16
    readonly property int spacing20: DesignSystem.spacing20
    readonly property int spacing24: DesignSystem.spacing24
    readonly property int spacing32: DesignSystem.spacing32
    readonly property int spacing40: DesignSystem.spacing40
    readonly property int spacing48: DesignSystem.spacing48

    // ========================================================================
    // Border radius
    // ========================================================================
    readonly property int radiusSmall: DesignSystem.radiusSmall
    readonly property int radiusMedium: DesignSystem.radiusMedium
    readonly property int radiusLarge: DesignSystem.radiusLarge
    readonly property int radiusXL: DesignSystem.radiusXL

    // ========================================================================
    // Font sizes (px)
    // ========================================================================
    readonly property int fontSizeDisplay: DesignSystem.fontSizeDisplay
    readonly property int fontSizeH1: DesignSystem.fontSizeH1
    readonly property int fontSizeH2: DesignSystem.fontSizeH2
    readonly property int fontSizeH3: DesignSystem.fontSizeH3
    readonly property int fontSizeBody: DesignSystem.fontSizeBody
    readonly property int fontSizeSmallBody: DesignSystem.fontSizeSmallBody
    readonly property int fontSizeCaption: DesignSystem.fontSizeCaption
    readonly property int fontSizeSmall: DesignSystem.fontSizeSmall

    // ========================================================================
    // Font weights
    // ========================================================================
    readonly property int fontWeightRegular: DesignSystem.fontWeightRegular
    readonly property int fontWeightMedium: DesignSystem.fontWeightMedium
    readonly property int fontWeightSemiBold: DesignSystem.fontWeightSemiBold
    readonly property int fontWeightBold: DesignSystem.fontWeightBold

    // ========================================================================
    // Letter spacing
    // ========================================================================
    readonly property real letterSpacingTight: DesignSystem.letterSpacingTight
    readonly property real letterSpacingWide: DesignSystem.letterSpacingWide

    // ========================================================================
    // Font families
    // ========================================================================
    readonly property string fontFamily: DesignSystem.fontFamily
    readonly property int fontBaseSize: DesignSystem.fontBaseSize

    // ========================================================================
    // Icon sizes
    // ========================================================================
    readonly property int iconSizeSmall: DesignSystem.iconSizeSmall
    readonly property int iconSizeMedium: DesignSystem.iconSizeMedium
    readonly property int iconSizeLarge: DesignSystem.iconSizeLarge

    // ========================================================================
    // Shadow definitions
    // ========================================================================
    readonly property color lightShadowSmall: DesignSystem.lightShadowSmall
    readonly property int lightShadowSmallBlur: DesignSystem.lightShadowSmallBlur
    readonly property int lightShadowSmallY: DesignSystem.lightShadowSmallY
    readonly property color lightShadowMedium: DesignSystem.lightShadowMedium
    readonly property int lightShadowMediumBlur: DesignSystem.lightShadowMediumBlur
    readonly property int lightShadowMediumY: DesignSystem.lightShadowMediumY
    readonly property color lightShadowLarge: DesignSystem.lightShadowLarge
    readonly property int lightShadowLargeBlur: DesignSystem.lightShadowLargeBlur
    readonly property int lightShadowLargeY: DesignSystem.lightShadowLargeY
    readonly property color darkShadowSmall: DesignSystem.darkShadowSmall
    readonly property int darkShadowSmallBlur: DesignSystem.darkShadowSmallBlur
    readonly property int darkShadowSmallY: DesignSystem.darkShadowSmallY
    readonly property color darkShadowMedium: DesignSystem.darkShadowMedium
    readonly property int darkShadowMediumBlur: DesignSystem.darkShadowMediumBlur
    readonly property int darkShadowMediumY: DesignSystem.darkShadowMediumY
    readonly property color darkShadowLarge: DesignSystem.darkShadowLarge
    readonly property int darkShadowLargeBlur: DesignSystem.darkShadowLargeBlur
    readonly property int darkShadowLargeY: DesignSystem.darkShadowLargeY

    // ========================================================================
    // Animation durations (ms)
    // ========================================================================
    readonly property int durationInstant: DesignSystem.durationInstant
    readonly property int durationFast: DesignSystem.durationFast
    readonly property int durationNormal: DesignSystem.durationNormal
    readonly property int durationSlow: DesignSystem.durationSlow
    readonly property int durationEmphasis: DesignSystem.durationEmphasis
    readonly property int durationBoundaryLoop: DesignSystem.durationBoundaryLoop
    readonly property int durationInteractionHover: DesignSystem.durationInteractionHover
    readonly property int durationInteractionPress: DesignSystem.durationInteractionPress
    readonly property int durationInteractionState: DesignSystem.durationInteractionState
    readonly property int durationInteractionMax: DesignSystem.durationInteractionMax

    // ========================================================================
    // Icon system (Lucide conventions)
    // ========================================================================
    readonly property int iconStrokeWidth: DesignSystem.iconStrokeWidth
    readonly property int iconGridSize: DesignSystem.iconGridSize
    readonly property color iconColorLight: DesignSystem.iconColorLight
    readonly property color iconColorDark: DesignSystem.iconColorDark
}
