#include "ui/DesignSystem.h"
#include "ToolbarStyle.h"
#include "settings/Settings.h"

#include <QFont>
#include <QJSEngine>
#include <QOperatingSystemVersion>
#include <QSettings>

// ============================================================================
// Singleton & QML factory
// ============================================================================

DesignSystem::DesignSystem(QObject* parent)
    : QObject(parent)
{
    updateThemeFromSettings();
}

DesignSystem& DesignSystem::instance()
{
    static DesignSystem inst;
    return inst;
}

DesignSystem* DesignSystem::create(QQmlEngine*, QJSEngine* jsEngine)
{
    auto* inst = &instance();
    QJSEngine::setObjectOwnership(inst, QJSEngine::CppOwnership);
    return inst;
}

bool DesignSystem::isDarkMode() const { return m_isDark; }

void DesignSystem::refreshTheme()
{
    updateThemeFromSettings();
}

void DesignSystem::updateThemeFromSettings()
{
    auto settings = SnapTray::getSettings();
    int styleValue = settings.value("appearance/toolbarStyle", 0).toInt();
    bool dark = (styleValue == static_cast<int>(ToolbarStyleType::Dark));

    if (m_isDark != dark) {
        m_isDark = dark;
        emit themeChanged();
    }
}

// ============================================================================
// Build backward-compatible ToolbarStyleConfig
// ============================================================================

ToolbarStyleConfig DesignSystem::buildToolbarStyleConfig(bool dark) const
{
    if (dark) {
        return ToolbarStyleConfig{
            QColor(55, 55, 55, 245),   QColor(40, 40, 40, 245),
            QColor(70, 70, 70),        50,
            QColor(220, 220, 220),     Qt::white,
            QColor(220, 220, 220),     accentLight(),
            QColor(255, 100, 100),     QColor(255, 80, 80),
            accentDefault(),            QColor(80, 80, 80),
            false,                     QColor(255, 68, 68),   6,
            QColor(80, 80, 80),        QColor(30, 30, 30, 230),
            QColor(80, 80, 80),        Qt::white,
            accentDefault(),           QColor(80, 80, 80),
            QColor(50, 50, 50),        QColor(200, 200, 200),
            Qt::white,                 QColor(50, 50, 50, 250),
            QColor(70, 70, 70),
            QColor(40, 40, 40, 217),   QColor(255, 255, 255, 15),
            QColor(255, 255, 255, 25),
            QColor(0, 0, 0, 60),       12, 4,
            10
        };
    }
    return ToolbarStyleConfig{
        QColor(248, 248, 248, 245), QColor(240, 240, 240, 245),
        QColor(224, 224, 224),      30,
        QColor(60, 60, 60),         Qt::white,
        QColor(70, 70, 70),         accentDefault(),
        QColor(220, 60, 60),        QColor(220, 50, 50),
        accentDefault(),             QColor(220, 220, 220),
        false,                      QColor(255, 68, 68),   6,
        QColor(208, 208, 208),      QColor(255, 255, 255, 240),
        QColor(200, 200, 200),      QColor(50, 50, 50),
        accentDefault(),            QColor(232, 232, 232),
        QColor(245, 245, 245),      QColor(80, 80, 80),
        Qt::white,                  QColor(255, 255, 255, 250),
        QColor(200, 200, 200),
        QColor(255, 255, 255, 230), QColor(255, 255, 255, 200),
        QColor(0, 0, 0, 20),
        QColor(0, 0, 0, 30),        8, 2,
        10
    };
}

// ============================================================================
// Primitive: Brand
// ============================================================================
QColor DesignSystem::primaryPurple() const { return QColor(0x6C, 0x5C, 0xE7); }

// ============================================================================
// Primitive: Annotation palette
// ============================================================================
QColor DesignSystem::annotationRed() const    { return QColor(0xFF, 0x3B, 0x30); }
QColor DesignSystem::annotationBlue() const   { return QColor(0x00, 0x7A, 0xFF); }
QColor DesignSystem::annotationOrange() const { return QColor(0xFF, 0x95, 0x00); }
QColor DesignSystem::annotationGreen() const  { return QColor(0x34, 0xC7, 0x59); }
QColor DesignSystem::annotationPurple() const { return QColor(0xAF, 0x52, 0xDE); }
QColor DesignSystem::annotationYellow() const { return QColor(0xFF, 0xCC, 0x00); }
QColor DesignSystem::annotationPink() const   { return QColor(0xFF, 0x2D, 0x55); }
QColor DesignSystem::annotationBlack() const  { return QColor(0x00, 0x00, 0x00); }
QColor DesignSystem::annotationWhite() const  { return QColor(0xFF, 0xFF, 0xFF); }

// ============================================================================
// Primitive: Capture overlay
// ============================================================================
QColor DesignSystem::dimOverlay() const      { return QColor::fromRgbF(0, 0, 0, 0.45); }
QColor DesignSystem::crosshair() const       { return QColor(0x00, 0xFF, 0x00); }
QColor DesignSystem::selectionBorder() const { return QColor(0xFF, 0xFF, 0xFF); }
QColor DesignSystem::dimensionLabel() const  { return QColor::fromRgbF(0, 0, 0, 0.75); }

