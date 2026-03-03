#include "ui/UnifiedToast.h"
#include "ui/DesignTokens.h"
#include "GlassRenderer.h"
#include "IconRenderer.h"
#include "ToolbarStyle.h"
#include "platform/WindowLevel.h"

#include <QPainter>
#include <QPainterPath>
#include <QGuiApplication>
#include <QScreen>
#include <QFontMetrics>

namespace SnapTray {

// ============================================================================
// Screen-level singleton
// ============================================================================

UnifiedToast& UnifiedToast::screenToast()
{
    static UnifiedToast instance;
    return instance;
}

// Private constructor for screen-level toast
UnifiedToast::UnifiedToast()
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_anchorMode(AnchorMode::ScreenTopRight)
    , m_defaultAnchorMode(AnchorMode::ScreenTopRight)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    setupAnimations();
}

// Parent-anchored constructor
UnifiedToast::UnifiedToast(QWidget* parent, int shadowMargin)
    : QWidget(parent)
    , m_anchorMode(AnchorMode::ParentTopCenter)
    , m_defaultAnchorMode(AnchorMode::ParentTopCenter)
    , m_shadowMargin(shadowMargin)
{
    setAttribute(Qt::WA_TranslucentBackground);
    hide();
    setupAnimations();
}

UnifiedToast::~UnifiedToast() = default;

void UnifiedToast::setupAnimations()
{
    m_displayTimer = new QTimer(this);
    m_displayTimer->setSingleShot(true);
    connect(m_displayTimer, &QTimer::timeout, this, &UnifiedToast::animateOut);

    // Show animation group: fade in + slide down
    auto* showFade = new QPropertyAnimation(this, "fadeOpacity", this);
    showFade->setDuration(200);
    showFade->setEasingCurve(QEasingCurve::OutCubic);

    auto* showSlide = new QPropertyAnimation(this, "slideOffset", this);
    showSlide->setDuration(200);
    showSlide->setEasingCurve(QEasingCurve::OutCubic);

    m_showGroup = new QParallelAnimationGroup(this);
    m_showGroup->addAnimation(showFade);
    m_showGroup->addAnimation(showSlide);

    // Hide animation group: fade out + slide up
    auto* hideFade = new QPropertyAnimation(this, "fadeOpacity", this);
    hideFade->setDuration(250);
    hideFade->setEasingCurve(QEasingCurve::InCubic);

    auto* hideSlide = new QPropertyAnimation(this, "slideOffset", this);
    hideSlide->setDuration(250);
    hideSlide->setEasingCurve(QEasingCurve::InCubic);

    m_hideGroup = new QParallelAnimationGroup(this);
    m_hideGroup->addAnimation(hideFade);
    m_hideGroup->addAnimation(hideSlide);
    connect(m_hideGroup, &QAbstractAnimation::finished,
            this, &UnifiedToast::onHideFinished);
}

// ============================================================================
// Public API
// ============================================================================

void UnifiedToast::showToast(Level level, const QString& title,
                             const QString& message, int durationMs)
{
    m_anchorMode = m_defaultAnchorMode;
    m_currentLevel = level;
    m_currentTitle = title;
    m_currentMessage = message;
    m_anchorRect = QRect();
    displayCurrent();
    m_displayTimer->start(qMax(durationMs, 100));
}

void UnifiedToast::showNearRect(Level level, const QString& title,
                                const QRect& anchorRect, int durationMs)
{
    m_anchorMode = AnchorMode::NearRect;
    m_currentLevel = level;
    m_currentTitle = title;
    m_currentMessage.clear();
    m_anchorRect = anchorRect;
    displayCurrent();
    m_displayTimer->start(qMax(durationMs, 100));
}

// ============================================================================
// Display logic
// ============================================================================

void UnifiedToast::displayCurrent()
{
    ensureIconsLoaded();

    QSize toastSize = calculateToastSize();
    setFixedSize(toastSize);

    positionToast();
    animateIn();
}

