#include "ColorAndWidthWidget.h"
#include "ui/sections/ColorSection.h"
#include "ui/sections/WidthSection.h"
#include "ui/sections/TextSection.h"
#include "ui/sections/ArrowStyleSection.h"
#include "ui/sections/LineStyleSection.h"
#include "ui/sections/ShapeSection.h"
#include "ui/sections/SizeSection.h"

#include <QPainter>
#include <QDebug>

ColorAndWidthWidget::ColorAndWidthWidget(QObject* parent)
    : QObject(parent)
    , m_colorSection(new ColorSection(this))
    , m_widthSection(new WidthSection(this))
    , m_textSection(new TextSection(this))
    , m_arrowStyleSection(new ArrowStyleSection(this))
    , m_lineStyleSection(new LineStyleSection(this))
    , m_shapeSection(new ShapeSection(this))
    , m_sizeSection(new SizeSection(this))
    , m_styleConfig(ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle()))
{
    connectSectionSignals();
}

ColorAndWidthWidget::~ColorAndWidthWidget()
{
    // Sections are QObject children, automatically deleted
}

void ColorAndWidthWidget::connectSectionSignals()
{
    // Forward ColorSection signals
    connect(m_colorSection, &ColorSection::colorSelected, this, [this](const QColor& color) {
        m_widthSection->setPreviewColor(color);
        emit colorSelected(color);
        qDebug() << "ColorAndWidthWidget: Color selected:" << color.name();
    });
    connect(m_colorSection, &ColorSection::moreColorsRequested, this, &ColorAndWidthWidget::moreColorsRequested);

    // Forward WidthSection signals
    connect(m_widthSection, &WidthSection::widthChanged, this, [this](int width) {
        emit widthChanged(width);
        qDebug() << "ColorAndWidthWidget: Width changed to" << width << "(scroll)";
    });

    // Forward TextSection signals
    connect(m_textSection, &TextSection::boldToggled, this, [this](bool enabled) {
        emit boldToggled(enabled);
        qDebug() << "ColorAndWidthWidget: Bold toggled:" << enabled;
    });
    connect(m_textSection, &TextSection::italicToggled, this, [this](bool enabled) {
        emit italicToggled(enabled);
        qDebug() << "ColorAndWidthWidget: Italic toggled:" << enabled;
    });
    connect(m_textSection, &TextSection::underlineToggled, this, [this](bool enabled) {
        emit underlineToggled(enabled);
        qDebug() << "ColorAndWidthWidget: Underline toggled:" << enabled;
    });
    connect(m_textSection, &TextSection::fontSizeChanged, this, &ColorAndWidthWidget::fontSizeChanged);
    connect(m_textSection, &TextSection::fontFamilyChanged, this, &ColorAndWidthWidget::fontFamilyChanged);
    connect(m_textSection, &TextSection::fontSizeDropdownRequested, this, &ColorAndWidthWidget::fontSizeDropdownRequested);
    connect(m_textSection, &TextSection::fontFamilyDropdownRequested, this, &ColorAndWidthWidget::fontFamilyDropdownRequested);

    // Forward ArrowStyleSection signals
    connect(m_arrowStyleSection, &ArrowStyleSection::arrowStyleChanged, this, &ColorAndWidthWidget::arrowStyleChanged);
    connect(m_arrowStyleSection, &ArrowStyleSection::polylineModeChanged, this, &ColorAndWidthWidget::polylineModeChanged);

    // Forward LineStyleSection signals
    connect(m_lineStyleSection, &LineStyleSection::lineStyleChanged, this, &ColorAndWidthWidget::lineStyleChanged);

    // Forward ShapeSection signals
    connect(m_shapeSection, &ShapeSection::shapeTypeChanged, this, [this](ShapeType type) {
        emit shapeTypeChanged(type);
        qDebug() << "ColorAndWidthWidget: Shape type changed to" << static_cast<int>(type);
    });
    connect(m_shapeSection, &ShapeSection::shapeFillModeChanged, this, [this](ShapeFillMode mode) {
        emit shapeFillModeChanged(mode);
        qDebug() << "ColorAndWidthWidget: Shape fill mode toggled to" << static_cast<int>(mode);
    });

    // Forward SizeSection signals
    connect(m_sizeSection, &SizeSection::sizeChanged, this, [this](StepBadgeSize size) {
        emit stepBadgeSizeChanged(size);
        qDebug() << "ColorAndWidthWidget: Step badge size changed to" << static_cast<int>(size);
    });
}