// ============================================================================
// Primitive: Neutral palette
// ============================================================================
QColor DesignSystem::white() const   { return QColor(0xFF, 0xFF, 0xFF); }
QColor DesignSystem::gray50() const  { return QColor(0xFA, 0xFA, 0xFA); }
QColor DesignSystem::gray100() const { return QColor(0xF5, 0xF5, 0xF5); }
QColor DesignSystem::gray200() const { return QColor(0xEE, 0xEE, 0xEE); }
QColor DesignSystem::gray300() const { return QColor(0xE0, 0xE0, 0xE0); }
QColor DesignSystem::gray400() const { return QColor(0xBD, 0xBD, 0xBD); }
QColor DesignSystem::gray500() const { return QColor(0x9E, 0x9E, 0x9E); }
QColor DesignSystem::gray600() const { return QColor(0x75, 0x75, 0x75); }
QColor DesignSystem::gray700() const { return QColor(0x61, 0x61, 0x61); }
QColor DesignSystem::gray800() const { return QColor(0x42, 0x42, 0x42); }
QColor DesignSystem::gray850() const { return QColor(0x30, 0x30, 0x30); }
QColor DesignSystem::gray900() const { return QColor(0x21, 0x21, 0x21); }
QColor DesignSystem::gray950() const { return QColor(0x1A, 0x1A, 0x1A); }
QColor DesignSystem::black() const   { return QColor(0x00, 0x00, 0x00); }

QColor DesignSystem::darkSurface() const         { return QColor(0x1A, 0x1A, 0x2E); }
QColor DesignSystem::darkSurfaceElevated() const  { return QColor(0x2E, 0x2E, 0x50); }
QColor DesignSystem::darkSurfaceOverlay() const   { return QColor::fromRgbF(0, 0, 0, 0.6); }
QColor DesignSystem::lightSurface() const         { return QColor(0xFF, 0xFF, 0xFF); }
QColor DesignSystem::lightSurfaceElevated() const { return QColor(0xF5, 0xF5, 0xFA); }
QColor DesignSystem::lightSurfaceOverlay() const  { return QColor::fromRgbF(0, 0, 0, 0.3); }

// ============================================================================
// Primitive: Status colors
// ============================================================================
QColor DesignSystem::green500() const  { return QColor(0x34, 0xC7, 0x59); }
QColor DesignSystem::green400() const  { return QColor(0x52, 0xD9, 0x77); }
QColor DesignSystem::amber500() const  { return QColor(0xF5, 0x9E, 0x0B); }
QColor DesignSystem::amber400() const  { return QColor(0xFB, 0xBF, 0x24); }
QColor DesignSystem::amber600() const  { return QColor(0xFF, 0xB8, 0x00); }
QColor DesignSystem::red500() const    { return QColor(0xEF, 0x44, 0x44); }
QColor DesignSystem::red400() const    { return QColor(0xF8, 0x71, 0x71); }
QColor DesignSystem::blue500() const   { return QColor(0x3B, 0x82, 0xF6); }
QColor DesignSystem::blue400() const   { return QColor(0x60, 0xA5, 0xFA); }
QColor DesignSystem::indigo500() const { return QColor(0x58, 0x56, 0xD6); }
QColor DesignSystem::purple400() const { return QColor(0xAF, 0x52, 0xDE); }

// ============================================================================
// Primitive: Recording boundary gradient
// ============================================================================
QColor DesignSystem::boundaryBlue() const   { return QColor(0x00, 0x7A, 0xFF); }
QColor DesignSystem::boundaryPurple() const { return QColor(0xBF, 0x5A, 0xF2); }

// ============================================================================
// Primitive: Accent states
// ============================================================================
QColor DesignSystem::accentDefault() const { return QColor(0x6C, 0x5C, 0xE7); }
QColor DesignSystem::accentHover() const   { return QColor(0x5A, 0x4B, 0xD6); }
QColor DesignSystem::accentPressed() const { return QColor(0x4A, 0x3B, 0xC5); }
QColor DesignSystem::accentLight() const   { return QColor(0xA2, 0x9B, 0xFE); }

// ============================================================================
// Primitive: Spacing
// ============================================================================
int DesignSystem::spacing2() const  { return 2; }
int DesignSystem::spacing4() const  { return 4; }
int DesignSystem::spacing8() const  { return 8; }
int DesignSystem::spacing12() const { return 12; }
int DesignSystem::spacing16() const { return 16; }
int DesignSystem::spacing20() const { return 20; }
int DesignSystem::spacing24() const { return 24; }
int DesignSystem::spacing32() const { return 32; }
int DesignSystem::spacing40() const { return 40; }
int DesignSystem::spacing48() const { return 48; }

// ============================================================================
// Primitive: Border radius
// ============================================================================
int DesignSystem::radiusSmall() const  { return 4; }
int DesignSystem::radiusMedium() const { return 8; }
int DesignSystem::radiusLarge() const  { return 12; }
int DesignSystem::radiusXL() const     { return 16; }

