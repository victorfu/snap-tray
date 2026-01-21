#include "toolbar/ToolOptionsPanel.h"
#include "GlassRenderer.h"
#include "ui/sections/ColorSection.h"
#include "ui/sections/WidthSection.h"
#include "ui/sections/TextSection.h"
#include "ui/sections/ArrowStyleSection.h"
#include "ui/sections/LineStyleSection.h"
#include "ui/sections/ShapeSection.h"
#include "ui/sections/SizeSection.h"
#include "ui/sections/AutoBlurSection.h"

#include <QPainter>
#include <QtGlobal>

ToolOptionsPanel::ToolOptionsPanel(QObject* parent)
    : QObject(parent)
    , m_colorSection(new ColorSection(this))
    , m_widthSection(new WidthSection(this))
    , m_textSection(new TextSection(this))
    , m_arrowStyleSection(new ArrowStyleSection(this))
    , m_lineStyleSection(new LineStyleSection(this))
    , m_shapeSection(new ShapeSection(this))
    , m_sizeSection(new SizeSection(this))
    , m_autoBlurSection(new AutoBlurSection(this))
    , m_styleConfig(ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle()))
{
    connectSectionSignals();
}

ToolOptionsPanel::~ToolOptionsPanel()
{
    // Sections are QObject children, automatically deleted
}

void ToolOptionsPanel::connectSectionSignals()
{
    // Forward ColorSection signals
    connect(m_colorSection, &ColorSection::colorSelected, this, &ToolOptionsPanel::colorSelected);
    connect(m_colorSection, &ColorSection::customColorPickerRequested, this, &ToolOptionsPanel::customColorPickerRequested);

    // Forward WidthSection signals
    connect(m_widthSection, &WidthSection::widthChanged, this, &ToolOptionsPanel::widthChanged);

    // Forward TextSection signals
    connect(m_textSection, &TextSection::boldToggled, this, &ToolOptionsPanel::boldToggled);
    connect(m_textSection, &TextSection::italicToggled, this, &ToolOptionsPanel::italicToggled);
    connect(m_textSection, &TextSection::underlineToggled, this, &ToolOptionsPanel::underlineToggled);
    connect(m_textSection, &TextSection::fontSizeChanged, this, &ToolOptionsPanel::fontSizeChanged);
    connect(m_textSection, &TextSection::fontFamilyChanged, this, &ToolOptionsPanel::fontFamilyChanged);
    connect(m_textSection, &TextSection::fontSizeDropdownRequested, this, &ToolOptionsPanel::fontSizeDropdownRequested);
    connect(m_textSection, &TextSection::fontFamilyDropdownRequested, this, &ToolOptionsPanel::fontFamilyDropdownRequested);

    // Forward ArrowStyleSection signals
    connect(m_arrowStyleSection, &ArrowStyleSection::arrowStyleChanged, this, &ToolOptionsPanel::arrowStyleChanged);

    // Forward LineStyleSection signals
    connect(m_lineStyleSection, &LineStyleSection::lineStyleChanged, this, &ToolOptionsPanel::lineStyleChanged);

    // Forward ShapeSection signals
    connect(m_shapeSection, &ShapeSection::shapeTypeChanged, this, &ToolOptionsPanel::shapeTypeChanged);
    connect(m_shapeSection, &ShapeSection::shapeFillModeChanged, this, &ToolOptionsPanel::shapeFillModeChanged);

    // Forward SizeSection signals
    connect(m_sizeSection, &SizeSection::sizeChanged, this, &ToolOptionsPanel::stepBadgeSizeChanged);

    // Forward AutoBlurSection signals
    connect(m_autoBlurSection, &AutoBlurSection::autoBlurRequested, this, &ToolOptionsPanel::autoBlurRequested);
}

// =============================================================================
// Color Methods
// =============================================================================

void ToolOptionsPanel::setColors(const QVector<QColor>& colors)
{
    m_colorSection->setColors(colors);
}

void ToolOptionsPanel::setCurrentColor(const QColor& color)
{
    m_colorSection->setCurrentColor(color);
}

QColor ToolOptionsPanel::currentColor() const
{
    return m_colorSection->currentColor();
}

void ToolOptionsPanel::setShowColorSection(bool show)
{
    m_showColorSection = show;
}

// =============================================================================
// Width Methods
// =============================================================================