// =============================================================================
// Color Methods
// =============================================================================

void ColorAndWidthWidget::setColors(const QVector<QColor>& colors)
{
    m_colorSection->setColors(colors);
}

void ColorAndWidthWidget::setCurrentColor(const QColor& color)
{
    m_colorSection->setCurrentColor(color);
    m_widthSection->setPreviewColor(color);
}

QColor ColorAndWidthWidget::currentColor() const
{
    return m_colorSection->currentColor();
}

void ColorAndWidthWidget::setShowMoreButton(bool show)
{
    m_colorSection->setShowMoreButton(show);
}

void ColorAndWidthWidget::setShowColorSection(bool show)
{
    m_showColorSection = show;
}

// =============================================================================
// Width Methods
// =============================================================================

void ColorAndWidthWidget::setShowWidthSection(bool show)
{
    m_showWidthSection = show;
}

void ColorAndWidthWidget::setWidthRange(int min, int max)
{
    m_widthSection->setWidthRange(min, max);
}

void ColorAndWidthWidget::setCurrentWidth(int width)
{
    m_widthSection->setCurrentWidth(width);
}

int ColorAndWidthWidget::currentWidth() const
{
    return m_widthSection->currentWidth();
}

// =============================================================================
// Text Formatting Methods
// =============================================================================

void ColorAndWidthWidget::setShowTextSection(bool show)
{
    m_showTextSection = show;
}

void ColorAndWidthWidget::setBold(bool enabled)
{
    m_textSection->setBold(enabled);
}

bool ColorAndWidthWidget::isBold() const
{
    return m_textSection->isBold();
}

void ColorAndWidthWidget::setItalic(bool enabled)
{
    m_textSection->setItalic(enabled);
}

bool ColorAndWidthWidget::isItalic() const
{
    return m_textSection->isItalic();
}

void ColorAndWidthWidget::setUnderline(bool enabled)
{
    m_textSection->setUnderline(enabled);
}

bool ColorAndWidthWidget::isUnderline() const
{
    return m_textSection->isUnderline();
}

void ColorAndWidthWidget::setFontSize(int size)
{
    m_textSection->setFontSize(size);
}

int ColorAndWidthWidget::fontSize() const
{
    return m_textSection->fontSize();
}

void ColorAndWidthWidget::setFontFamily(const QString& family)
{
    m_textSection->setFontFamily(family);
}

QString ColorAndWidthWidget::fontFamily() const
{
    return m_textSection->fontFamily();
}

// =============================================================================
// Arrow Style Methods
// =============================================================================

void ColorAndWidthWidget::setShowArrowStyleSection(bool show)
{
    m_showArrowStyleSection = show;
}

void ColorAndWidthWidget::setArrowStyle(LineEndStyle style)
{
    m_arrowStyleSection->setArrowStyle(style);
}

LineEndStyle ColorAndWidthWidget::arrowStyle() const
{
    return m_arrowStyleSection->arrowStyle();
}

// =============================================================================
// Polyline Mode Methods
// =============================================================================

void ColorAndWidthWidget::setPolylineMode(bool enabled)
{
    m_arrowStyleSection->setPolylineMode(enabled);
}

bool ColorAndWidthWidget::polylineMode() const
{
    return m_arrowStyleSection->polylineMode();
}

