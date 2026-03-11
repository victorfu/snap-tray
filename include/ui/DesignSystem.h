#pragma once

#include <QColor>
#include <QObject>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

struct ToolbarStyleConfig;

/**
 * @brief Single source of truth for all design tokens.
 *
 * Consolidates primitive values (brand colors, spacing, radii, durations),
 * semantic mappings (theme-aware surfaces, text, borders), and component
 * tokens (toolbar, toast, badge, recording, settings) into one C++ singleton
 * exposed to both QML and C++.
 *
 * QML usage:  DesignSystem.primaryPurple
 * C++ usage:  DesignSystem::instance().primaryPurple()
 *
 * For backward compatibility, ToolbarStyleConfig and DesignTokens read
 * from this singleton so existing C++ call-sites continue to work.
 */
class DesignSystem : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // ====================================================================
    // Theme state
    // ====================================================================
    Q_PROPERTY(bool isDarkMode READ isDarkMode NOTIFY themeChanged)

    // ====================================================================
    // Primitive: Brand
    // ====================================================================
    Q_PROPERTY(QColor primaryPurple READ primaryPurple CONSTANT)

    // ====================================================================
    // Primitive: Annotation palette
    // ====================================================================
    Q_PROPERTY(QColor annotationRed READ annotationRed CONSTANT)
    Q_PROPERTY(QColor annotationBlue READ annotationBlue CONSTANT)
    Q_PROPERTY(QColor annotationOrange READ annotationOrange CONSTANT)
    Q_PROPERTY(QColor annotationGreen READ annotationGreen CONSTANT)
    Q_PROPERTY(QColor annotationPurple READ annotationPurple CONSTANT)
    Q_PROPERTY(QColor annotationYellow READ annotationYellow CONSTANT)
    Q_PROPERTY(QColor annotationPink READ annotationPink CONSTANT)
    Q_PROPERTY(QColor annotationBlack READ annotationBlack CONSTANT)
    Q_PROPERTY(QColor annotationWhite READ annotationWhite CONSTANT)

    // ====================================================================
    // Primitive: Capture overlay (always dark)
    // ====================================================================
    Q_PROPERTY(QColor dimOverlay READ dimOverlay CONSTANT)
    Q_PROPERTY(QColor crosshair READ crosshair CONSTANT)
    Q_PROPERTY(QColor selectionBorder READ selectionBorder CONSTANT)
    Q_PROPERTY(QColor dimensionLabel READ dimensionLabel CONSTANT)

    // ====================================================================
    // Primitive: Neutral palette
    // ====================================================================
    Q_PROPERTY(QColor white READ white CONSTANT)
    Q_PROPERTY(QColor gray50 READ gray50 CONSTANT)
    Q_PROPERTY(QColor gray100 READ gray100 CONSTANT)
    Q_PROPERTY(QColor gray200 READ gray200 CONSTANT)
    Q_PROPERTY(QColor gray300 READ gray300 CONSTANT)
    Q_PROPERTY(QColor gray400 READ gray400 CONSTANT)
    Q_PROPERTY(QColor gray500 READ gray500 CONSTANT)
    Q_PROPERTY(QColor gray600 READ gray600 CONSTANT)
    Q_PROPERTY(QColor gray700 READ gray700 CONSTANT)
    Q_PROPERTY(QColor gray800 READ gray800 CONSTANT)
    Q_PROPERTY(QColor gray850 READ gray850 CONSTANT)
    Q_PROPERTY(QColor gray900 READ gray900 CONSTANT)
    Q_PROPERTY(QColor gray950 READ gray950 CONSTANT)
    Q_PROPERTY(QColor black READ black CONSTANT)

    // Dark/light surfaces
    Q_PROPERTY(QColor darkSurface READ darkSurface CONSTANT)
    Q_PROPERTY(QColor darkSurfaceElevated READ darkSurfaceElevated CONSTANT)
    Q_PROPERTY(QColor darkSurfaceOverlay READ darkSurfaceOverlay CONSTANT)
    Q_PROPERTY(QColor lightSurface READ lightSurface CONSTANT)
    Q_PROPERTY(QColor lightSurfaceElevated READ lightSurfaceElevated CONSTANT)
    Q_PROPERTY(QColor lightSurfaceOverlay READ lightSurfaceOverlay CONSTANT)

    // ====================================================================
    // Primitive: Status colors
    // ====================================================================
    Q_PROPERTY(QColor green500 READ green500 CONSTANT)
    Q_PROPERTY(QColor green400 READ green400 CONSTANT)
    Q_PROPERTY(QColor amber500 READ amber500 CONSTANT)
    Q_PROPERTY(QColor amber400 READ amber400 CONSTANT)
    Q_PROPERTY(QColor amber600 READ amber600 CONSTANT)
    Q_PROPERTY(QColor red500 READ red500 CONSTANT)
    Q_PROPERTY(QColor red400 READ red400 CONSTANT)
    Q_PROPERTY(QColor blue500 READ blue500 CONSTANT)
    Q_PROPERTY(QColor blue400 READ blue400 CONSTANT)
    Q_PROPERTY(QColor indigo500 READ indigo500 CONSTANT)
    Q_PROPERTY(QColor purple400 READ purple400 CONSTANT)

    // ====================================================================
    // Primitive: Recording boundary gradient
    // ====================================================================
    Q_PROPERTY(QColor boundaryBlue READ boundaryBlue CONSTANT)
    Q_PROPERTY(QColor boundaryPurple READ boundaryPurple CONSTANT)

    // ====================================================================
    // Primitive: Accent states
    // ====================================================================
    Q_PROPERTY(QColor accentDefault READ accentDefault CONSTANT)
    Q_PROPERTY(QColor accentHover READ accentHover CONSTANT)
    Q_PROPERTY(QColor accentPressed READ accentPressed CONSTANT)
    Q_PROPERTY(QColor accentLight READ accentLight CONSTANT)

    // ====================================================================
    // Primitive: Spacing
    // ====================================================================
    Q_PROPERTY(int spacing2 READ spacing2 CONSTANT)
    Q_PROPERTY(int spacing4 READ spacing4 CONSTANT)
    Q_PROPERTY(int spacing8 READ spacing8 CONSTANT)
    Q_PROPERTY(int spacing12 READ spacing12 CONSTANT)
    Q_PROPERTY(int spacing16 READ spacing16 CONSTANT)
    Q_PROPERTY(int spacing20 READ spacing20 CONSTANT)
    Q_PROPERTY(int spacing24 READ spacing24 CONSTANT)
    Q_PROPERTY(int spacing32 READ spacing32 CONSTANT)
    Q_PROPERTY(int spacing40 READ spacing40 CONSTANT)
    Q_PROPERTY(int spacing48 READ spacing48 CONSTANT)

    // ====================================================================
    // Primitive: Border radius
    // ====================================================================
    Q_PROPERTY(int radiusSmall READ radiusSmall CONSTANT)
    Q_PROPERTY(int radiusMedium READ radiusMedium CONSTANT)
    Q_PROPERTY(int radiusLarge READ radiusLarge CONSTANT)
    Q_PROPERTY(int radiusXL READ radiusXL CONSTANT)

    // ====================================================================
    // Primitive: Font sizes
    // ====================================================================
    Q_PROPERTY(int fontSizeDisplay READ fontSizeDisplay CONSTANT)
    Q_PROPERTY(int fontSizeH1 READ fontSizeH1 CONSTANT)
    Q_PROPERTY(int fontSizeH2 READ fontSizeH2 CONSTANT)
    Q_PROPERTY(int fontSizeH3 READ fontSizeH3 CONSTANT)
    Q_PROPERTY(int fontSizeBody READ fontSizeBody CONSTANT)
    Q_PROPERTY(int fontSizeSmallBody READ fontSizeSmallBody CONSTANT)
    Q_PROPERTY(int fontSizeCaption READ fontSizeCaption CONSTANT)
    Q_PROPERTY(int fontSizeSmall READ fontSizeSmall CONSTANT)

    // ====================================================================
    // Primitive: Font weights (Qt::Weight enum values)
    // ====================================================================
    Q_PROPERTY(int fontWeightRegular READ fontWeightRegular CONSTANT)
    Q_PROPERTY(int fontWeightMedium READ fontWeightMedium CONSTANT)
    Q_PROPERTY(int fontWeightSemiBold READ fontWeightSemiBold CONSTANT)
    Q_PROPERTY(int fontWeightBold READ fontWeightBold CONSTANT)

    // ====================================================================
    // Primitive: Letter spacing
    // ====================================================================
    Q_PROPERTY(qreal letterSpacingTight READ letterSpacingTight CONSTANT)
    Q_PROPERTY(qreal letterSpacingWide READ letterSpacingWide CONSTANT)

    // ====================================================================
    // Primitive: Font family
    // ====================================================================
    Q_PROPERTY(QString fontFamily READ fontFamily CONSTANT)
    Q_PROPERTY(int fontBaseSize READ fontBaseSize CONSTANT)

    // ====================================================================
    // Primitive: Icon sizes
    // ====================================================================
    Q_PROPERTY(int iconSizeSmall READ iconSizeSmall CONSTANT)
    Q_PROPERTY(int iconSizeMedium READ iconSizeMedium CONSTANT)
    Q_PROPERTY(int iconSizeLarge READ iconSizeLarge CONSTANT)

    // ====================================================================
    // Primitive: Shadows (light mode)
    // ====================================================================
    Q_PROPERTY(QColor lightShadowSmall READ lightShadowSmall CONSTANT)
    Q_PROPERTY(int lightShadowSmallBlur READ lightShadowSmallBlur CONSTANT)
    Q_PROPERTY(int lightShadowSmallY READ lightShadowSmallY CONSTANT)
    Q_PROPERTY(QColor lightShadowMedium READ lightShadowMedium CONSTANT)
    Q_PROPERTY(int lightShadowMediumBlur READ lightShadowMediumBlur CONSTANT)
    Q_PROPERTY(int lightShadowMediumY READ lightShadowMediumY CONSTANT)
    Q_PROPERTY(QColor lightShadowLarge READ lightShadowLarge CONSTANT)
    Q_PROPERTY(int lightShadowLargeBlur READ lightShadowLargeBlur CONSTANT)
    Q_PROPERTY(int lightShadowLargeY READ lightShadowLargeY CONSTANT)

    // ====================================================================
    // Primitive: Shadows (dark mode)
    // ====================================================================
    Q_PROPERTY(QColor darkShadowSmall READ darkShadowSmall CONSTANT)
    Q_PROPERTY(int darkShadowSmallBlur READ darkShadowSmallBlur CONSTANT)
    Q_PROPERTY(int darkShadowSmallY READ darkShadowSmallY CONSTANT)
    Q_PROPERTY(QColor darkShadowMedium READ darkShadowMedium CONSTANT)
    Q_PROPERTY(int darkShadowMediumBlur READ darkShadowMediumBlur CONSTANT)
    Q_PROPERTY(int darkShadowMediumY READ darkShadowMediumY CONSTANT)
    Q_PROPERTY(QColor darkShadowLarge READ darkShadowLarge CONSTANT)
    Q_PROPERTY(int darkShadowLargeBlur READ darkShadowLargeBlur CONSTANT)
    Q_PROPERTY(int darkShadowLargeY READ darkShadowLargeY CONSTANT)

    // ====================================================================
    // Primitive: Animation durations
    // ====================================================================
    Q_PROPERTY(int durationInstant READ durationInstant CONSTANT)
    Q_PROPERTY(int durationFast READ durationFast CONSTANT)
    Q_PROPERTY(int durationNormal READ durationNormal CONSTANT)
    Q_PROPERTY(int durationSlow READ durationSlow CONSTANT)
    Q_PROPERTY(int durationEmphasis READ durationEmphasis CONSTANT)
    Q_PROPERTY(int durationBoundaryLoop READ durationBoundaryLoop CONSTANT)
    Q_PROPERTY(int durationInteractionHover READ durationInteractionHover CONSTANT)
    Q_PROPERTY(int durationInteractionPress READ durationInteractionPress CONSTANT)
    Q_PROPERTY(int durationInteractionState READ durationInteractionState CONSTANT)
    Q_PROPERTY(int durationInteractionMax READ durationInteractionMax CONSTANT)

    // ====================================================================
    // Primitive: Icon system
    // ====================================================================
    Q_PROPERTY(int iconStrokeWidth READ iconStrokeWidth CONSTANT)
    Q_PROPERTY(int iconGridSize READ iconGridSize CONSTANT)
    Q_PROPERTY(QColor iconColorLight READ iconColorLight CONSTANT)
    Q_PROPERTY(QColor iconColorDark READ iconColorDark CONSTANT)

    // ====================================================================
    // Semantic: Background (theme-dependent)
    // ====================================================================
    Q_PROPERTY(QColor backgroundPrimary READ backgroundPrimary NOTIFY themeChanged)
    Q_PROPERTY(QColor backgroundElevated READ backgroundElevated NOTIFY themeChanged)
    Q_PROPERTY(QColor backgroundOverlay READ backgroundOverlay NOTIFY themeChanged)

    // ====================================================================
    // Semantic: Text (theme-dependent)
    // ====================================================================
    Q_PROPERTY(QColor textPrimary READ textPrimary NOTIFY themeChanged)
    Q_PROPERTY(QColor textSecondary READ textSecondary NOTIFY themeChanged)
    Q_PROPERTY(QColor textTertiary READ textTertiary NOTIFY themeChanged)
    Q_PROPERTY(QColor textInverse READ textInverse NOTIFY themeChanged)
    Q_PROPERTY(QColor textOnAccent READ textOnAccent CONSTANT)

    // ====================================================================
    // Semantic: Border (theme-dependent)
    // ====================================================================
    Q_PROPERTY(QColor borderDefault READ borderDefault NOTIFY themeChanged)
    Q_PROPERTY(QColor borderFocus READ borderFocus CONSTANT)
    Q_PROPERTY(QColor borderActive READ borderActive CONSTANT)

    // ====================================================================
    // Semantic: Status (theme-dependent)
    // ====================================================================
    Q_PROPERTY(QColor statusSuccess READ statusSuccess NOTIFY themeChanged)
    Q_PROPERTY(QColor statusWarning READ statusWarning NOTIFY themeChanged)
    Q_PROPERTY(QColor statusError READ statusError NOTIFY themeChanged)
    Q_PROPERTY(QColor statusInfo READ statusInfo NOTIFY themeChanged)

    // ====================================================================
    // Semantic: Toolbar (theme-dependent)
    // ====================================================================
    Q_PROPERTY(QColor toolbarBackground READ toolbarBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor toolbarIcon READ toolbarIcon NOTIFY themeChanged)
    Q_PROPERTY(QColor toolbarIconActive READ toolbarIconActive CONSTANT)
    Q_PROPERTY(QColor toolbarSeparator READ toolbarSeparator NOTIFY themeChanged)

    // ====================================================================
    // Semantic: Shadows (theme-dependent)
    // ====================================================================
    Q_PROPERTY(QColor shadowSmallColor READ shadowSmallColor NOTIFY themeChanged)
    Q_PROPERTY(int shadowSmallBlur READ shadowSmallBlur NOTIFY themeChanged)
    Q_PROPERTY(int shadowSmallY READ shadowSmallY NOTIFY themeChanged)
    Q_PROPERTY(QColor shadowMediumColor READ shadowMediumColor NOTIFY themeChanged)
    Q_PROPERTY(int shadowMediumBlur READ shadowMediumBlur NOTIFY themeChanged)
    Q_PROPERTY(int shadowMediumY READ shadowMediumY NOTIFY themeChanged)
    Q_PROPERTY(QColor shadowLargeColor READ shadowLargeColor NOTIFY themeChanged)
    Q_PROPERTY(int shadowLargeBlur READ shadowLargeBlur NOTIFY themeChanged)
    Q_PROPERTY(int shadowLargeY READ shadowLargeY NOTIFY themeChanged)

    // ====================================================================
    // Semantic: Capture overlay (always dark)
    // ====================================================================
    Q_PROPERTY(QColor captureOverlayDim READ captureOverlayDim CONSTANT)
    Q_PROPERTY(QColor captureOverlayCrosshair READ captureOverlayCrosshair CONSTANT)
    Q_PROPERTY(QColor captureOverlaySelectionBorder READ captureOverlaySelectionBorder CONSTANT)
    Q_PROPERTY(QColor captureOverlayDimensionLabel READ captureOverlayDimensionLabel CONSTANT)

    // Capture overlay panel (shortcut hints, region control — always dark)
    Q_PROPERTY(QColor captureOverlayPanelBg READ captureOverlayPanelBg CONSTANT)
    Q_PROPERTY(QColor captureOverlayPanelBgTop READ captureOverlayPanelBgTop CONSTANT)
    Q_PROPERTY(QColor captureOverlayPanelHighlight READ captureOverlayPanelHighlight CONSTANT)
    Q_PROPERTY(QColor captureOverlayPanelBorder READ captureOverlayPanelBorder CONSTANT)
    Q_PROPERTY(int captureOverlayPanelRadius READ captureOverlayPanelRadius CONSTANT)

    // Shortcut hints specific
    Q_PROPERTY(QColor captureOverlayKeycapBg READ captureOverlayKeycapBg CONSTANT)
    Q_PROPERTY(QColor captureOverlayKeycapBorder READ captureOverlayKeycapBorder CONSTANT)
    Q_PROPERTY(QColor captureOverlayKeycapText READ captureOverlayKeycapText CONSTANT)
    Q_PROPERTY(QColor captureOverlayHintText READ captureOverlayHintText CONSTANT)
    Q_PROPERTY(int captureOverlayKeycapRadius READ captureOverlayKeycapRadius CONSTANT)

    // Region control icon states
    Q_PROPERTY(QColor captureOverlayIconNormal READ captureOverlayIconNormal CONSTANT)
    Q_PROPERTY(QColor captureOverlayIconActive READ captureOverlayIconActive CONSTANT)
    Q_PROPERTY(QColor captureOverlayIconDimmed READ captureOverlayIconDimmed CONSTANT)
    Q_PROPERTY(QColor captureOverlayControlHoverBg READ captureOverlayControlHoverBg CONSTANT)

    // Multi-region list panel
    Q_PROPERTY(QColor captureOverlayListItemHover READ captureOverlayListItemHover CONSTANT)
    Q_PROPERTY(QColor captureOverlayListItemActive READ captureOverlayListItemActive CONSTANT)
    Q_PROPERTY(QColor captureOverlayListButtonBg READ captureOverlayListButtonBg CONSTANT)
    Q_PROPERTY(QColor captureOverlayListButtonHover READ captureOverlayListButtonHover CONSTANT)
    Q_PROPERTY(QColor captureOverlaySliderTrack READ captureOverlaySliderTrack CONSTANT)
    Q_PROPERTY(QColor captureOverlaySliderFill READ captureOverlaySliderFill CONSTANT)
    Q_PROPERTY(QColor captureOverlaySliderKnob READ captureOverlaySliderKnob CONSTANT)

    // ====================================================================
    // Semantic: Icon overlay (theme-dependent)
    // ====================================================================
    Q_PROPERTY(QColor iconColor READ iconColor NOTIFY themeChanged)

    // ====================================================================
    // Component: Button Primary
    // ====================================================================
    Q_PROPERTY(QColor buttonPrimaryBackground READ buttonPrimaryBackground CONSTANT)
    Q_PROPERTY(QColor buttonPrimaryBackgroundHover READ buttonPrimaryBackgroundHover CONSTANT)
    Q_PROPERTY(QColor buttonPrimaryBackgroundPressed READ buttonPrimaryBackgroundPressed CONSTANT)
    Q_PROPERTY(QColor buttonPrimaryText READ buttonPrimaryText CONSTANT)
    Q_PROPERTY(int buttonPrimaryRadius READ buttonPrimaryRadius CONSTANT)

    // ====================================================================
    // Component: Button Secondary (theme-dependent)
    // ====================================================================
    Q_PROPERTY(QColor buttonSecondaryBackground READ buttonSecondaryBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor buttonSecondaryBackgroundHover READ buttonSecondaryBackgroundHover NOTIFY themeChanged)
    Q_PROPERTY(QColor buttonSecondaryBackgroundPressed READ buttonSecondaryBackgroundPressed NOTIFY themeChanged)
    Q_PROPERTY(QColor buttonSecondaryText READ buttonSecondaryText NOTIFY themeChanged)
    Q_PROPERTY(QColor buttonSecondaryBorder READ buttonSecondaryBorder NOTIFY themeChanged)
    Q_PROPERTY(int buttonSecondaryRadius READ buttonSecondaryRadius CONSTANT)

    // ====================================================================
    // Component: Button Ghost (theme-dependent)
    // ====================================================================
    Q_PROPERTY(QColor buttonGhostBackground READ buttonGhostBackground CONSTANT)
    Q_PROPERTY(QColor buttonGhostBackgroundHover READ buttonGhostBackgroundHover NOTIFY themeChanged)
    Q_PROPERTY(QColor buttonGhostBackgroundPressed READ buttonGhostBackgroundPressed NOTIFY themeChanged)
    Q_PROPERTY(QColor buttonGhostText READ buttonGhostText NOTIFY themeChanged)
    Q_PROPERTY(int buttonGhostRadius READ buttonGhostRadius CONSTANT)

    // ====================================================================
    // Component: Button Disabled (theme-dependent)
    // ====================================================================
    Q_PROPERTY(QColor buttonDisabledBackground READ buttonDisabledBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor buttonDisabledText READ buttonDisabledText NOTIFY themeChanged)
    Q_PROPERTY(QColor buttonDisabledBorder READ buttonDisabledBorder NOTIFY themeChanged)

    // ====================================================================
    // Component: Toolbar
    // ====================================================================
    Q_PROPERTY(int toolbarHeight READ toolbarHeight CONSTANT)
    Q_PROPERTY(int toolbarIconSize READ toolbarIconSize CONSTANT)
    Q_PROPERTY(int toolbarPadding READ toolbarPadding CONSTANT)
    Q_PROPERTY(int toolbarRadius READ toolbarRadius CONSTANT)
    Q_PROPERTY(QColor toolbarControlBackground READ toolbarControlBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor toolbarControlBackgroundHover READ toolbarControlBackgroundHover NOTIFY themeChanged)
    Q_PROPERTY(QColor toolbarControlBackgroundPressed READ toolbarControlBackgroundPressed NOTIFY themeChanged)
    Q_PROPERTY(QColor toolbarPopupBackground READ toolbarPopupBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor toolbarPopupBackgroundTop READ toolbarPopupBackgroundTop NOTIFY themeChanged)
    Q_PROPERTY(QColor toolbarPopupHighlight READ toolbarPopupHighlight NOTIFY themeChanged)
    Q_PROPERTY(QColor toolbarPopupBorder READ toolbarPopupBorder NOTIFY themeChanged)
    Q_PROPERTY(QColor toolbarPopupHover READ toolbarPopupHover NOTIFY themeChanged)
    Q_PROPERTY(QColor toolbarPopupSelected READ toolbarPopupSelected NOTIFY themeChanged)
    Q_PROPERTY(QColor toolbarPopupSelectedBorder READ toolbarPopupSelectedBorder NOTIFY themeChanged)

    // ====================================================================
    // Component: Tooltip (theme-dependent)
    // ====================================================================
    Q_PROPERTY(QColor tooltipBackground READ tooltipBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor tooltipBackgroundTop READ tooltipBackgroundTop NOTIFY themeChanged)
    Q_PROPERTY(QColor tooltipBorder READ tooltipBorder NOTIFY themeChanged)
    Q_PROPERTY(QColor tooltipHighlight READ tooltipHighlight NOTIFY themeChanged)
    Q_PROPERTY(QColor tooltipText READ tooltipText NOTIFY themeChanged)
    Q_PROPERTY(int tooltipRadius READ tooltipRadius CONSTANT)
    Q_PROPERTY(int tooltipPadding READ tooltipPadding CONSTANT)

    // ====================================================================
    // Component: Toast (theme-dependent)
    // ====================================================================
    Q_PROPERTY(QColor toastBackground READ toastBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor toastBorder READ toastBorder NOTIFY themeChanged)
    Q_PROPERTY(QColor toastTitleText READ toastTitleText NOTIFY themeChanged)
    Q_PROPERTY(QColor toastMessageText READ toastMessageText NOTIFY themeChanged)
    Q_PROPERTY(int toastRadius READ toastRadius CONSTANT)
    Q_PROPERTY(int toastPadding READ toastPadding CONSTANT)
    Q_PROPERTY(int toastShowDuration READ toastShowDuration CONSTANT)
    Q_PROPERTY(int toastHideDuration READ toastHideDuration CONSTANT)
    Q_PROPERTY(int toastDisplayDuration READ toastDisplayDuration CONSTANT)
    Q_PROPERTY(int toastPaddingH READ toastPaddingH CONSTANT)
    Q_PROPERTY(int toastPaddingV READ toastPaddingV CONSTANT)
    Q_PROPERTY(int toastDotSize READ toastDotSize CONSTANT)
    Q_PROPERTY(int toastDotLeftMargin READ toastDotLeftMargin CONSTANT)
    Q_PROPERTY(int toastIconTextGap READ toastIconTextGap CONSTANT)
    Q_PROPERTY(int toastScreenWidth READ toastScreenWidth CONSTANT)
    Q_PROPERTY(int toastTitleFontSize READ toastTitleFontSize CONSTANT)
    Q_PROPERTY(int toastMessageFontSize READ toastMessageFontSize CONSTANT)
    Q_PROPERTY(int toastTitleMessageGap READ toastTitleMessageGap CONSTANT)

    // ====================================================================
    // Component: Panel & Dialog (theme-dependent)
    // ====================================================================
    Q_PROPERTY(QColor panelBackground READ panelBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor panelBorder READ panelBorder NOTIFY themeChanged)
    Q_PROPERTY(int panelRadius READ panelRadius CONSTANT)
    Q_PROPERTY(int panelPadding READ panelPadding CONSTANT)
    Q_PROPERTY(QColor panelGlassBackground READ panelGlassBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor panelGlassBackgroundTop READ panelGlassBackgroundTop NOTIFY themeChanged)
    Q_PROPERTY(QColor panelGlassHighlight READ panelGlassHighlight CONSTANT)
    Q_PROPERTY(QColor panelGlassBorder READ panelGlassBorder NOTIFY themeChanged)
    Q_PROPERTY(QColor dialogBackground READ dialogBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor dialogBorder READ dialogBorder NOTIFY themeChanged)
    Q_PROPERTY(QColor dialogOverlay READ dialogOverlay NOTIFY themeChanged)
    Q_PROPERTY(int dialogRadius READ dialogRadius CONSTANT)
    Q_PROPERTY(int dialogPadding READ dialogPadding CONSTANT)
    Q_PROPERTY(int dialogTitleBarHeight READ dialogTitleBarHeight CONSTANT)
    Q_PROPERTY(int dialogButtonBarHeight READ dialogButtonBarHeight CONSTANT)

    // ====================================================================
    // Component: Badge (theme-dependent)
    // ====================================================================
    Q_PROPERTY(QColor badgeBackground READ badgeBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor badgeHighlight READ badgeHighlight CONSTANT)
    Q_PROPERTY(QColor badgeBorder READ badgeBorder NOTIFY themeChanged)
    Q_PROPERTY(QColor badgeText READ badgeText NOTIFY themeChanged)
    Q_PROPERTY(int badgeRadius READ badgeRadius CONSTANT)
    Q_PROPERTY(int badgePaddingH READ badgePaddingH CONSTANT)
    Q_PROPERTY(int badgePaddingV READ badgePaddingV CONSTANT)
    Q_PROPERTY(int badgeFontSize READ badgeFontSize CONSTANT)
    Q_PROPERTY(int badgeFadeInDuration READ badgeFadeInDuration CONSTANT)
    Q_PROPERTY(int badgeFadeOutDuration READ badgeFadeOutDuration CONSTANT)
    Q_PROPERTY(int badgeDisplayDuration READ badgeDisplayDuration CONSTANT)

    // ====================================================================
    // Component: Settings (theme-dependent)
    // ====================================================================
    Q_PROPERTY(QColor settingsSidebarBg READ settingsSidebarBg NOTIFY themeChanged)
    Q_PROPERTY(QColor settingsSidebarActiveItem READ settingsSidebarActiveItem NOTIFY themeChanged)
    Q_PROPERTY(QColor settingsSidebarHoverItem READ settingsSidebarHoverItem NOTIFY themeChanged)
    Q_PROPERTY(QColor settingsSectionText READ settingsSectionText NOTIFY themeChanged)
    Q_PROPERTY(int settingsSidebarWidth READ settingsSidebarWidth CONSTANT)
    Q_PROPERTY(int settingsSidebarMinWidth READ settingsSidebarMinWidth CONSTANT)
    Q_PROPERTY(int settingsSidebarMaxWidth READ settingsSidebarMaxWidth CONSTANT)
    Q_PROPERTY(int settingsContentPadding READ settingsContentPadding CONSTANT)
    Q_PROPERTY(int settingsSectionTopPadding READ settingsSectionTopPadding CONSTANT)
    Q_PROPERTY(int settingsColumnSpacing READ settingsColumnSpacing CONSTANT)
    Q_PROPERTY(int settingsBottomBarHeight READ settingsBottomBarHeight CONSTANT)
    Q_PROPERTY(int settingsBottomBarPaddingH READ settingsBottomBarPaddingH CONSTANT)
    Q_PROPERTY(int settingsToastTopMargin READ settingsToastTopMargin CONSTANT)

    // ====================================================================
    // Component: Form controls (theme-dependent)
    // ====================================================================
    Q_PROPERTY(QColor toggleTrackOn READ toggleTrackOn CONSTANT)
    Q_PROPERTY(QColor toggleTrackOff READ toggleTrackOff NOTIFY themeChanged)
    Q_PROPERTY(QColor toggleKnob READ toggleKnob CONSTANT)
    Q_PROPERTY(int toggleWidth READ toggleWidth CONSTANT)
    Q_PROPERTY(int toggleHeight READ toggleHeight CONSTANT)
    Q_PROPERTY(int toggleKnobSize READ toggleKnobSize CONSTANT)
    Q_PROPERTY(int toggleKnobRadius READ toggleKnobRadius CONSTANT)
    Q_PROPERTY(int toggleKnobInset READ toggleKnobInset CONSTANT)
    Q_PROPERTY(int toggleLabelIndent READ toggleLabelIndent CONSTANT)
    Q_PROPERTY(QColor inputBackground READ inputBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor inputBorder READ inputBorder NOTIFY themeChanged)
    Q_PROPERTY(QColor inputBorderFocus READ inputBorderFocus CONSTANT)
    Q_PROPERTY(int inputRadius READ inputRadius CONSTANT)
    Q_PROPERTY(int inputHeight READ inputHeight CONSTANT)
    Q_PROPERTY(QColor sliderTrack READ sliderTrack NOTIFY themeChanged)
    Q_PROPERTY(QColor sliderFill READ sliderFill CONSTANT)
    Q_PROPERTY(QColor sliderKnob READ sliderKnob CONSTANT)
    Q_PROPERTY(int sliderHeight READ sliderHeight CONSTANT)
    Q_PROPERTY(int sliderKnobSize READ sliderKnobSize CONSTANT)

    // ====================================================================
    // Component: Info Panel, Focus Ring (theme-dependent)
    // ====================================================================
    Q_PROPERTY(QColor infoPanelBg READ infoPanelBg NOTIFY themeChanged)
    Q_PROPERTY(QColor infoPanelBorder READ infoPanelBorder NOTIFY themeChanged)
    Q_PROPERTY(QColor infoPanelText READ infoPanelText NOTIFY themeChanged)
    Q_PROPERTY(QColor focusRingColor READ focusRingColor CONSTANT)
    Q_PROPERTY(int focusRingWidth READ focusRingWidth CONSTANT)
    Q_PROPERTY(int focusRingOffset READ focusRingOffset CONSTANT)
    Q_PROPERTY(int focusRingRadius READ focusRingRadius CONSTANT)

    // ====================================================================
    // Component: Recording Preview (theme-dependent)
    // ====================================================================
    Q_PROPERTY(QColor recordingPreviewBgStart READ recordingPreviewBgStart NOTIFY themeChanged)
    Q_PROPERTY(QColor recordingPreviewBgEnd READ recordingPreviewBgEnd NOTIFY themeChanged)
    Q_PROPERTY(QColor recordingPreviewPanel READ recordingPreviewPanel NOTIFY themeChanged)
    Q_PROPERTY(QColor recordingPreviewPanelHover READ recordingPreviewPanelHover NOTIFY themeChanged)
    Q_PROPERTY(QColor recordingPreviewPanelPressed READ recordingPreviewPanelPressed NOTIFY themeChanged)
    Q_PROPERTY(QColor recordingPreviewBorder READ recordingPreviewBorder NOTIFY themeChanged)
    Q_PROPERTY(QColor recordingPreviewPlayOverlay READ recordingPreviewPlayOverlay NOTIFY themeChanged)
    Q_PROPERTY(QColor recordingPreviewTrimOverlay READ recordingPreviewTrimOverlay NOTIFY themeChanged)
    Q_PROPERTY(QColor recordingPreviewPlayhead READ recordingPreviewPlayhead CONSTANT)
    Q_PROPERTY(QColor recordingPreviewPlayheadBorder READ recordingPreviewPlayheadBorder NOTIFY themeChanged)
    Q_PROPERTY(QColor recordingPreviewControlHighlight READ recordingPreviewControlHighlight NOTIFY themeChanged)
    Q_PROPERTY(QColor recordingPreviewDangerHover READ recordingPreviewDangerHover NOTIFY themeChanged)
    Q_PROPERTY(QColor recordingPreviewDangerPressed READ recordingPreviewDangerPressed NOTIFY themeChanged)
    Q_PROPERTY(QColor recordingPreviewPrimaryButton READ recordingPreviewPrimaryButton CONSTANT)
    Q_PROPERTY(QColor recordingPreviewPrimaryButtonHover READ recordingPreviewPrimaryButtonHover CONSTANT)
    Q_PROPERTY(QColor recordingPreviewPrimaryButtonPressed READ recordingPreviewPrimaryButtonPressed CONSTANT)
    Q_PROPERTY(QColor recordingPreviewPrimaryButtonIcon READ recordingPreviewPrimaryButtonIcon CONSTANT)
    Q_PROPERTY(int recordingPreviewControlButtonSize READ recordingPreviewControlButtonSize CONSTANT)
    Q_PROPERTY(int recordingPreviewControlButtonHeight READ recordingPreviewControlButtonHeight CONSTANT)
    Q_PROPERTY(int recordingPreviewIconSize READ recordingPreviewIconSize CONSTANT)
    Q_PROPERTY(int recordingPreviewActionIconSize READ recordingPreviewActionIconSize CONSTANT)

    // ====================================================================
    // Component: Panel-specific visuals (theme-dependent)
    // ====================================================================
    Q_PROPERTY(QColor thumbnailCardBackground READ thumbnailCardBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor thumbnailCardHover READ thumbnailCardHover NOTIFY themeChanged)
    Q_PROPERTY(QColor thumbnailCardSelected READ thumbnailCardSelected NOTIFY themeChanged)
    Q_PROPERTY(QColor thumbnailCardBorder READ thumbnailCardBorder NOTIFY themeChanged)
    Q_PROPERTY(QColor thumbnailCardSelectedBorder READ thumbnailCardSelectedBorder CONSTANT)
    Q_PROPERTY(QColor thumbnailCardErrorBackground READ thumbnailCardErrorBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor emojiCellHover READ emojiCellHover NOTIFY themeChanged)
    Q_PROPERTY(QColor emojiCellPressed READ emojiCellPressed NOTIFY themeChanged)
    Q_PROPERTY(QColor emojiCellSelectedRing READ emojiCellSelectedRing CONSTANT)
    Q_PROPERTY(QColor beautifyPreviewFrame READ beautifyPreviewFrame NOTIFY themeChanged)
    Q_PROPERTY(QColor beautifyPreviewPlaceholder READ beautifyPreviewPlaceholder NOTIFY themeChanged)
    Q_PROPERTY(QColor beautifyPresetRing READ beautifyPresetRing CONSTANT)
    Q_PROPERTY(QColor beautifyPresetHoverRing READ beautifyPresetHoverRing CONSTANT)

    // ====================================================================
    // Component: Recording Boundary (always dark)
    // ====================================================================
    Q_PROPERTY(QColor recordingBoundaryGradientStart READ recordingBoundaryGradientStart CONSTANT)
    Q_PROPERTY(QColor recordingBoundaryGradientMid1 READ recordingBoundaryGradientMid1 CONSTANT)
    Q_PROPERTY(QColor recordingBoundaryGradientMid2 READ recordingBoundaryGradientMid2 CONSTANT)
    Q_PROPERTY(QColor recordingBoundaryGradientEnd READ recordingBoundaryGradientEnd CONSTANT)
    Q_PROPERTY(QColor recordingBoundaryPaused READ recordingBoundaryPaused CONSTANT)
    Q_PROPERTY(int recordingBoundaryLoopDuration READ recordingBoundaryLoopDuration CONSTANT)

    // ====================================================================
    // Component: Countdown Overlay (theme-dependent overlay)
    // ====================================================================
    Q_PROPERTY(QColor countdownOverlay READ countdownOverlay NOTIFY themeChanged)
    Q_PROPERTY(QColor countdownText READ countdownText CONSTANT)
    Q_PROPERTY(int countdownFontSize READ countdownFontSize CONSTANT)
    Q_PROPERTY(int countdownScaleDuration READ countdownScaleDuration CONSTANT)
    Q_PROPERTY(int countdownFadeDuration READ countdownFadeDuration CONSTANT)

    // ====================================================================
    // Component: Icon sizes (semantic aliases)
    // ====================================================================
    Q_PROPERTY(int iconSizeToolbar READ iconSizeToolbar CONSTANT)
    Q_PROPERTY(int iconSizeMenu READ iconSizeMenu CONSTANT)
    Q_PROPERTY(int iconSizeAction READ iconSizeAction CONSTANT)

    // ====================================================================
    // Component: Recording Indicator
    // ====================================================================
    Q_PROPERTY(int recordingIndicatorDuration READ recordingIndicatorDuration CONSTANT)
    Q_PROPERTY(QColor recordingAudioActive READ recordingAudioActive CONSTANT)

    // ====================================================================
    // Component: Recording Control Bar (always dark)
    // ====================================================================
    Q_PROPERTY(QColor recordingControlBarBg READ recordingControlBarBg CONSTANT)
    Q_PROPERTY(QColor recordingControlBarBgTop READ recordingControlBarBgTop CONSTANT)
    Q_PROPERTY(QColor recordingControlBarHighlight READ recordingControlBarHighlight CONSTANT)
    Q_PROPERTY(QColor recordingControlBarBorder READ recordingControlBarBorder CONSTANT)
    Q_PROPERTY(QColor recordingControlBarText READ recordingControlBarText CONSTANT)
    Q_PROPERTY(QColor recordingControlBarSeparator READ recordingControlBarSeparator CONSTANT)
    Q_PROPERTY(QColor recordingControlBarHoverBg READ recordingControlBarHoverBg CONSTANT)
    Q_PROPERTY(QColor recordingControlBarIconNormal READ recordingControlBarIconNormal CONSTANT)
    Q_PROPERTY(QColor recordingControlBarIconActive READ recordingControlBarIconActive CONSTANT)
    Q_PROPERTY(QColor recordingControlBarIconRecord READ recordingControlBarIconRecord CONSTANT)
    Q_PROPERTY(QColor recordingControlBarIconCancel READ recordingControlBarIconCancel CONSTANT)
    Q_PROPERTY(int recordingControlBarRadius READ recordingControlBarRadius CONSTANT)
    Q_PROPERTY(int recordingControlBarHeight READ recordingControlBarHeight CONSTANT)
    Q_PROPERTY(int recordingControlBarButtonSize READ recordingControlBarButtonSize CONSTANT)
    Q_PROPERTY(int recordingControlBarButtonSpacing READ recordingControlBarButtonSpacing CONSTANT)
    Q_PROPERTY(int recordingControlBarMarginH READ recordingControlBarMarginH CONSTANT)
    Q_PROPERTY(int recordingControlBarMarginV READ recordingControlBarMarginV CONSTANT)
    Q_PROPERTY(int recordingControlBarItemSpacing READ recordingControlBarItemSpacing CONSTANT)
    Q_PROPERTY(int recordingControlBarIndicatorSize READ recordingControlBarIndicatorSize CONSTANT)
    Q_PROPERTY(int recordingControlBarFontSize READ recordingControlBarFontSize CONSTANT)
    Q_PROPERTY(int recordingControlBarFontSizeSmall READ recordingControlBarFontSizeSmall CONSTANT)
    Q_PROPERTY(int recordingControlBarIconSize READ recordingControlBarIconSize CONSTANT)
    Q_PROPERTY(int recordingControlBarButtonRadius READ recordingControlBarButtonRadius CONSTANT)

    // ====================================================================
    // Component: Recording Region Selector (always dark)
    // ====================================================================
    Q_PROPERTY(QColor recordingRegionOverlayDim READ recordingRegionOverlayDim CONSTANT)
    Q_PROPERTY(QColor recordingRegionCrosshair READ recordingRegionCrosshair CONSTANT)
    Q_PROPERTY(QColor recordingRegionGradientStart READ recordingRegionGradientStart CONSTANT)
    Q_PROPERTY(QColor recordingRegionGradientMid READ recordingRegionGradientMid CONSTANT)
    Q_PROPERTY(QColor recordingRegionGradientEnd READ recordingRegionGradientEnd CONSTANT)
    Q_PROPERTY(QColor recordingRegionGlowColor READ recordingRegionGlowColor CONSTANT)
    Q_PROPERTY(QColor recordingRegionDashColor READ recordingRegionDashColor CONSTANT)
    Q_PROPERTY(QColor recordingRegionGlassBg READ recordingRegionGlassBg CONSTANT)
    Q_PROPERTY(QColor recordingRegionGlassBgTop READ recordingRegionGlassBgTop CONSTANT)
    Q_PROPERTY(QColor recordingRegionGlassHighlight READ recordingRegionGlassHighlight CONSTANT)
    Q_PROPERTY(QColor recordingRegionGlassBorder READ recordingRegionGlassBorder CONSTANT)
    Q_PROPERTY(QColor recordingRegionText READ recordingRegionText CONSTANT)
    Q_PROPERTY(QColor recordingRegionHoverBg READ recordingRegionHoverBg CONSTANT)
    Q_PROPERTY(QColor recordingRegionIconNormal READ recordingRegionIconNormal CONSTANT)
    Q_PROPERTY(QColor recordingRegionIconCancel READ recordingRegionIconCancel CONSTANT)
    Q_PROPERTY(QColor recordingRegionIconRecord READ recordingRegionIconRecord CONSTANT)
    Q_PROPERTY(int recordingRegionActionBarWidth READ recordingRegionActionBarWidth CONSTANT)
    Q_PROPERTY(int recordingRegionActionBarHeight READ recordingRegionActionBarHeight CONSTANT)
    Q_PROPERTY(int recordingRegionActionBarButtonSize READ recordingRegionActionBarButtonSize CONSTANT)
    Q_PROPERTY(int recordingRegionActionBarButtonHeight READ recordingRegionActionBarButtonHeight CONSTANT)
    Q_PROPERTY(int recordingRegionActionBarRadius READ recordingRegionActionBarRadius CONSTANT)
    Q_PROPERTY(int recordingRegionGlassRadius READ recordingRegionGlassRadius CONSTANT)
    Q_PROPERTY(int recordingRegionDimensionRadius READ recordingRegionDimensionRadius CONSTANT)
    Q_PROPERTY(int recordingRegionDimensionFontSize READ recordingRegionDimensionFontSize CONSTANT)
    Q_PROPERTY(int recordingRegionHelpFontSize READ recordingRegionHelpFontSize CONSTANT)

    // ====================================================================
    // Component: Watermark, Combo, Spinner
    // ====================================================================
    Q_PROPERTY(int watermarkPreviewColumnWidth READ watermarkPreviewColumnWidth CONSTANT)
    Q_PROPERTY(int watermarkPreviewSize READ watermarkPreviewSize CONSTANT)
    Q_PROPERTY(int comboPopupMaxHeight READ comboPopupMaxHeight CONSTANT)
    Q_PROPERTY(int spinnerDuration READ spinnerDuration CONSTANT)