// ============================================================================
// Primitive: Font sizes
// ============================================================================
int DesignSystem::fontSizeDisplay() const   { return 28; }
int DesignSystem::fontSizeH1() const        { return 22; }
int DesignSystem::fontSizeH2() const        { return 18; }
int DesignSystem::fontSizeH3() const        { return 15; }
int DesignSystem::fontSizeBody() const      { return 13; }
int DesignSystem::fontSizeSmallBody() const { return 12; }
int DesignSystem::fontSizeCaption() const   { return 11; }
int DesignSystem::fontSizeSmall() const     { return 10; }

// ============================================================================
// Primitive: Font weights (Qt::Weight enum)
// ============================================================================
int DesignSystem::fontWeightRegular() const  { return QFont::Normal; }
int DesignSystem::fontWeightMedium() const   { return QFont::Medium; }
int DesignSystem::fontWeightSemiBold() const { return QFont::DemiBold; }
int DesignSystem::fontWeightBold() const     { return QFont::Bold; }

// ============================================================================
// Primitive: Letter spacing
// ============================================================================
qreal DesignSystem::letterSpacingTight() const { return -0.2; }
qreal DesignSystem::letterSpacingWide() const  { return 0.5; }

// ============================================================================
// Primitive: Font family
// ============================================================================
QString DesignSystem::fontFamily() const
{
#ifdef Q_OS_MACOS
    return QStringLiteral(".AppleSystemUIFont");
#else
    return QStringLiteral("Segoe UI");
#endif
}

int DesignSystem::fontBaseSize() const { return 13; }

// ============================================================================
// Primitive: Icon sizes
// ============================================================================
int DesignSystem::iconSizeSmall() const  { return 16; }
int DesignSystem::iconSizeMedium() const { return 20; }
int DesignSystem::iconSizeLarge() const  { return 24; }

// ============================================================================
// Primitive: Shadows
// ============================================================================
QColor DesignSystem::lightShadowSmall() const     { return QColor::fromRgbF(0, 0, 0, 0.08); }
int    DesignSystem::lightShadowSmallBlur() const  { return 4; }
int    DesignSystem::lightShadowSmallY() const     { return 1; }
QColor DesignSystem::lightShadowMedium() const    { return QColor::fromRgbF(0, 0, 0, 0.12); }
int    DesignSystem::lightShadowMediumBlur() const { return 8; }
int    DesignSystem::lightShadowMediumY() const    { return 2; }
QColor DesignSystem::lightShadowLarge() const     { return QColor::fromRgbF(0, 0, 0, 0.16); }
int    DesignSystem::lightShadowLargeBlur() const  { return 16; }
int    DesignSystem::lightShadowLargeY() const     { return 4; }
QColor DesignSystem::darkShadowSmall() const      { return QColor::fromRgbF(0, 0, 0, 0.17); }
int    DesignSystem::darkShadowSmallBlur() const   { return 4; }
int    DesignSystem::darkShadowSmallY() const      { return 1; }
QColor DesignSystem::darkShadowMedium() const     { return QColor::fromRgbF(0, 0, 0, 0.25); }
int    DesignSystem::darkShadowMediumBlur() const  { return 8; }
int    DesignSystem::darkShadowMediumY() const     { return 2; }
QColor DesignSystem::darkShadowLarge() const      { return QColor::fromRgbF(0, 0, 0, 0.34); }
int    DesignSystem::darkShadowLargeBlur() const   { return 16; }
int    DesignSystem::darkShadowLargeY() const      { return 4; }

// ============================================================================
// Primitive: Animation durations
// ============================================================================
int DesignSystem::durationInstant() const          { return 0; }
int DesignSystem::durationFast() const             { return 100; }
int DesignSystem::durationNormal() const           { return 200; }
int DesignSystem::durationSlow() const             { return 300; }
int DesignSystem::durationEmphasis() const         { return 400; }
int DesignSystem::durationBoundaryLoop() const     { return 3200; }
int DesignSystem::durationInteractionHover() const { return 100; }
int DesignSystem::durationInteractionPress() const { return 100; }
int DesignSystem::durationInteractionState() const { return 200; }
int DesignSystem::durationInteractionMax() const   { return 200; }

// ============================================================================
// Primitive: Icon system
// ============================================================================
int    DesignSystem::iconStrokeWidth() const { return 2; }
int    DesignSystem::iconGridSize() const    { return 24; }
QColor DesignSystem::iconColorLight() const  { return QColor(0x40, 0x40, 0x40); }
QColor DesignSystem::iconColorDark() const   { return QColor(0xE0, 0xE0, 0xEE); }

// ============================================================================
// Semantic: Background
// ============================================================================
QColor DesignSystem::backgroundPrimary() const  { return m_isDark ? darkSurface() : lightSurface(); }
QColor DesignSystem::backgroundElevated() const { return m_isDark ? darkSurfaceElevated() : lightSurfaceElevated(); }
QColor DesignSystem::backgroundOverlay() const  { return m_isDark ? darkSurfaceOverlay() : lightSurfaceOverlay(); }

// ============================================================================
// Semantic: Text
// ============================================================================
QColor DesignSystem::textPrimary() const   { return m_isDark ? gray200() : gray900(); }
QColor DesignSystem::textSecondary() const { return m_isDark ? gray500() : gray600(); }
QColor DesignSystem::textTertiary() const  { return m_isDark ? gray600() : gray500(); }
QColor DesignSystem::textInverse() const   { return m_isDark ? gray900() : white(); }