// =============================================================================
// Line Style Methods
// =============================================================================

void ColorAndWidthWidget::setShowLineStyleSection(bool show)
{
    m_showLineStyleSection = show;
}

void ColorAndWidthWidget::setLineStyle(LineStyle style)
{
    m_lineStyleSection->setLineStyle(style);
}

LineStyle ColorAndWidthWidget::lineStyle() const
{
    return m_lineStyleSection->lineStyle();
}

// =============================================================================
// Shape Methods
// =============================================================================

void ColorAndWidthWidget::setShowShapeSection(bool show)
{
    m_showShapeSection = show;
}

void ColorAndWidthWidget::setShapeType(ShapeType type)
{
    m_shapeSection->setShapeType(type);
}

ShapeType ColorAndWidthWidget::shapeType() const
{
    return m_shapeSection->shapeType();
}

void ColorAndWidthWidget::setShapeFillMode(ShapeFillMode mode)
{
    m_shapeSection->setShapeFillMode(mode);
}

ShapeFillMode ColorAndWidthWidget::shapeFillMode() const
{
    return m_shapeSection->shapeFillMode();
}

// =============================================================================
// Size Methods (Step Badge)
// =============================================================================

void ColorAndWidthWidget::setShowSizeSection(bool show)
{
    m_showSizeSection = show;
}

void ColorAndWidthWidget::setStepBadgeSize(StepBadgeSize size)
{
    m_sizeSection->setSize(size);
}

StepBadgeSize ColorAndWidthWidget::stepBadgeSize() const
{
    return m_sizeSection->size();
}

// =============================================================================
// Positioning
// =============================================================================

void ColorAndWidthWidget::updatePosition(const QRect& anchorRect, bool above, int screenWidth)
{
    // Calculate total width based on visible sections
    int totalWidth = 0;
    bool hasFirstSection = false;

    if (m_showColorSection) {
        totalWidth = m_colorSection->preferredWidth();
        hasFirstSection = true;
    }

    if (m_showWidthSection) {
        if (hasFirstSection) {
            totalWidth += SECTION_SPACING;
        }
        totalWidth += m_widthSection->preferredWidth();
        hasFirstSection = true;
    }
    if (m_showArrowStyleSection) {
        if (hasFirstSection) totalWidth += SECTION_SPACING;
        totalWidth += m_arrowStyleSection->preferredWidth();
        hasFirstSection = true;
    }
    if (m_showLineStyleSection) {
        if (hasFirstSection) totalWidth += SECTION_SPACING;
        totalWidth += m_lineStyleSection->preferredWidth();
        hasFirstSection = true;
    }
    if (m_showTextSection) {
        if (hasFirstSection) totalWidth += SECTION_SPACING;
        totalWidth += m_textSection->preferredWidth();
        hasFirstSection = true;
    }
    if (m_showShapeSection) {
        if (hasFirstSection) totalWidth += SECTION_SPACING;
        totalWidth += m_shapeSection->preferredWidth();
        hasFirstSection = true;
    }
    if (m_showSizeSection) {
        if (hasFirstSection) totalWidth += SECTION_SPACING;
        totalWidth += m_sizeSection->preferredWidth();
        hasFirstSection = true;
    }

    // Add right margin for visual balance
    if (hasFirstSection) {
        totalWidth += WIDGET_RIGHT_MARGIN;
    }

    int widgetX = anchorRect.left();
    int widgetY;

    if (above) {
        widgetY = anchorRect.top() - WIDGET_HEIGHT - 4;
    } else {
        widgetY = anchorRect.bottom() + 4;
    }

    // Keep on screen - left boundary
    if (widgetX < 5) widgetX = 5;

    // Keep on screen - right boundary
    if (screenWidth > 0) {
        int maxX = screenWidth - totalWidth - 5;
        if (widgetX > maxX) widgetX = maxX;
    }

    // Keep on screen - top boundary (fallback to below)
    if (widgetY < 5 && above) {
        widgetY = anchorRect.bottom() + 4;
    }

    m_widgetRect = QRect(widgetX, widgetY, totalWidth, WIDGET_HEIGHT);
    m_dropdownExpandsUpward = above;

    // Update dropdown section directions
    m_arrowStyleSection->setDropdownExpandsUpward(above);
    m_lineStyleSection->setDropdownExpandsUpward(above);

    updateLayout();
}