QSize UnifiedToast::calculateToastSize() const
{
    bool isScreenLevel = (m_anchorMode == AnchorMode::ScreenTopRight);
    int toastWidth = isScreenLevel ? kScreenToastWidth : 0;

    int contentLeft = kAccentStripWidth + kPaddingH + kIconSize + kIconTextGap;
    int contentRight = kPaddingH;

    QFont titleFont;
    titleFont.setPixelSize(kTitleFontSize);
    titleFont.setWeight(QFont::DemiBold);
    QFontMetrics titleFm(titleFont);

    if (isScreenLevel) {
        // Fixed width — wrap text
        int textWidth = toastWidth - contentLeft - contentRight;
        int textHeight = titleFm.boundingRect(0, 0, textWidth, 0,
                                              Qt::TextWordWrap, m_currentTitle).height();

        if (!m_currentMessage.isEmpty()) {
            QFont msgFont;
            msgFont.setPixelSize(kMessageFontSize);
            QFontMetrics msgFm(msgFont);
            textHeight += kTitleMessageGap;
            textHeight += msgFm.boundingRect(0, 0, textWidth, 0,
                                             Qt::TextWordWrap, m_currentMessage).height();
        }

        int totalHeight = qMax(kIconSize, textHeight) + kPaddingV * 2;
        return QSize(toastWidth, totalHeight);
    } else {
        // Auto-width — single line
        int textWidth = titleFm.horizontalAdvance(m_currentTitle);
        toastWidth = contentLeft + textWidth + contentRight;
        int totalHeight = qMax(kIconSize, titleFm.height()) + kPaddingV * 2;
        return QSize(toastWidth, totalHeight);
    }
}

void UnifiedToast::positionToast()
{
    switch (m_anchorMode) {
    case AnchorMode::ScreenTopRight: {
        QScreen* screen = QGuiApplication::primaryScreen();
        if (!screen) return;
        QRect geo = screen->availableGeometry();
        int x = geo.right() - width() - kScreenMargin;
        int y = geo.top() + kScreenMargin;
        move(x, y);
        break;
    }
    case AnchorMode::ParentTopCenter: {
        if (!parentWidget()) return;
        int x = (parentWidget()->width() - width()) / 2;
        int y = m_shadowMargin + 12;
        move(x, y);
        break;
    }
    case AnchorMode::NearRect: {
        if (m_anchorRect.isNull()) {
            // Fall back to parent center if anchor rect is unavailable
            if (parentWidget()) {
                int x = (parentWidget()->width() - width()) / 2;
                int y = 12;
                move(x, y);
            }
            break;
        }
        int x = m_anchorRect.center().x() - width() / 2;
        int y = m_anchorRect.top() + 12;
        // Clamp within parent bounds
        if (parentWidget()) {
            x = qBound(4, x, parentWidget()->width() - width() - 4);
            y = qBound(4, y, parentWidget()->height() - height() - 4);
        }
        move(x, y);
        break;
    }
    }
}

// ============================================================================
// Animation
// ============================================================================

void UnifiedToast::animateIn()
{
    m_showGroup->stop();
    m_hideGroup->stop();

    auto* showFade = qobject_cast<QPropertyAnimation*>(m_showGroup->animationAt(0));
    auto* showSlide = qobject_cast<QPropertyAnimation*>(m_showGroup->animationAt(1));
    if (!showFade || !showSlide) {
        show();
        raise();
        return;
    }

    // Configure show: fade 0→1, slide -8→0
    showFade->setStartValue(0.0);
    showFade->setEndValue(1.0);
    showSlide->setStartValue(-16);
    showSlide->setEndValue(0);

    show();
    raise();

    if (m_anchorMode == AnchorMode::ScreenTopRight) {
        setWindowFloatingWithoutFocus(this);
    }

    m_showGroup->start();
}

void UnifiedToast::animateOut()
{
    m_displayTimer->stop();
    m_showGroup->stop();

    auto* hideFade = qobject_cast<QPropertyAnimation*>(m_hideGroup->animationAt(0));
    auto* hideSlide = qobject_cast<QPropertyAnimation*>(m_hideGroup->animationAt(1));
    if (!hideFade || !hideSlide) {
        hide();
        return;
    }

    hideFade->setStartValue(m_fadeOpacity);
    hideFade->setEndValue(0.0);
    hideSlide->setStartValue(m_slideOffset);
    hideSlide->setEndValue(-10);

    m_hideGroup->start();
}

void UnifiedToast::onHideFinished()
{
    hide();
    m_anchorMode = m_defaultAnchorMode;
}

void UnifiedToast::setFadeOpacity(qreal opacity)
{
    m_fadeOpacity = qBound(0.0, opacity, 1.0);
    update();
}

void UnifiedToast::setSlideOffset(int offset)
{
    m_slideOffset = offset;
    update();
}

// ============================================================================
// Painting
// ============================================================================