// ============================================================================
// Semantic: Border
// ============================================================================
QColor DesignSystem::borderDefault() const { return m_isDark ? gray800() : gray300(); }
QColor DesignSystem::borderFocus() const   { return accentDefault(); }
QColor DesignSystem::borderActive() const  { return accentDefault(); }

// ============================================================================
// Semantic: Status
// ============================================================================
QColor DesignSystem::statusSuccess() const { return m_isDark ? green400() : green500(); }
QColor DesignSystem::statusWarning() const { return m_isDark ? amber400() : amber500(); }
QColor DesignSystem::statusError() const   { return m_isDark ? red400() : red500(); }
QColor DesignSystem::statusInfo() const    { return m_isDark ? blue400() : blue500(); }

// ============================================================================
// Semantic: Toolbar
// ============================================================================
QColor DesignSystem::toolbarBackground() const
{
    QColor base = m_isDark ? gray850() : white();
    base.setAlphaF(m_isDark ? 0.85 : 0.90);
    return base;
}

QColor DesignSystem::toolbarIcon() const       { return m_isDark ? gray300() : gray700(); }
QColor DesignSystem::toolbarIconActive() const  { return white(); }
QColor DesignSystem::toolbarSeparator() const  { return m_isDark ? gray800() : gray300(); }

// ============================================================================
// Semantic: Shadows (theme-dependent)
// ============================================================================
QColor DesignSystem::shadowSmallColor() const  { return m_isDark ? darkShadowSmall() : lightShadowSmall(); }
int    DesignSystem::shadowSmallBlur() const   { return m_isDark ? darkShadowSmallBlur() : lightShadowSmallBlur(); }
int    DesignSystem::shadowSmallY() const      { return m_isDark ? darkShadowSmallY() : lightShadowSmallY(); }
QColor DesignSystem::shadowMediumColor() const { return m_isDark ? darkShadowMedium() : lightShadowMedium(); }
int    DesignSystem::shadowMediumBlur() const  { return m_isDark ? darkShadowMediumBlur() : lightShadowMediumBlur(); }
int    DesignSystem::shadowMediumY() const     { return m_isDark ? darkShadowMediumY() : lightShadowMediumY(); }
QColor DesignSystem::shadowLargeColor() const  { return m_isDark ? darkShadowLarge() : lightShadowLarge(); }
int    DesignSystem::shadowLargeBlur() const   { return m_isDark ? darkShadowLargeBlur() : lightShadowLargeBlur(); }
int    DesignSystem::shadowLargeY() const      { return m_isDark ? darkShadowLargeY() : lightShadowLargeY(); }

// ============================================================================
// Semantic: Capture overlay (always dark)
// ============================================================================
QColor DesignSystem::captureOverlayDim() const             { return dimOverlay(); }
QColor DesignSystem::captureOverlayCrosshair() const       { return crosshair(); }
QColor DesignSystem::captureOverlaySelectionBorder() const { return selectionBorder(); }
QColor DesignSystem::captureOverlayDimensionLabel() const  { return dimensionLabel(); }

// ============================================================================
// Semantic: Icon overlay (theme-dependent)
// ============================================================================
QColor DesignSystem::iconColor() const { return m_isDark ? iconColorDark() : iconColorLight(); }

// ============================================================================
// Component: Button Primary (accent — theme-independent)
// ============================================================================
QColor DesignSystem::buttonPrimaryBackground() const        { return accentDefault(); }
QColor DesignSystem::buttonPrimaryBackgroundHover() const   { return accentHover(); }
QColor DesignSystem::buttonPrimaryBackgroundPressed() const { return accentPressed(); }
QColor DesignSystem::buttonPrimaryText() const              { return white(); }
int    DesignSystem::buttonPrimaryRadius() const            { return radiusMedium(); }

// ============================================================================
// Component: Button Secondary
// ============================================================================
QColor DesignSystem::buttonSecondaryBackground() const        { return backgroundElevated(); }
QColor DesignSystem::buttonSecondaryBackgroundHover() const   { return m_isDark ? gray800() : gray200(); }
QColor DesignSystem::buttonSecondaryBackgroundPressed() const { return m_isDark ? gray700() : gray300(); }
QColor DesignSystem::buttonSecondaryText() const              { return textPrimary(); }
QColor DesignSystem::buttonSecondaryBorder() const            { return borderDefault(); }
int    DesignSystem::buttonSecondaryRadius() const            { return radiusMedium(); }

// ============================================================================
// Component: Button Ghost
// ============================================================================
QColor DesignSystem::buttonGhostBackground() const        { return Qt::transparent; }
QColor DesignSystem::buttonGhostBackgroundHover() const   { return m_isDark ? QColor::fromRgbF(1, 1, 1, 0.08) : QColor::fromRgbF(0, 0, 0, 0.05); }
QColor DesignSystem::buttonGhostBackgroundPressed() const { return m_isDark ? QColor::fromRgbF(1, 1, 1, 0.12) : QColor::fromRgbF(0, 0, 0, 0.08); }
QColor DesignSystem::buttonGhostText() const              { return textPrimary(); }
int    DesignSystem::buttonGhostRadius() const            { return radiusMedium(); }