void ToolOptionsPanel::setShowWidthSection(bool show)
{
    m_showWidthSection = show;
}

void ToolOptionsPanel::setWidthSectionHidden(bool hidden)
{
    m_widthSectionHidden = hidden;
}

void ToolOptionsPanel::setWidthRange(int min, int max)
{
    m_widthSection->setWidthRange(min, max);
}

void ToolOptionsPanel::setCurrentWidth(int width)
{
    m_widthSection->setCurrentWidth(width);
}

int ToolOptionsPanel::currentWidth() const
{
    return m_widthSection->currentWidth();
}

// =============================================================================
// Text Formatting Methods
// =============================================================================

void ToolOptionsPanel::setShowTextSection(bool show)
{
    m_showTextSection = show;
}

void ToolOptionsPanel::setBold(bool enabled)
{
    m_textSection->setBold(enabled);
}

bool ToolOptionsPanel::isBold() const
{
    return m_textSection->isBold();
}

void ToolOptionsPanel::setItalic(bool enabled)
{
    m_textSection->setItalic(enabled);
}

bool ToolOptionsPanel::isItalic() const
{
    return m_textSection->isItalic();
}

void ToolOptionsPanel::setUnderline(bool enabled)
{
    m_textSection->setUnderline(enabled);
}

bool ToolOptionsPanel::isUnderline() const
{
    return m_textSection->isUnderline();
}

void ToolOptionsPanel::setFontSize(int size)
{
    m_textSection->setFontSize(size);
}

int ToolOptionsPanel::fontSize() const
{
    return m_textSection->fontSize();
}

void ToolOptionsPanel::setFontFamily(const QString& family)
{
    m_textSection->setFontFamily(family);
}

QString ToolOptionsPanel::fontFamily() const
{
    return m_textSection->fontFamily();
}

// =============================================================================
// Arrow Style Methods
// =============================================================================

void ToolOptionsPanel::setShowArrowStyleSection(bool show)
{
    m_showArrowStyleSection = show;
}

void ToolOptionsPanel::setArrowStyle(LineEndStyle style)
{
    m_arrowStyleSection->setArrowStyle(style);
}

LineEndStyle ToolOptionsPanel::arrowStyle() const
{
    return m_arrowStyleSection->arrowStyle();
}

// =============================================================================
// Line Style Methods
// =============================================================================

void ToolOptionsPanel::setShowLineStyleSection(bool show)
{
    m_showLineStyleSection = show;
}

void ToolOptionsPanel::setLineStyle(LineStyle style)
{
    m_lineStyleSection->setLineStyle(style);
}

LineStyle ToolOptionsPanel::lineStyle() const
{
    return m_lineStyleSection->lineStyle();
}

// =============================================================================
// Shape Methods
// =============================================================================

void ToolOptionsPanel::setShowShapeSection(bool show)
{
    m_showShapeSection = show;
}

void ToolOptionsPanel::setShapeType(ShapeType type)
{
    m_shapeSection->setShapeType(type);
}

ShapeType ToolOptionsPanel::shapeType() const
{
    return m_shapeSection->shapeType();
}

void ToolOptionsPanel::setShapeFillMode(ShapeFillMode mode)
{
    m_shapeSection->setShapeFillMode(mode);
}

ShapeFillMode ToolOptionsPanel::shapeFillMode() const
{
    return m_shapeSection->shapeFillMode();
}

// =============================================================================
// Size Methods (Step Badge)
// =============================================================================

void ToolOptionsPanel::setShowSizeSection(bool show)
{
    m_showSizeSection = show;
}

void ToolOptionsPanel::setStepBadgeSize(StepBadgeSize size)
{
    m_sizeSection->setSize(size);
}

StepBadgeSize ToolOptionsPanel::stepBadgeSize() const
{
    return m_sizeSection->size();
}

// =============================================================================
// Auto Blur Methods
// =============================================================================

void ToolOptionsPanel::setShowAutoBlurSection(bool show)
{
    m_showAutoBlurSection = show;
}

void ToolOptionsPanel::setAutoBlurEnabled(bool enabled)
{
    m_autoBlurSection->setEnabled(enabled);
}

void ToolOptionsPanel::setAutoBlurProcessing(bool processing)
{
    m_autoBlurSection->setProcessing(processing);
}