void ColorAndWidthWidget::updateLayout()
{
    int xOffset = m_widgetRect.left();
    bool hasFirstSection = false;

    // Color section
    if (m_showColorSection) {
        m_colorSection->updateLayout(m_widgetRect.top(), WIDGET_HEIGHT, xOffset);
        xOffset += m_colorSection->preferredWidth();
        hasFirstSection = true;
    }

    // Width section
    if (m_showWidthSection) {
        if (hasFirstSection) xOffset += SECTION_SPACING;
        m_widthSection->updateLayout(m_widgetRect.top(), WIDGET_HEIGHT, xOffset);
        xOffset += m_widthSection->preferredWidth();
        hasFirstSection = true;
    }

    // Arrow style section
    if (m_showArrowStyleSection) {
        if (hasFirstSection) xOffset += SECTION_SPACING;
        m_arrowStyleSection->updateLayout(m_widgetRect.top(), WIDGET_HEIGHT, xOffset);
        xOffset += m_arrowStyleSection->preferredWidth();
        hasFirstSection = true;
    }

    // Line style section
    if (m_showLineStyleSection) {
        if (hasFirstSection) xOffset += SECTION_SPACING;
        m_lineStyleSection->updateLayout(m_widgetRect.top(), WIDGET_HEIGHT, xOffset);
        xOffset += m_lineStyleSection->preferredWidth();
        hasFirstSection = true;
    }

    // Text section
    if (m_showTextSection) {
        if (hasFirstSection) xOffset += SECTION_SPACING;
        m_textSection->updateLayout(m_widgetRect.top(), WIDGET_HEIGHT, xOffset);
        xOffset += m_textSection->preferredWidth();
        hasFirstSection = true;
    }

    // Shape section
    if (m_showShapeSection) {
        if (hasFirstSection) xOffset += SECTION_SPACING;
        m_shapeSection->updateLayout(m_widgetRect.top(), WIDGET_HEIGHT, xOffset);
        xOffset += m_shapeSection->preferredWidth();
        hasFirstSection = true;
    }

    // Size section (Step Badge)
    if (m_showSizeSection) {
        if (hasFirstSection) xOffset += SECTION_SPACING;
        m_sizeSection->updateLayout(m_widgetRect.top(), WIDGET_HEIGHT, xOffset);
    }
}

// =============================================================================
// Rendering
// =============================================================================

void ColorAndWidthWidget::draw(QPainter& painter)
{
    if (!m_visible) return;

    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw unified shadow
    QRect shadowRect = m_widgetRect.adjusted(2, 2, 2, 2);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, m_styleConfig.shadowAlpha));
    painter.drawRoundedRect(shadowRect, 6, 6);

    // Draw unified background with gradient
    QLinearGradient gradient(m_widgetRect.topLeft(), m_widgetRect.bottomLeft());
    gradient.setColorAt(0, m_styleConfig.backgroundColorTop);
    gradient.setColorAt(1, m_styleConfig.backgroundColorBottom);
    painter.setBrush(gradient);
    painter.setPen(QPen(m_styleConfig.borderColor, 1));
    painter.drawRoundedRect(m_widgetRect, 6, 6);

    // Draw sections
    if (m_showColorSection) {
        m_colorSection->draw(painter, m_styleConfig);
    }

    if (m_showWidthSection) {
        m_widthSection->draw(painter, m_styleConfig);
    }

    if (m_showArrowStyleSection) {
        m_arrowStyleSection->draw(painter, m_styleConfig);
    }

    if (m_showLineStyleSection) {
        m_lineStyleSection->draw(painter, m_styleConfig);
    }

    if (m_showTextSection) {
        m_textSection->draw(painter, m_styleConfig);
    }

    if (m_showShapeSection) {
        m_shapeSection->draw(painter, m_styleConfig);
    }

    if (m_showSizeSection) {
        m_sizeSection->draw(painter, m_styleConfig);
    }
}