public:
    static DesignSystem& instance();
    static DesignSystem* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    bool isDarkMode() const;

    /**
     * @brief Re-evaluate the theme from current settings.
     * Call after ToolbarStyleConfig::saveStyle() to propagate changes.
     */
    void refreshTheme();

    /**
     * @brief Build a ToolbarStyleConfig populated from canonical tokens.
     * Used by the backward-compatible ToolbarStyleConfig facade.
     */
    ToolbarStyleConfig buildToolbarStyleConfig(bool dark) const;

    // -- Primitive: Brand ------------------------------------------------
    QColor primaryPurple() const;

    // -- Primitive: Annotation palette ------------------------------------
    QColor annotationRed() const;
    QColor annotationBlue() const;
    QColor annotationOrange() const;
    QColor annotationGreen() const;
    QColor annotationPurple() const;
    QColor annotationYellow() const;
    QColor annotationPink() const;
    QColor annotationBlack() const;
    QColor annotationWhite() const;

    // -- Primitive: Capture overlay (always dark) -------------------------
    QColor dimOverlay() const;
    QColor crosshair() const;
    QColor selectionBorder() const;
    QColor dimensionLabel() const;

    // -- Primitive: Neutral palette ---------------------------------------
    QColor white() const;
    QColor gray50() const;
    QColor gray100() const;
    QColor gray200() const;
    QColor gray300() const;
    QColor gray400() const;
    QColor gray500() const;
    QColor gray600() const;
    QColor gray700() const;
    QColor gray800() const;
    QColor gray850() const;
    QColor gray900() const;
    QColor gray950() const;
    QColor black() const;
    QColor darkSurface() const;
    QColor darkSurfaceElevated() const;
    QColor darkSurfaceOverlay() const;
    QColor lightSurface() const;
    QColor lightSurfaceElevated() const;
    QColor lightSurfaceOverlay() const;

    // -- Primitive: Status colors -----------------------------------------
    QColor green500() const;
    QColor green400() const;
    QColor amber500() const;
    QColor amber400() const;
    QColor amber600() const;
    QColor red500() const;
    QColor red400() const;
    QColor blue500() const;
    QColor blue400() const;
    QColor indigo500() const;
    QColor purple400() const;

    // -- Primitive: Recording boundary gradient ---------------------------
    QColor boundaryBlue() const;
    QColor boundaryPurple() const;

    // -- Primitive: Accent states -----------------------------------------
    QColor accentDefault() const;
    QColor accentHover() const;
    QColor accentPressed() const;
    QColor accentLight() const;

    // -- Primitive: Spacing -----------------------------------------------
    int spacing2() const;
    int spacing4() const;
    int spacing8() const;
    int spacing12() const;
    int spacing16() const;
    int spacing20() const;
    int spacing24() const;
    int spacing32() const;
    int spacing40() const;
    int spacing48() const;

    // -- Primitive: Radius ------------------------------------------------
    int radiusSmall() const;
    int radiusMedium() const;
    int radiusLarge() const;
    int radiusXL() const;

    // -- Primitive: Font sizes --------------------------------------------
    int fontSizeDisplay() const;
    int fontSizeH1() const;
    int fontSizeH2() const;
    int fontSizeH3() const;
    int fontSizeBody() const;
    int fontSizeSmallBody() const;
    int fontSizeCaption() const;
    int fontSizeSmall() const;

    // -- Primitive: Font weights -------------------------------------------
    int fontWeightRegular() const;
    int fontWeightMedium() const;
    int fontWeightSemiBold() const;
    int fontWeightBold() const;

    // -- Primitive: Letter spacing -----------------------------------------
    qreal letterSpacingTight() const;
    qreal letterSpacingWide() const;

    // -- Primitive: Font family --------------------------------------------
    QString fontFamily() const;
    int fontBaseSize() const;

    // -- Primitive: Icon sizes ---------------------------------------------
    int iconSizeSmall() const;
    int iconSizeMedium() const;
    int iconSizeLarge() const;

    // -- Primitive: Shadows ------------------------------------------------
    QColor lightShadowSmall() const;
    int lightShadowSmallBlur() const;
    int lightShadowSmallY() const;
    QColor lightShadowMedium() const;
    int lightShadowMediumBlur() const;
    int lightShadowMediumY() const;
    QColor lightShadowLarge() const;
    int lightShadowLargeBlur() const;
    int lightShadowLargeY() const;
    QColor darkShadowSmall() const;
    int darkShadowSmallBlur() const;
    int darkShadowSmallY() const;
    QColor darkShadowMedium() const;
    int darkShadowMediumBlur() const;
    int darkShadowMediumY() const;
    QColor darkShadowLarge() const;
    int darkShadowLargeBlur() const;
    int darkShadowLargeY() const;

    // -- Primitive: Animation durations ------------------------------------
    int durationInstant() const;
    int durationFast() const;
    int durationNormal() const;
    int durationSlow() const;
    int durationEmphasis() const;
    int durationBoundaryLoop() const;
    int durationInteractionHover() const;
    int durationInteractionPress() const;
    int durationInteractionState() const;
    int durationInteractionMax() const;

    // -- Primitive: Icon system --------------------------------------------
    int iconStrokeWidth() const;
    int iconGridSize() const;
    QColor iconColorLight() const;
    QColor iconColorDark() const;

    // -- Semantic: Background (theme-dependent) ----------------------------
    QColor backgroundPrimary() const;
    QColor backgroundElevated() const;
    QColor backgroundOverlay() const;

    // -- Semantic: Text (theme-dependent) ----------------------------------
    QColor textPrimary() const;
    QColor textSecondary() const;
    QColor textTertiary() const;
    QColor textInverse() const;
    QColor textOnAccent() const;

    // -- Semantic: Border -------------------------------------------------
    QColor borderDefault() const;
    QColor borderFocus() const;
    QColor borderActive() const;

    // -- Semantic: Status (theme-dependent) --------------------------------
    QColor statusSuccess() const;
    QColor statusWarning() const;
    QColor statusError() const;
    QColor statusInfo() const;

    // -- Semantic: Toolbar (theme-dependent) -------------------------------
    QColor toolbarBackground() const;
    QColor toolbarIcon() const;
    QColor toolbarIconActive() const;
    QColor toolbarSeparator() const;

    // -- Semantic: Shadows (theme-dependent) -------------------------------
    QColor shadowSmallColor() const;
    int shadowSmallBlur() const;
    int shadowSmallY() const;
    QColor shadowMediumColor() const;
    int shadowMediumBlur() const;
    int shadowMediumY() const;
    QColor shadowLargeColor() const;
    int shadowLargeBlur() const;
    int shadowLargeY() const;

    // -- Semantic: Capture overlay -----------------------------------------
    QColor captureOverlayDim() const;
    QColor captureOverlayCrosshair() const;
    QColor captureOverlaySelectionBorder() const;
    QColor captureOverlayDimensionLabel() const;

    // -- Capture overlay panel (always dark) ------------------------------
    QColor captureOverlayPanelBg() const;
    QColor captureOverlayPanelBgTop() const;
    QColor captureOverlayPanelHighlight() const;
    QColor captureOverlayPanelBorder() const;
    int captureOverlayPanelRadius() const;
    QColor captureOverlayKeycapBg() const;
    QColor captureOverlayKeycapBorder() const;
    QColor captureOverlayKeycapText() const;
    QColor captureOverlayHintText() const;
    int captureOverlayKeycapRadius() const;
    QColor captureOverlayIconNormal() const;
    QColor captureOverlayIconActive() const;
    QColor captureOverlayIconDimmed() const;
    QColor captureOverlayControlHoverBg() const;
    QColor captureOverlayListItemHover() const;
    QColor captureOverlayListItemActive() const;
    QColor captureOverlayListButtonBg() const;
    QColor captureOverlayListButtonHover() const;
    QColor captureOverlaySliderTrack() const;
    QColor captureOverlaySliderFill() const;
    QColor captureOverlaySliderKnob() const;

    // -- Semantic: Icon overlay (theme-dependent) -------------------------
    QColor iconColor() const;

    // -- Component: Button Primary ----------------------------------------
    QColor buttonPrimaryBackground() const;
    QColor buttonPrimaryBackgroundHover() const;
    QColor buttonPrimaryBackgroundPressed() const;
    QColor buttonPrimaryText() const;
    int buttonPrimaryRadius() const;

    // -- Component: Button Secondary (theme-dependent) --------------------
    QColor buttonSecondaryBackground() const;
    QColor buttonSecondaryBackgroundHover() const;
    QColor buttonSecondaryBackgroundPressed() const;
    QColor buttonSecondaryText() const;
    QColor buttonSecondaryBorder() const;
    int buttonSecondaryRadius() const;

    // -- Component: Button Ghost (theme-dependent) ------------------------
    QColor buttonGhostBackground() const;
    QColor buttonGhostBackgroundHover() const;
    QColor buttonGhostBackgroundPressed() const;
    QColor buttonGhostText() const;
    int buttonGhostRadius() const;

    // -- Component: Button Disabled (theme-dependent) ---------------------
    QColor buttonDisabledBackground() const;
    QColor buttonDisabledText() const;
    QColor buttonDisabledBorder() const;

    // -- Component: Toolbar layout ----------------------------------------
    int toolbarHeight() const;
    int toolbarIconSize() const;
    int toolbarPadding() const;
    int toolbarRadius() const;
    QColor toolbarControlBackground() const;
    QColor toolbarControlBackgroundHover() const;
    QColor toolbarControlBackgroundPressed() const;
    QColor toolbarPopupBackground() const;
    QColor toolbarPopupBackgroundTop() const;
    QColor toolbarPopupHighlight() const;
    QColor toolbarPopupBorder() const;
    QColor toolbarPopupHover() const;
    QColor toolbarPopupSelected() const;
    QColor toolbarPopupSelectedBorder() const;

    // -- Component: Tooltip (theme-dependent) -----------------------------
    QColor tooltipBackground() const;
    QColor tooltipBackgroundTop() const;
    QColor tooltipBorder() const;
    QColor tooltipHighlight() const;
    QColor tooltipText() const;
    int tooltipRadius() const;
    int tooltipPadding() const;

    // -- Component: Toast (theme-dependent) -------------------------------
    QColor toastBackground() const;
    QColor toastBorder() const;
    QColor toastTitleText() const;
    QColor toastMessageText() const;
    int toastRadius() const;
    int toastPadding() const;
    int toastShowDuration() const;
    int toastHideDuration() const;
    int toastDisplayDuration() const;
    int toastPaddingH() const;
    int toastPaddingV() const;
    int toastDotSize() const;
    int toastDotLeftMargin() const;
    int toastIconTextGap() const;
    int toastScreenWidth() const;
    int toastTitleFontSize() const;
    int toastMessageFontSize() const;
    int toastTitleMessageGap() const;

    // -- Component: Panel & Dialog ----------------------------------------
    QColor panelBackground() const;
    QColor panelBorder() const;
    int panelRadius() const;
    int panelPadding() const;
    QColor panelGlassBackground() const;
    QColor panelGlassBackgroundTop() const;
    QColor panelGlassHighlight() const;
    QColor panelGlassBorder() const;
    QColor dialogBackground() const;
    QColor dialogBorder() const;
    QColor dialogOverlay() const;
    int dialogRadius() const;
    int dialogPadding() const;
    int dialogTitleBarHeight() const;
    int dialogButtonBarHeight() const;

    // -- Component: Badge (theme-dependent) -------------------------------
    QColor badgeBackground() const;
    QColor badgeHighlight() const;
    QColor badgeBorder() const;
    QColor badgeText() const;
    int badgeRadius() const;
    int badgePaddingH() const;
    int badgePaddingV() const;
    int badgeFontSize() const;
    int badgeFadeInDuration() const;
    int badgeFadeOutDuration() const;
    int badgeDisplayDuration() const;

    // -- Component: Settings (theme-dependent) ----------------------------
    QColor settingsSidebarBg() const;
    QColor settingsSidebarActiveItem() const;
    QColor settingsSidebarHoverItem() const;
    QColor settingsSectionText() const;
    int settingsSidebarWidth() const;
    int settingsSidebarMinWidth() const;
    int settingsSidebarMaxWidth() const;
    int settingsContentPadding() const;
    int settingsSectionTopPadding() const;
    int settingsColumnSpacing() const;
    int settingsBottomBarHeight() const;
    int settingsBottomBarPaddingH() const;
    int settingsToastTopMargin() const;

    // -- Component: Form controls -----------------------------------------
    QColor toggleTrackOn() const;
    QColor toggleTrackOff() const;
    QColor toggleKnob() const;
    int toggleWidth() const;
    int toggleHeight() const;
    int toggleKnobSize() const;
    int toggleKnobRadius() const;
    int toggleKnobInset() const;
    int toggleLabelIndent() const;
    QColor inputBackground() const;
    QColor inputBorder() const;
    QColor inputBorderFocus() const;
    int inputRadius() const;
    int inputHeight() const;
    QColor sliderTrack() const;
    QColor sliderFill() const;
    QColor sliderKnob() const;
    int sliderHeight() const;
    int sliderKnobSize() const;

    // -- Component: Info Panel, Focus Ring --------------------------------
    QColor infoPanelBg() const;
    QColor infoPanelBorder() const;
    QColor infoPanelText() const;
    QColor focusRingColor() const;
    int focusRingWidth() const;
    int focusRingOffset() const;
    int focusRingRadius() const;

    // -- Component: Recording Preview (theme-dependent) -------------------
    QColor recordingPreviewBgStart() const;
    QColor recordingPreviewBgEnd() const;
    QColor recordingPreviewPanel() const;
    QColor recordingPreviewPanelHover() const;
    QColor recordingPreviewPanelPressed() const;
    QColor recordingPreviewBorder() const;
    QColor recordingPreviewPlayOverlay() const;
    QColor recordingPreviewTrimOverlay() const;
    QColor recordingPreviewPlayhead() const;
    QColor recordingPreviewPlayheadBorder() const;
    QColor recordingPreviewControlHighlight() const;
    QColor recordingPreviewDangerHover() const;
    QColor recordingPreviewDangerPressed() const;
    QColor recordingPreviewPrimaryButton() const;
    QColor recordingPreviewPrimaryButtonHover() const;
    QColor recordingPreviewPrimaryButtonPressed() const;
    QColor recordingPreviewPrimaryButtonIcon() const;
    int recordingPreviewControlButtonSize() const;
    int recordingPreviewControlButtonHeight() const;
    int recordingPreviewIconSize() const;
    int recordingPreviewActionIconSize() const;

    // -- Component: Panel-specific visuals --------------------------------
    QColor thumbnailCardBackground() const;
    QColor thumbnailCardHover() const;
    QColor thumbnailCardSelected() const;
    QColor thumbnailCardBorder() const;
    QColor thumbnailCardSelectedBorder() const;
    QColor thumbnailCardErrorBackground() const;
    QColor emojiCellHover() const;
    QColor emojiCellPressed() const;
    QColor emojiCellSelectedRing() const;
    QColor beautifyPreviewFrame() const;
    QColor beautifyPreviewPlaceholder() const;
    QColor beautifyPresetRing() const;
    QColor beautifyPresetHoverRing() const;

    // -- Component: Recording Boundary (always dark) ----------------------
    QColor recordingBoundaryGradientStart() const;
    QColor recordingBoundaryGradientMid1() const;
    QColor recordingBoundaryGradientMid2() const;
    QColor recordingBoundaryGradientEnd() const;
    QColor recordingBoundaryPaused() const;
    int recordingBoundaryLoopDuration() const;

    // -- Component: Countdown Overlay -------------------------------------
    QColor countdownOverlay() const;
    QColor countdownText() const;
    int countdownFontSize() const;
    int countdownScaleDuration() const;
    int countdownFadeDuration() const;

    // -- Component: Icon sizes (semantic aliases) -------------------------
    int iconSizeToolbar() const;
    int iconSizeMenu() const;
    int iconSizeAction() const;

    // -- Component: Recording Indicator -----------------------------------
    int recordingIndicatorDuration() const;
    QColor recordingAudioActive() const;

    // -- Component: Recording Control Bar (always dark) -------------------
    QColor recordingControlBarBg() const;
    QColor recordingControlBarBgTop() const;
    QColor recordingControlBarHighlight() const;
    QColor recordingControlBarBorder() const;
    QColor recordingControlBarText() const;
    QColor recordingControlBarSeparator() const;
    QColor recordingControlBarHoverBg() const;
    QColor recordingControlBarIconNormal() const;
    QColor recordingControlBarIconActive() const;
    QColor recordingControlBarIconRecord() const;
    QColor recordingControlBarIconCancel() const;
    int recordingControlBarRadius() const;
    int recordingControlBarHeight() const;
    int recordingControlBarButtonSize() const;
    int recordingControlBarButtonSpacing() const;
    int recordingControlBarMarginH() const;
    int recordingControlBarMarginV() const;
    int recordingControlBarItemSpacing() const;
    int recordingControlBarIndicatorSize() const;
    int recordingControlBarFontSize() const;
    int recordingControlBarFontSizeSmall() const;
    int recordingControlBarIconSize() const;
    int recordingControlBarButtonRadius() const;

    // -- Component: Recording Region Selector (always dark) ---------------
    QColor recordingRegionOverlayDim() const;
    QColor recordingRegionCrosshair() const;
    QColor recordingRegionGradientStart() const;
    QColor recordingRegionGradientMid() const;
    QColor recordingRegionGradientEnd() const;
    QColor recordingRegionGlowColor() const;
    QColor recordingRegionDashColor() const;
    QColor recordingRegionGlassBg() const;
    QColor recordingRegionGlassBgTop() const;
    QColor recordingRegionGlassHighlight() const;
    QColor recordingRegionGlassBorder() const;
    QColor recordingRegionText() const;
    QColor recordingRegionHoverBg() const;
    QColor recordingRegionIconNormal() const;
    QColor recordingRegionIconCancel() const;
    QColor recordingRegionIconRecord() const;
    int recordingRegionActionBarWidth() const;
    int recordingRegionActionBarHeight() const;
    int recordingRegionActionBarButtonSize() const;
    int recordingRegionActionBarButtonHeight() const;
    int recordingRegionActionBarRadius() const;
    int recordingRegionGlassRadius() const;
    int recordingRegionDimensionRadius() const;
    int recordingRegionDimensionFontSize() const;
    int recordingRegionHelpFontSize() const;

    // -- Component: Watermark, Combo, Spinner -----------------------------
    int watermarkPreviewColumnWidth() const;
    int watermarkPreviewSize() const;
    int comboPopupMaxHeight() const;
    int spinnerDuration() const;

signals:
    void themeChanged();

private:
    explicit DesignSystem(QObject* parent = nullptr);
    DesignSystem(const DesignSystem&) = delete;
    DesignSystem& operator=(const DesignSystem&) = delete;

    void updateThemeFromSettings();

    bool m_isDark = false;
};