// =============================================================================
// Positioning
// =============================================================================

void ToolOptionsPanel::updatePosition(const QRect& anchorRect, bool above, int screenWidth)
{
    // Calculate total width based on visible sections
    int totalWidth = 0;
    bool hasFirstSection = false;

    // Width section first (leftmost) - only if not hidden
    bool widthSectionVisible = m_showWidthSection && !m_widthSectionHidden;
    if (widthSectionVisible) {
        totalWidth = m_widthSection->preferredWidth();
        hasFirstSection = true;
    }

    if (m_showColorSection) {
        if (widthSectionVisible) {
            totalWidth += WIDTH_TO_COLOR_SPACING;  // Smaller spacing between width and color
        } else if (hasFirstSection) {
            totalWidth += SECTION_SPACING;
        }
        totalWidth += m_colorSection->preferredWidth();
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
    if (m_showAutoBlurSection) {
        if (hasFirstSection) totalWidth += SECTION_SPACING;
        totalWidth += m_autoBlurSection->preferredWidth();
        hasFirstSection = true;
    }
    // Add right margin for visual balance
    if (hasFirstSection) {
        totalWidth += WIDGET_RIGHT_MARGIN;
    }

    int widgetHeight = WIDGET_HEIGHT;
    if (m_showColorSection) {
        widgetHeight = qMax(widgetHeight, m_colorSection->preferredHeight());
    }

    int widgetX = anchorRect.left();
    int widgetY;

    if (above) {
        widgetY = anchorRect.top() - widgetHeight - 4;
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

    m_widgetRect = QRect(widgetX, widgetY, totalWidth, widgetHeight);
    m_dropdownExpandsUpward = above;

    // Update dropdown section directions
    m_arrowStyleSection->setDropdownExpandsUpward(above);
    m_lineStyleSection->setDropdownExpandsUpward(above);

    updateLayout();
}

void ToolOptionsPanel::updateLayout()
{
    int containerTop = m_widgetRect.top() + VERTICAL_PADDING;
    int containerHeight = m_widgetRect.height() - 2 * VERTICAL_PADDING;
    int xOffset = m_widgetRect.left();
    bool hasFirstSection = false;

    // Width section first (leftmost) - only if not hidden
    bool widthSectionVisible = m_showWidthSection && !m_widthSectionHidden;
    if (widthSectionVisible) {
        m_widthSection->updateLayout(containerTop, containerHeight, xOffset);
        xOffset += m_widthSection->preferredWidth();
        hasFirstSection = true;
    }

    // Color section
    if (m_showColorSection) {
        if (widthSectionVisible) {
            xOffset += WIDTH_TO_COLOR_SPACING;  // Smaller spacing between width and color
        } else if (hasFirstSection) {
            xOffset += SECTION_SPACING;
        }
        m_colorSection->updateLayout(containerTop, containerHeight, xOffset);
        xOffset += m_colorSection->preferredWidth();
        hasFirstSection = true;
    }

    // Arrow style section
    if (m_showArrowStyleSection) {
        if (hasFirstSection) xOffset += SECTION_SPACING;
        m_arrowStyleSection->updateLayout(containerTop, containerHeight, xOffset);
        xOffset += m_arrowStyleSection->preferredWidth();
        hasFirstSection = true;
    }

    // Line style section
    if (m_showLineStyleSection) {
        if (hasFirstSection) xOffset += SECTION_SPACING;
        m_lineStyleSection->updateLayout(containerTop, containerHeight, xOffset);
        xOffset += m_lineStyleSection->preferredWidth();
        hasFirstSection = true;
    }

    // Text section
    if (m_showTextSection) {
        if (hasFirstSection) xOffset += SECTION_SPACING;
        m_textSection->updateLayout(containerTop, containerHeight, xOffset);
        xOffset += m_textSection->preferredWidth();
        hasFirstSection = true;
    }

    // Shape section
    if (m_showShapeSection) {
        if (hasFirstSection) xOffset += SECTION_SPACING;
        m_shapeSection->updateLayout(containerTop, containerHeight, xOffset);
        xOffset += m_shapeSection->preferredWidth();
        hasFirstSection = true;
    }

    // Size section (Step Badge)
    if (m_showSizeSection) {
        if (hasFirstSection) xOffset += SECTION_SPACING;
        m_sizeSection->updateLayout(containerTop, containerHeight, xOffset);
        xOffset += m_sizeSection->preferredWidth();
        hasFirstSection = true;
    }

    if (m_showAutoBlurSection) {
        if (hasFirstSection) xOffset += SECTION_SPACING;
        m_autoBlurSection->updateLayout(containerTop, containerHeight, xOffset);
        xOffset += m_autoBlurSection->preferredWidth();
    }
}

// =============================================================================
// Rendering
// =============================================================================

void ToolOptionsPanel::draw(QPainter& painter)
{
    if (!m_visible) return;

    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw glass panel (6px radius for sub-widgets) if enabled
    if (m_drawBackground) {
        GlassRenderer::drawGlassPanel(painter, m_widgetRect, m_styleConfig, 6);
    }

    // Draw sections
    if (m_showColorSection) {
        m_colorSection->draw(painter, m_styleConfig);
    }

    if (m_showWidthSection && !m_widthSectionHidden) {
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

    if (m_showAutoBlurSection) {
        m_autoBlurSection->draw(painter, m_styleConfig);
    }
}

// =============================================================================
// Event Handling
// =============================================================================

bool ToolOptionsPanel::contains(const QPoint& pos) const
{
    if (!m_visible) {
        return false;
    }
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

bool ToolOptionsPanel::handleClick(const QPoint& pos)
{
    // Track dropdown state before handling click
    bool wasArrowOpen = m_showArrowStyleSection && m_arrowStyleSection->isDropdownOpen();
    bool wasLineOpen = m_showLineStyleSection && m_lineStyleSection->isDropdownOpen();

    bool handled = false;

    // Handle dropdown clicks first (even outside main widget)
    if (m_showArrowStyleSection) {
        if (m_arrowStyleSection->handleClick(pos)) {
            handled = true;
        }
    }
    if (!handled && m_showLineStyleSection) {
        if (m_lineStyleSection->handleClick(pos)) {
            handled = true;
        }
    }

    // Check if dropdown state changed (emit signal regardless of handled status)
    // This ensures window resizes back when clicking outside to close dropdown
    bool isArrowOpen = m_showArrowStyleSection && m_arrowStyleSection->isDropdownOpen();
    bool isLineOpen = m_showLineStyleSection && m_lineStyleSection->isDropdownOpen();
    if (wasArrowOpen != isArrowOpen || wasLineOpen != isLineOpen) {
        emit dropdownStateChanged();
    }

    if (handled) return true;

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

    // Auto blur section
    if (m_showAutoBlurSection && m_autoBlurSection->contains(pos)) {
        return m_autoBlurSection->handleClick(pos);
    }

    // Width section: consume click but don't trigger color picker
    if (m_showWidthSection && !m_widthSectionHidden && m_widthSection->contains(pos)) {
        return true;
    }

    // Color section
    if (m_showColorSection && m_colorSection->contains(pos)) {
        return m_colorSection->handleClick(pos);
    }

    return false;
}

bool ToolOptionsPanel::handleMousePress(const QPoint& pos)
{
    return handleClick(pos);
}

bool ToolOptionsPanel::handleMouseMove(const QPoint& pos, bool pressed)
{
    Q_UNUSED(pressed);
    if (!m_visible) return false;
    return updateHovered(pos);
}

bool ToolOptionsPanel::handleMouseRelease(const QPoint& pos)
{
    Q_UNUSED(pos);
    return false;
}

bool ToolOptionsPanel::updateHovered(const QPoint& pos)
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

    if (m_showAutoBlurSection) {
        changed |= m_autoBlurSection->updateHovered(pos);
    }

    return changed;
}

bool ToolOptionsPanel::handleWheel(int delta)
{
    if (!m_showWidthSection) {
        return false;
    }
    return m_widthSection->handleWheel(delta);
}

int ToolOptionsPanel::activeDropdownHeight() const
{
    int dropdownHeight = 0;

    if (m_showArrowStyleSection && m_arrowStyleSection->isDropdownOpen()) {
        dropdownHeight = qMax(dropdownHeight, m_arrowStyleSection->dropdownRect().height());
    }

    if (m_showLineStyleSection && m_lineStyleSection->isDropdownOpen()) {
        dropdownHeight = qMax(dropdownHeight, m_lineStyleSection->dropdownRect().height());
    }

    return dropdownHeight;
}