// =============================================================================
// Event Handling
// =============================================================================

bool ColorAndWidthWidget::contains(const QPoint& pos) const
{
    if (m_widgetRect.contains(pos)) return true;
    // Also include arrow style dropdown area when open
    if (m_showArrowStyleSection && m_arrowStyleSection->isDropdownOpen() &&
        m_arrowStyleSection->dropdownRect().contains(pos)) {
        return true;
    }
    // Also include line style dropdown area when open
    if (m_showLineStyleSection && m_lineStyleSection->isDropdownOpen() &&
        m_lineStyleSection->dropdownRect().contains(pos)) {
        return true;
    }
    return false;
}

bool ColorAndWidthWidget::handleClick(const QPoint& pos)
{
    // Handle dropdown clicks first (even outside main widget)
    if (m_showArrowStyleSection) {
        if (m_arrowStyleSection->handleClick(pos)) {
            return true;
        }
    }
    if (m_showLineStyleSection) {
        if (m_lineStyleSection->handleClick(pos)) {
            return true;
        }
    }

    if (!m_widgetRect.contains(pos)) return false;

    // Text section
    if (m_showTextSection && m_textSection->contains(pos)) {
        return m_textSection->handleClick(pos);
    }

    // Shape section
    if (m_showShapeSection && m_shapeSection->contains(pos)) {
        return m_shapeSection->handleClick(pos);
    }

    // Size section (Step Badge)
    if (m_showSizeSection && m_sizeSection->contains(pos)) {
        return m_sizeSection->handleClick(pos);
    }

    // Width section
    if (m_showWidthSection && m_widthSection->contains(pos)) {
        return m_widthSection->handleClick(pos);
    }

    // Color section
    if (m_showColorSection && m_colorSection->contains(pos)) {
        return m_colorSection->handleClick(pos);
    }

    return false;
}

bool ColorAndWidthWidget::handleMousePress(const QPoint& pos)
{
    return handleClick(pos);
}

bool ColorAndWidthWidget::handleMouseMove(const QPoint& pos, bool pressed)
{
    Q_UNUSED(pressed);
    if (!m_visible) return false;
    return updateHovered(pos);
}

bool ColorAndWidthWidget::handleMouseRelease(const QPoint& pos)
{
    Q_UNUSED(pos);
    return false;
}

bool ColorAndWidthWidget::updateHovered(const QPoint& pos)
{
    bool changed = false;

    if (m_showColorSection) {
        changed |= m_colorSection->updateHovered(pos);
    }

    if (m_showWidthSection) {
        changed |= m_widthSection->updateHovered(pos);
    }

    if (m_showArrowStyleSection) {
        changed |= m_arrowStyleSection->updateHovered(pos);
    }

    if (m_showLineStyleSection) {
        changed |= m_lineStyleSection->updateHovered(pos);
    }

    if (m_showTextSection) {
        changed |= m_textSection->updateHovered(pos);
    }

    if (m_showShapeSection) {
        changed |= m_shapeSection->updateHovered(pos);
    }

    if (m_showSizeSection) {
        changed |= m_sizeSection->updateHovered(pos);
    }

    return changed;
}

bool ColorAndWidthWidget::handleWheel(int delta)
{
    if (!m_showWidthSection) {
        return false;
    }
    return m_widthSection->handleWheel(delta);
}