// ============================================================================
// Component: Button Disabled
// ============================================================================
QColor DesignSystem::buttonDisabledBackground() const { return m_isDark ? gray800() : gray200(); }
QColor DesignSystem::buttonDisabledText() const       { return m_isDark ? gray600() : gray400(); }
QColor DesignSystem::buttonDisabledBorder() const     { return m_isDark ? gray700() : gray300(); }

// ============================================================================
// Component: Toolbar layout
// ============================================================================
int DesignSystem::toolbarHeight() const  { return 32; }
int DesignSystem::toolbarIconSize() const { return 16; }
int DesignSystem::toolbarPadding() const { return spacing8(); }
int DesignSystem::toolbarRadius() const  { return 8; }
QColor DesignSystem::toolbarControlBackground() const
{
    QColor base = backgroundElevated();
    base = m_isDark ? base.darker(112) : base.darker(103);
    base.setAlpha(m_isDark ? 245 : 248);
    return base;
}

QColor DesignSystem::toolbarControlBackgroundHover() const
{
    QColor base = backgroundElevated();
    base = m_isDark ? base.lighter(116) : base.darker(106);
    base.setAlpha(m_isDark ? 248 : 250);
    return base;
}

QColor DesignSystem::toolbarPopupBackground() const
{
    QColor base = backgroundElevated();
    base.setAlpha(m_isDark ? 248 : 245);
    return base;
}

QColor DesignSystem::toolbarPopupBackgroundTop() const
{
    QColor base = m_isDark ? darkSurfaceElevated().lighter(112)
                           : lightSurface().lighter(102);
    base.setAlpha(m_isDark ? 250 : 248);
    return base;
}

QColor DesignSystem::toolbarPopupHighlight() const
{
    return m_isDark ? QColor(255, 255, 255, 18)
                    : QColor(255, 255, 255, 92);
}

QColor DesignSystem::toolbarPopupBorder() const
{
    return m_isDark ? QColor(0x49, 0x49, 0x6A, 230)
                    : QColor(0xD8, 0xDA, 0xE7, 235);
}

QColor DesignSystem::toolbarPopupHover() const
{
    QColor color = accentDefault();
    color.setAlpha(m_isDark ? 34 : 18);
    return color;
}

QColor DesignSystem::toolbarPopupSelected() const
{
    QColor color = accentDefault();
    color.setAlpha(m_isDark ? 88 : 46);
    return color;
}

QColor DesignSystem::toolbarPopupSelectedBorder() const
{
    QColor color = accentDefault();
    color.setAlpha(m_isDark ? 142 : 104);
    return color;
}

// ============================================================================
// Component: Tooltip
// ============================================================================
QColor DesignSystem::tooltipBackground() const    { return m_isDark ? QColor::fromRgbF(0.12, 0.12, 0.12, 0.90) : QColor::fromRgbF(1, 1, 1, 0.94); }
QColor DesignSystem::tooltipBackgroundTop() const { return m_isDark ? QColor::fromRgbF(0.12, 0.12, 0.12, 0.98) : white(); }
QColor DesignSystem::tooltipBorder() const        { return m_isDark ? gray800() : gray300(); }
QColor DesignSystem::tooltipHighlight() const     { return m_isDark ? QColor::fromRgbF(1, 1, 1, 0.06) : QColor::fromRgbF(1, 1, 1, 0.32); }
QColor DesignSystem::tooltipText() const          { return textPrimary(); }
int    DesignSystem::tooltipRadius() const        { return radiusSmall(); }
int    DesignSystem::tooltipPadding() const       { return spacing8(); }

// ============================================================================
// Component: Toast
// ============================================================================
QColor DesignSystem::toastBackground() const  { return m_isDark ? QColor::fromRgbF(0.12, 0.12, 0.12, 0.90) : QColor::fromRgbF(1, 1, 1, 0.92); }
QColor DesignSystem::toastBorder() const      { return m_isDark ? QColor::fromRgbF(1, 1, 1, 0.10) : QColor::fromRgbF(0, 0, 0, 0.06); }
QColor DesignSystem::toastTitleText() const   { return m_isDark ? gray100() : gray900(); }
QColor DesignSystem::toastMessageText() const { return m_isDark ? gray400() : gray600(); }
int DesignSystem::toastRadius() const         { return radiusLarge(); }
int DesignSystem::toastPadding() const        { return spacing16(); }
int DesignSystem::toastShowDuration() const   { return 180; }
int DesignSystem::toastHideDuration() const   { return durationInteractionState(); }
int DesignSystem::toastDisplayDuration() const { return 2500; }
int DesignSystem::toastPaddingH() const       { return 14; }
int DesignSystem::toastPaddingV() const       { return 10; }
int DesignSystem::toastDotSize() const        { return 8; }
int DesignSystem::toastDotLeftMargin() const  { return 14; }
int DesignSystem::toastIconTextGap() const    { return 10; }
int DesignSystem::toastScreenWidth() const    { return 320; }
int DesignSystem::toastTitleFontSize() const  { return fontSizeBody(); }
int DesignSystem::toastMessageFontSize() const { return fontSizeSmallBody(); }
int DesignSystem::toastTitleMessageGap() const { return 3; }