void UnifiedToast::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(m_fadeOpacity);
    painter.translate(0, m_slideOffset);

    paintToast(painter);
}

void UnifiedToast::paintToast(QPainter& painter)
{
    const auto& tokens = DesignTokens::current();
    QRect toastRect(0, 0, width(), height());

    // 1. Glass background
    GlassRenderer::drawGlassPanel(painter, toastRect,
                                  tokens.toastBackground,
                                  tokens.toastHighlight,
                                  tokens.toastBorder,
                                  kCornerRadius);

    // 2. Accent strip on the left edge
    QColor accent = accentColorForLevel(m_currentLevel);
    {
        QPainterPath stripPath;
        qreal r = kCornerRadius;
        qreal w = kAccentStripWidth;
        qreal top = toastRect.top();
        qreal bot = toastRect.bottom() + 1;
        qreal left = toastRect.left();

        // Top-left rounded corner → straight left edge → bottom-left rounded corner
        stripPath.moveTo(left + r, top);
        stripPath.lineTo(left + w, top);
        stripPath.lineTo(left + w, bot);
        stripPath.lineTo(left + r, bot);
        stripPath.arcTo(QRectF(left, bot - r * 2, r * 2, r * 2), 270, -90);
        stripPath.lineTo(left, top + r);
        stripPath.arcTo(QRectF(left, top, r * 2, r * 2), 180, -90);
        stripPath.closeSubpath();

        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        painter.drawPath(stripPath);
    }

    // 3. Icon
    int iconX = toastRect.left() + kAccentStripWidth + kPaddingH;
    int iconY = toastRect.top() + kPaddingV;
    QRect iconRect(iconX, iconY, kIconSize, kIconSize);

    QString iconKey = iconKeyForLevel(m_currentLevel);
    IconRenderer::instance().renderIcon(painter, iconRect, iconKey, accent, 0);

    // 4. Text
    int textX = iconX + kIconSize + kIconTextGap;
    int textRight = toastRect.right() - kPaddingH;
    int textWidth = textRight - textX;
    int textY = toastRect.top() + kPaddingV;

    // Title
    QFont titleFont = painter.font();
    titleFont.setPixelSize(kTitleFontSize);
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(tokens.toastTitleColor);

    QRect titleBound(textX, textY, textWidth, toastRect.height() - kPaddingV * 2);
    QRect titleActual;
    painter.drawText(titleBound, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                     m_currentTitle, &titleActual);

    // Message (if present)
    if (!m_currentMessage.isEmpty()) {
        QFont msgFont = painter.font();
        msgFont.setPixelSize(kMessageFontSize);
        msgFont.setWeight(QFont::Normal);
        painter.setFont(msgFont);
        painter.setPen(tokens.toastMessageColor);

        int msgY = titleActual.bottom() + kTitleMessageGap;
        QRect msgBound(textX, msgY, textWidth, toastRect.bottom() - msgY - kPaddingV);
        painter.drawText(msgBound, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                         m_currentMessage);
    }
}

// ============================================================================
// Helpers
// ============================================================================

QColor UnifiedToast::accentColorForLevel(Level level) const
{
    const auto& tokens = DesignTokens::current();
    switch (level) {
    case Level::Success: return tokens.successAccent;
    case Level::Error:   return tokens.errorAccent;
    case Level::Warning: return tokens.warningAccent;
    case Level::Info:    return tokens.infoAccent;
    }
    return tokens.infoAccent;
}

QString UnifiedToast::iconKeyForLevel(Level level) const
{
    switch (level) {
    case Level::Success: return QStringLiteral("toast-success");
    case Level::Error:   return QStringLiteral("toast-error");
    case Level::Warning: return QStringLiteral("toast-warning");
    case Level::Info:    return QStringLiteral("toast-info");
    }
    return QStringLiteral("toast-info");
}

void UnifiedToast::ensureIconsLoaded()
{
    if (m_iconsLoaded) return;

    bool allLoaded = true;
    for (const auto& key : {QStringLiteral("toast-success"),
                            QStringLiteral("toast-error"),
                            QStringLiteral("toast-warning"),
                            QStringLiteral("toast-info")}) {
        if (!IconRenderer::instance().loadIconByKey(key)) {
            qWarning() << "UnifiedToast: Failed to load icon:" << key;
            allLoaded = false;
        }
    }
    m_iconsLoaded = allLoaded;
}

} // namespace SnapTray