// ============================================================================
// Component: Panel & Dialog
// ============================================================================
QColor DesignSystem::panelBackground() const { return backgroundElevated(); }
QColor DesignSystem::panelBorder() const     { return borderDefault(); }
int    DesignSystem::panelRadius() const     { return radiusLarge(); }
int    DesignSystem::panelPadding() const    { return spacing16(); }
QColor DesignSystem::dialogBackground() const { return backgroundPrimary(); }
QColor DesignSystem::dialogBorder() const     { return borderDefault(); }
QColor DesignSystem::dialogOverlay() const    { return backgroundOverlay(); }
int    DesignSystem::dialogRadius() const     { return radiusXL(); }
int    DesignSystem::dialogPadding() const    { return spacing24(); }

// ============================================================================
// Component: Badge
// ============================================================================
QColor DesignSystem::badgeBackground() const { return m_isDark ? QColor::fromRgbF(0, 0, 0, 0.71) : QColor::fromRgbF(1, 1, 1, 0.78); }
QColor DesignSystem::badgeHighlight() const  { return QColor::fromRgbF(1, 1, 1, 0.04); }
QColor DesignSystem::badgeBorder() const     { return toastBorder(); }
QColor DesignSystem::badgeText() const       { return m_isDark ? white() : QColor::fromRgbF(0.16, 0.16, 0.16, 1.0); }
int DesignSystem::badgeRadius() const        { return 6; }
int DesignSystem::badgePaddingH() const      { return 10; }
int DesignSystem::badgePaddingV() const      { return 5; }
int DesignSystem::badgeFontSize() const      { return 12; }
int DesignSystem::badgeFadeInDuration() const  { return 150; }
int DesignSystem::badgeFadeOutDuration() const { return durationInteractionState(); }
int DesignSystem::badgeDisplayDuration() const { return 1500; }

// ============================================================================
// Component: Settings
// ============================================================================
QColor DesignSystem::settingsSidebarBg() const         { return m_isDark ? QColor::fromRgbF(0.07, 0.07, 0.08, 1.0) : QColor::fromRgbF(0.96, 0.96, 0.97, 1.0); }
QColor DesignSystem::settingsSidebarActiveItem() const { return m_isDark ? QColor::fromRgbF(1, 1, 1, 0.06) : QColor::fromRgbF(0, 0, 0, 0.04); }
QColor DesignSystem::settingsSidebarHoverItem() const  { return m_isDark ? QColor::fromRgbF(1, 1, 1, 0.03) : QColor::fromRgbF(0, 0, 0, 0.02); }
QColor DesignSystem::settingsSectionText() const       { return textSecondary(); }
int DesignSystem::settingsSidebarWidth() const     { return 200; }
int DesignSystem::settingsSidebarMinWidth() const  { return 160; }
int DesignSystem::settingsSidebarMaxWidth() const  { return 220; }
int DesignSystem::settingsContentPadding() const   { return 24; }
int DesignSystem::settingsSectionTopPadding() const { return spacing16(); }
int DesignSystem::settingsColumnSpacing() const    { return spacing4(); }
int DesignSystem::settingsBottomBarHeight() const  { return 52; }
int DesignSystem::settingsBottomBarPaddingH() const { return spacing16(); }
int DesignSystem::settingsToastTopMargin() const   { return spacing12(); }

// ============================================================================
// Component: Form controls
// ============================================================================
QColor DesignSystem::toggleTrackOn() const  { return accentDefault(); }
QColor DesignSystem::toggleTrackOff() const { return m_isDark ? gray700() : gray300(); }
QColor DesignSystem::toggleKnob() const     { return white(); }
int DesignSystem::toggleWidth() const       { return 36; }
int DesignSystem::toggleHeight() const      { return 20; }
int DesignSystem::toggleKnobSize() const    { return 16; }
int DesignSystem::toggleKnobRadius() const  { return radiusMedium(); }
int DesignSystem::toggleKnobInset() const   { return spacing2(); }
int DesignSystem::toggleLabelIndent() const { return 140; }

QColor DesignSystem::inputBackground() const  { return m_isDark ? QColor::fromRgbF(1, 1, 1, 0.04) : QColor::fromRgbF(0, 0, 0, 0.03); }
QColor DesignSystem::inputBorder() const      { return borderDefault(); }
QColor DesignSystem::inputBorderFocus() const { return borderFocus(); }
int    DesignSystem::inputRadius() const      { return radiusSmall(); }
int    DesignSystem::inputHeight() const      { return 32; }

QColor DesignSystem::sliderTrack() const  { return m_isDark ? gray700() : gray300(); }
QColor DesignSystem::sliderFill() const   { return accentDefault(); }
QColor DesignSystem::sliderKnob() const   { return white(); }
int    DesignSystem::sliderHeight() const    { return 4; }
int    DesignSystem::sliderKnobSize() const  { return 14; }

// ============================================================================
// Component: Info Panel & Focus Ring
// ============================================================================
QColor DesignSystem::infoPanelBg() const     { return m_isDark ? QColor::fromRgbF(0.15, 0.22, 0.35, 1.0) : QColor::fromRgbF(0.93, 0.96, 1.0, 1.0); }
QColor DesignSystem::infoPanelBorder() const { return m_isDark ? QColor::fromRgbF(0.25, 0.35, 0.55, 1.0) : QColor::fromRgbF(0.7, 0.8, 0.95, 1.0); }
QColor DesignSystem::infoPanelText() const   { return m_isDark ? QColor::fromRgbF(0.7, 0.82, 1.0, 1.0) : QColor::fromRgbF(0.15, 0.25, 0.5, 1.0); }
QColor DesignSystem::focusRingColor() const  { return borderFocus(); }
int    DesignSystem::focusRingWidth() const  { return 2; }
int    DesignSystem::focusRingOffset() const { return 2; }
int    DesignSystem::focusRingRadius() const { return radiusMedium(); }

// ============================================================================
// Component: Recording Preview
// ============================================================================
QColor DesignSystem::recordingPreviewBgStart() const      { return m_isDark ? gray950() : gray100(); }
QColor DesignSystem::recordingPreviewBgEnd() const        { return m_isDark ? black() : gray200(); }
QColor DesignSystem::recordingPreviewPanel() const        { return m_isDark ? gray900() : white(); }
QColor DesignSystem::recordingPreviewPanelHover() const   { return m_isDark ? gray850() : gray100(); }
QColor DesignSystem::recordingPreviewPanelPressed() const { return m_isDark ? gray800() : gray200(); }
QColor DesignSystem::recordingPreviewBorder() const       { return m_isDark ? gray800() : gray300(); }
QColor DesignSystem::recordingPreviewPlayOverlay() const  { return m_isDark ? QColor::fromRgbF(0, 0, 0, 0.6) : QColor::fromRgbF(0, 0, 0, 0.35); }
QColor DesignSystem::recordingPreviewTrimOverlay() const  { return m_isDark ? QColor::fromRgbF(0, 0, 0, 0.55) : QColor::fromRgbF(0, 0, 0, 0.20); }
QColor DesignSystem::recordingPreviewPlayhead() const     { return white(); }
QColor DesignSystem::recordingPreviewPlayheadBorder() const { return m_isDark ? gray700() : gray400(); }

QColor DesignSystem::recordingPreviewControlHighlight() const
{
    QColor c = accentDefault();
    c.setAlphaF(m_isDark ? 0.18 : 0.12);
    return c;
}

QColor DesignSystem::recordingPreviewDangerHover() const
{
    QColor c = red500();
    c.setAlphaF(m_isDark ? 0.14 : 0.10);
    return c;
}

QColor DesignSystem::recordingPreviewDangerPressed() const
{
    QColor c = red500();
    c.setAlphaF(m_isDark ? 0.24 : 0.18);
    return c;
}

QColor DesignSystem::recordingPreviewPrimaryButton() const        { return accentDefault(); }
QColor DesignSystem::recordingPreviewPrimaryButtonHover() const   { return accentHover(); }
QColor DesignSystem::recordingPreviewPrimaryButtonPressed() const { return accentPressed(); }
QColor DesignSystem::recordingPreviewPrimaryButtonIcon() const    { return white(); }
int    DesignSystem::recordingPreviewControlButtonSize() const    { return 36; }
int    DesignSystem::recordingPreviewControlButtonHeight() const  { return 30; }
int    DesignSystem::recordingPreviewIconSize() const             { return iconSizeToolbar(); }
int    DesignSystem::recordingPreviewActionIconSize() const       { return iconSizeAction(); }

// ============================================================================
// Component: Recording Boundary (always dark)
// ============================================================================
QColor DesignSystem::recordingBoundaryGradientStart() const { return boundaryBlue(); }
QColor DesignSystem::recordingBoundaryGradientMid1() const  { return indigo500(); }
QColor DesignSystem::recordingBoundaryGradientMid2() const  { return purple400(); }
QColor DesignSystem::recordingBoundaryGradientEnd() const   { return boundaryPurple(); }
QColor DesignSystem::recordingBoundaryPaused() const        { return amber600(); }
int    DesignSystem::recordingBoundaryLoopDuration() const  { return durationBoundaryLoop(); }

// ============================================================================
// Component: Countdown Overlay
// ============================================================================
QColor DesignSystem::countdownOverlay() const       { return backgroundOverlay(); }
QColor DesignSystem::countdownText() const          { return white(); }
int    DesignSystem::countdownFontSize() const      { return 200; }
int    DesignSystem::countdownScaleDuration() const { return 800; }
int    DesignSystem::countdownFadeDuration() const  { return 267; }

// ============================================================================
// Component: Icon sizes (semantic aliases)
// ============================================================================
int DesignSystem::iconSizeToolbar() const { return iconSizeMedium(); }
int DesignSystem::iconSizeMenu() const    { return iconSizeSmall(); }
int DesignSystem::iconSizeAction() const  { return iconSizeLarge(); }

// ============================================================================
// Component: Recording Indicator
// ============================================================================
int    DesignSystem::recordingIndicatorDuration() const { return 800; }
QColor DesignSystem::recordingAudioActive() const      { return green500(); }

// ============================================================================
// Component: Recording Control Bar (always dark)
// ============================================================================
QColor DesignSystem::recordingControlBarBg() const         { return QColor::fromRgbF(0, 0, 0, 0.6); }
QColor DesignSystem::recordingControlBarBgTop() const      { return QColor::fromRgbF(0, 0, 0, 0.68); }
QColor DesignSystem::recordingControlBarHighlight() const  { return QColor::fromRgbF(1, 1, 1, 0.06); }
QColor DesignSystem::recordingControlBarBorder() const     { return QColor::fromRgbF(1, 1, 1, 0.12); }
QColor DesignSystem::recordingControlBarText() const       { return white(); }
QColor DesignSystem::recordingControlBarSeparator() const  { return QColor::fromRgbF(1, 1, 1, 0.2); }
QColor DesignSystem::recordingControlBarHoverBg() const    { return QColor::fromRgbF(1, 1, 1, 0.12); }
QColor DesignSystem::recordingControlBarIconNormal() const { return QColor(0xCC, 0xCC, 0xCC); }
QColor DesignSystem::recordingControlBarIconActive() const { return white(); }
QColor DesignSystem::recordingControlBarIconRecord() const { return annotationRed(); }
QColor DesignSystem::recordingControlBarIconCancel() const { return QColor(0xFF, 0x45, 0x3A); }
int DesignSystem::recordingControlBarRadius() const        { return radiusMedium() + 2; }
int DesignSystem::recordingControlBarHeight() const        { return spacing32(); }
int DesignSystem::recordingControlBarButtonSize() const    { return spacing24(); }
int DesignSystem::recordingControlBarButtonSpacing() const { return spacing2(); }
int DesignSystem::recordingControlBarMarginH() const       { return radiusMedium() + 2; }
int DesignSystem::recordingControlBarMarginV() const       { return spacing4(); }
int DesignSystem::recordingControlBarItemSpacing() const   { return 6; }
int DesignSystem::recordingControlBarIndicatorSize() const { return spacing12(); }
int DesignSystem::recordingControlBarFontSize() const      { return fontSizeCaption(); }
int DesignSystem::recordingControlBarFontSizeSmall() const { return fontSizeSmall(); }
int DesignSystem::recordingControlBarIconSize() const      { return iconSizeSmall(); }
int DesignSystem::recordingControlBarButtonRadius() const  { return 6; }

// ============================================================================
// Component: Recording Region Selector (always dark)
// ============================================================================
QColor DesignSystem::recordingRegionOverlayDim() const      { return QColor::fromRgbF(0, 0, 0, 0.39); }
QColor DesignSystem::recordingRegionCrosshair() const       { return QColor::fromRgbF(1, 1, 1, 200.0 / 255.0); }
QColor DesignSystem::recordingRegionGradientStart() const   { return boundaryBlue(); }
QColor DesignSystem::recordingRegionGradientMid() const     { return indigo500(); }
QColor DesignSystem::recordingRegionGradientEnd() const     { return purple400(); }
QColor DesignSystem::recordingRegionGlowColor() const       { return indigo500(); }
QColor DesignSystem::recordingRegionDashColor() const       { return boundaryBlue(); }
QColor DesignSystem::recordingRegionGlassBg() const         { return QColor::fromRgbF(0, 0, 0, 0.85); }
QColor DesignSystem::recordingRegionGlassBgTop() const      { return QColor::fromRgbF(0, 0, 0, 0.92); }
QColor DesignSystem::recordingRegionGlassHighlight() const  { return QColor::fromRgbF(1, 1, 1, 0.06); }
QColor DesignSystem::recordingRegionGlassBorder() const     { return QColor::fromRgbF(1, 1, 1, 0.12); }
QColor DesignSystem::recordingRegionText() const            { return white(); }
QColor DesignSystem::recordingRegionHoverBg() const         { return QColor::fromRgbF(1, 1, 1, 0.12); }
QColor DesignSystem::recordingRegionIconNormal() const      { return QColor(0xCC, 0xCC, 0xCC); }
QColor DesignSystem::recordingRegionIconCancel() const      { return QColor(0xFF, 0x45, 0x3A); }
QColor DesignSystem::recordingRegionIconRecord() const      { return annotationRed(); }
int DesignSystem::recordingRegionActionBarWidth() const        { return 80; }
int DesignSystem::recordingRegionActionBarHeight() const       { return spacing32(); }
int DesignSystem::recordingRegionActionBarButtonSize() const   { return 28; }
int DesignSystem::recordingRegionActionBarButtonHeight() const { return spacing24(); }
int DesignSystem::recordingRegionActionBarRadius() const       { return radiusMedium() + 2; }
int DesignSystem::recordingRegionGlassRadius() const           { return 6; }
int DesignSystem::recordingRegionDimensionRadius() const       { return radiusSmall(); }
int DesignSystem::recordingRegionDimensionFontSize() const     { return fontSizeSmallBody(); }
int DesignSystem::recordingRegionHelpFontSize() const          { return fontSizeBody(); }

// ============================================================================
// Component: Watermark, Combo, Spinner
// ============================================================================
int DesignSystem::watermarkPreviewColumnWidth() const { return 140; }
int DesignSystem::watermarkPreviewSize() const        { return 120; }
int DesignSystem::comboPopupMaxHeight() const         { return 300; }
int DesignSystem::spinnerDuration() const             { return 900; }
