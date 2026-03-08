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
#include "qml/PinToolOptionsViewModel.h"
#include "qml/QmlOverlayManager.h"

#include <QCoreApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQmlContext>
#include <QWidget>
#include <QtGlobal>

class EmbeddedSharedStyleControl : public QObject
{
public:
    EmbeddedSharedStyleControl(const QString& kind,
                               PinToolOptionsViewModel* viewModel,
                               QWidget* hostWidget,
                               QObject* parent = nullptr)
        : QObject(parent)
        , m_kind(kind)
        , m_viewModel(viewModel)
        , m_hostWidget(hostWidget)
    {
        ensureWidget();
    }

    ~EmbeddedSharedStyleControl() override
    {
        delete m_container;
    }

    QQuickItem* rootItem() const
    {
        return m_rootItem;
    }

    QRect geometry() const
    {
        return m_container ? m_container->geometry() : QRect();
    }

    bool contains(const QPoint& pos) const
    {
        return m_container && m_container->isVisible() && m_container->geometry().contains(pos);
    }

    bool isMenuOpen() const
    {
        return m_rootItem && m_rootItem->property("dropdownOpen").toBool();
    }

    void setAnchorRect(const QRect& rect)
    {
        m_anchorRect = rect;
        updateGeometryFromAnchor();
    }

    void setExpandUpward(bool upward)
    {
        m_expandUpward = upward;
        if (m_rootItem) {
            m_rootItem->setProperty("expandUpward", upward);
        }
        updateGeometryFromAnchor();
    }

    void setSectionVisible(bool visible)
    {
        m_sectionVisible = visible;
        if (!visible) {
            closeMenu();
            hide();
            return;
        }

        updateGeometryFromAnchor();
        show();
    }

    void closeMenu()
    {
        if (m_rootItem) {
            QMetaObject::invokeMethod(m_rootItem, "closeMenu");
        }
    }

    void hide()
    {
        if (m_container)
            m_container->hide();
    }

private:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_quickWidget) {
            if (event->type() == QEvent::MouseButtonPress) {
                forwardMousePressToHost(static_cast<QMouseEvent*>(event));
            } else if (event->type() == QEvent::Resize) {
                syncSizeFromRoot();
                updateGeometryFromAnchor();
            }
        }
        return QObject::eventFilter(watched, event);
    }

    void ensureWidget()
    {
        if (m_container || !m_hostWidget)
            return;

        m_container = new QWidget(m_hostWidget);
        m_container->setObjectName(
            m_kind == QStringLiteral("line")
                ? QStringLiteral("sharedLineStyleControlContainer")
                : QStringLiteral("sharedArrowStyleControlContainer"));
        m_container->setAttribute(Qt::WA_TranslucentBackground);
        m_container->setAttribute(Qt::WA_NoSystemBackground);
        m_container->setAutoFillBackground(false);
        m_container->setFocusPolicy(Qt::NoFocus);
        m_container->hide();

        m_quickWidget = new QQuickWidget(SnapTray::QmlOverlayManager::instance().engine(), m_container);
        m_quickWidget->setObjectName(
            m_kind == QStringLiteral("line")
                ? QStringLiteral("sharedLineStyleQuickWidget")
                : QStringLiteral("sharedArrowStyleQuickWidget"));
        m_quickWidget->setAttribute(Qt::WA_TranslucentBackground);
        m_quickWidget->setAttribute(Qt::WA_AlwaysStackOnTop);
        m_quickWidget->setClearColor(Qt::transparent);
        m_quickWidget->setResizeMode(QQuickWidget::SizeViewToRootObject);
        m_quickWidget->setFocusPolicy(Qt::NoFocus);
        m_quickWidget->installEventFilter(this);
        m_quickWidget->rootContext()->setContextProperty(
            QStringLiteral("sharedStyleDropdownViewModel"), m_viewModel);
        m_quickWidget->setSource(QUrl(QStringLiteral("qrc:/SnapTrayQml/toolbar/SharedStyleDropdownHost.qml")));

        if (m_quickWidget->status() != QQuickWidget::Ready) {
            qWarning() << "SharedStyleDropdownHost QML error:" << m_quickWidget->errors();
            return;
        }

        m_rootItem = qobject_cast<QQuickItem*>(m_quickWidget->rootObject());
        if (!m_rootItem)
            return;

        m_rootItem->setProperty("kind", m_kind);
        m_rootItem->setProperty("expandUpward", m_expandUpward);
        m_rootItem->setProperty("viewModel",
                                QVariant::fromValue(static_cast<QObject*>(m_viewModel)));

        QObject::connect(m_rootItem, &QQuickItem::widthChanged, m_container, [this]() {
            syncSizeFromRoot();
            updateGeometryFromAnchor();
        });
        QObject::connect(m_rootItem, &QQuickItem::heightChanged, m_container, [this]() {
            syncSizeFromRoot();
            updateGeometryFromAnchor();
        });

        syncSizeFromRoot();
    }

    void forwardMousePressToHost(QMouseEvent* event)
    {
        if (!event || !m_hostWidget || !m_container)
            return;

        const QPoint hostPos = m_container->mapTo(m_hostWidget, event->position().toPoint());
        const QPoint globalPos = m_hostWidget->mapToGlobal(hostPos);
        QMouseEvent forwardedEvent(event->type(),
                                   QPointF(hostPos),
                                   QPointF(globalPos),
                                   event->button(),
                                   event->buttons(),
                                   event->modifiers());
        QCoreApplication::sendEvent(m_hostWidget, &forwardedEvent);
    }

    void syncSizeFromRoot()
    {
        if (!m_container || !m_quickWidget || !m_rootItem)
            return;

        const QSize size(qMax(qRound(m_rootItem->width()), qRound(m_rootItem->implicitWidth())),
                         qMax(qRound(m_rootItem->height()), qRound(m_rootItem->implicitHeight())));
        if (!size.isValid() || size.isEmpty())
            return;

        if (m_container->size() != size)
            m_container->setFixedSize(size);
        if (m_quickWidget->size() != size)
            m_quickWidget->setFixedSize(size);
        if (m_quickWidget->pos() != QPoint(0, 0))
            m_quickWidget->move(0, 0);
    }

    void updateGeometryFromAnchor()
    {
        if (!m_container || !m_quickWidget || !m_rootItem)
            return;
        if (!m_sectionVisible || !m_anchorRect.isValid()) {
            hide();
            return;
        }

        syncSizeFromRoot();

        const int offsetX = m_rootItem->property("anchorOffsetX").toInt();
        const int offsetY = m_rootItem->property("anchorOffsetY").toInt();
        const QPoint topLeft = m_anchorRect.topLeft() - QPoint(offsetX, offsetY);
        if (m_container->pos() != topLeft)
            m_container->move(topLeft);

        if (!m_container->isVisible())
            m_container->show();

        m_container->raise();
        m_quickWidget->raise();
    }

    void show()
    {
        if (!m_sectionVisible)
            return;

        updateGeometryFromAnchor();
    }

    QString m_kind;
    PinToolOptionsViewModel* m_viewModel = nullptr;
    QWidget* m_hostWidget = nullptr;
    QPointer<QWidget> m_container;
    QPointer<QQuickWidget> m_quickWidget;
    QPointer<QQuickItem> m_rootItem;
    QRect m_anchorRect;
    bool m_expandUpward = false;
    bool m_sectionVisible = false;
};

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
    if (m_useSharedStyleDropdowns) {
        if (QWidget* widget = hostWidget())
            widget->removeEventFilter(this);
    }
    hideSharedStyleDropdowns();
    delete m_arrowStyleQmlControl;
    delete m_lineStyleQmlControl;
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
    syncSharedStyleDropdowns();
}

void ToolOptionsPanel::setArrowStyle(LineEndStyle style)
{
    m_arrowStyleSection->setArrowStyle(style);
    syncSharedStyleDropdowns();
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
    syncSharedStyleDropdowns();
}

void ToolOptionsPanel::setLineStyle(LineStyle style)
{
    m_lineStyleSection->setLineStyle(style);
    syncSharedStyleDropdowns();
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

void ToolOptionsPanel::setVisible(bool visible)
{
    m_visible = visible;
    syncSharedStyleDropdowns();
}

void ToolOptionsPanel::setUseSharedStyleDropdowns(bool enabled)
{
    if (m_useSharedStyleDropdowns == enabled)
        return;

    m_useSharedStyleDropdowns = enabled;

    if (QWidget* widget = hostWidget()) {
        if (enabled) {
            widget->installEventFilter(this);
        } else {
            widget->removeEventFilter(this);
        }
    }

    syncSharedStyleDropdowns();
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
        if (widthSectionVisible && !m_showColorSection) {
            totalWidth += WIDTH_TO_AUTOBLUR_SPACING;  // Smaller spacing between width and auto blur
        } else if (hasFirstSection) {
            totalWidth += SECTION_SPACING;
        }
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
    syncSharedStyleDropdowns();
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
        if (widthSectionVisible && !m_showColorSection) {
            xOffset += WIDTH_TO_AUTOBLUR_SPACING;  // Smaller spacing between width and auto blur
        } else if (hasFirstSection) {
            xOffset += SECTION_SPACING;
        }
        m_autoBlurSection->updateLayout(containerTop, containerHeight, xOffset);
        xOffset += m_autoBlurSection->preferredWidth();
    }

    syncSharedStyleDropdowns();
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

    if (m_showArrowStyleSection && !m_useSharedStyleDropdowns) {
        m_arrowStyleSection->draw(painter, m_styleConfig);
    }

    if (m_showLineStyleSection && !m_useSharedStyleDropdowns) {
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

    if (m_widgetRect.contains(pos))
        return true;

    if (m_useSharedStyleDropdowns) {
        if (m_arrowStyleQmlControl && m_arrowStyleQmlControl->contains(pos))
            return true;
        if (m_lineStyleQmlControl && m_lineStyleQmlControl->contains(pos))
            return true;
        return false;
    }

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
    if (m_useSharedStyleDropdowns) {
        syncSharedStyleDropdowns();

        if ((m_arrowStyleQmlControl && m_arrowStyleQmlControl->contains(pos)) ||
            (m_lineStyleQmlControl && m_lineStyleQmlControl->contains(pos))) {
            return true;
        }

        if (!m_widgetRect.contains(pos))
            return false;

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

    if (m_showArrowStyleSection && !m_useSharedStyleDropdowns) {
        changed |= m_arrowStyleSection->updateHovered(pos);
    }

    if (m_showLineStyleSection && !m_useSharedStyleDropdowns) {
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
    if (m_useSharedStyleDropdowns) {
        return 0;
    }

    int dropdownHeight = 0;

    if (m_showArrowStyleSection && m_arrowStyleSection->isDropdownOpen()) {
        dropdownHeight = qMax(dropdownHeight, m_arrowStyleSection->dropdownRect().height());
    }

    if (m_showLineStyleSection && m_lineStyleSection->isDropdownOpen()) {
        dropdownHeight = qMax(dropdownHeight, m_lineStyleSection->dropdownRect().height());
    }

    return dropdownHeight;
}

bool ToolOptionsPanel::hasOpenSharedStyleMenu() const
{
    if (!m_useSharedStyleDropdowns)
        return false;

    return (m_arrowStyleQmlControl && m_arrowStyleQmlControl->isMenuOpen()) ||
           (m_lineStyleQmlControl && m_lineStyleQmlControl->isMenuOpen());
}

bool ToolOptionsPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (!m_useSharedStyleDropdowns || watched != hostWidget() || !m_visible) {
        return QObject::eventFilter(watched, event);
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QPoint pos = mouseEvent->position().toPoint();
        const bool insideArrow = m_arrowStyleQmlControl && m_arrowStyleQmlControl->contains(pos);
        const bool insideLine = m_lineStyleQmlControl && m_lineStyleQmlControl->contains(pos);
        if (!insideArrow && !insideLine && hasOpenSharedStyleMenu()) {
            hideSharedStyleDropdowns();
        }
    } else if (event->type() == QEvent::Hide) {
        hideSharedStyleDropdowns();
    }

    return QObject::eventFilter(watched, event);
}

QWidget* ToolOptionsPanel::hostWidget() const
{
    return qobject_cast<QWidget*>(parent());
}

bool ToolOptionsPanel::shouldDeferSharedStyleSync() const
{
    QWidget* widget = hostWidget();
    return m_useSharedStyleDropdowns && widget && widget->testAttribute(Qt::WA_WState_InPaintEvent);
}

void ToolOptionsPanel::queueSharedStyleSync()
{
    if (m_sharedStyleSyncQueued)
        return;

    m_sharedStyleSyncQueued = true;
    QMetaObject::invokeMethod(this, [this]() {
        m_sharedStyleSyncQueued = false;
        syncSharedStyleDropdowns();
    }, Qt::QueuedConnection);
}

void ToolOptionsPanel::ensureSharedStyleDropdowns()
{
    if (!m_useSharedStyleDropdowns || !hostWidget())
        return;

    if (!m_sharedStyleDropdownViewModel) {
        m_sharedStyleDropdownViewModel = new PinToolOptionsViewModel(this);
        m_sharedStyleDropdownViewModel->setArrowStyle(static_cast<int>(m_arrowStyleSection->arrowStyle()));
        m_sharedStyleDropdownViewModel->setLineStyle(static_cast<int>(m_lineStyleSection->lineStyle()));
        connect(m_sharedStyleDropdownViewModel, &PinToolOptionsViewModel::arrowStyleSelected,
                this, [this](int style) {
                    m_arrowStyleSection->setArrowStyle(static_cast<LineEndStyle>(style));
                });
        connect(m_sharedStyleDropdownViewModel, &PinToolOptionsViewModel::lineStyleSelected,
                this, [this](int style) {
                    m_lineStyleSection->setLineStyle(static_cast<LineStyle>(style));
                });
    }

    if (!m_arrowStyleQmlControl) {
        m_arrowStyleQmlControl = new EmbeddedSharedStyleControl(QStringLiteral("arrow"),
                                                                m_sharedStyleDropdownViewModel,
                                                                hostWidget(),
                                                                this);
        if (QQuickItem* root = m_arrowStyleQmlControl->rootItem()) {
            connect(root, SIGNAL(menuOpened()), this, SLOT(onSharedArrowMenuOpened()));
            connect(root, SIGNAL(menuClosed()), this, SLOT(onSharedArrowMenuClosed()));
        }
    }

    if (!m_lineStyleQmlControl) {
        m_lineStyleQmlControl = new EmbeddedSharedStyleControl(QStringLiteral("line"),
                                                               m_sharedStyleDropdownViewModel,
                                                               hostWidget(),
                                                               this);
        if (QQuickItem* root = m_lineStyleQmlControl->rootItem()) {
            connect(root, SIGNAL(menuOpened()), this, SLOT(onSharedLineMenuOpened()));
            connect(root, SIGNAL(menuClosed()), this, SLOT(onSharedLineMenuClosed()));
        }
    }
}

void ToolOptionsPanel::syncSharedStyleDropdowns()
{
    if (shouldDeferSharedStyleSync()) {
        queueSharedStyleSync();
        return;
    }

    if (!m_useSharedStyleDropdowns || !m_visible || !hostWidget()) {
        hideSharedStyleDropdowns();
        return;
    }

    ensureSharedStyleDropdowns();
    if (!m_sharedStyleDropdownViewModel)
        return;

    m_sharedStyleDropdownViewModel->setArrowStyle(static_cast<int>(m_arrowStyleSection->arrowStyle()));
    m_sharedStyleDropdownViewModel->setLineStyle(static_cast<int>(m_lineStyleSection->lineStyle()));

    if (m_arrowStyleQmlControl) {
        m_arrowStyleQmlControl->setExpandUpward(m_dropdownExpandsUpward);
        m_arrowStyleQmlControl->setAnchorRect(m_arrowStyleSection->buttonRect());
        m_arrowStyleQmlControl->setSectionVisible(m_showArrowStyleSection);
    }

    if (m_lineStyleQmlControl) {
        m_lineStyleQmlControl->setExpandUpward(m_dropdownExpandsUpward);
        m_lineStyleQmlControl->setAnchorRect(m_lineStyleSection->buttonRect());
        m_lineStyleQmlControl->setSectionVisible(m_showLineStyleSection);
    }
}

void ToolOptionsPanel::hideSharedStyleDropdowns()
{
    if (m_arrowStyleQmlControl) {
        m_arrowStyleQmlControl->closeMenu();
        if (!m_useSharedStyleDropdowns || !m_visible || !m_showArrowStyleSection)
            m_arrowStyleQmlControl->hide();
    }
    if (m_lineStyleQmlControl) {
        m_lineStyleQmlControl->closeMenu();
        if (!m_useSharedStyleDropdowns || !m_visible || !m_showLineStyleSection)
            m_lineStyleQmlControl->hide();
    }
}

void ToolOptionsPanel::onSharedArrowMenuOpened()
{
    if (m_lineStyleQmlControl && m_lineStyleQmlControl->isMenuOpen())
        m_lineStyleQmlControl->closeMenu();
    emit dropdownStateChanged();
}

void ToolOptionsPanel::onSharedLineMenuOpened()
{
    if (m_arrowStyleQmlControl && m_arrowStyleQmlControl->isMenuOpen())
        m_arrowStyleQmlControl->closeMenu();
    emit dropdownStateChanged();
}

void ToolOptionsPanel::onSharedArrowMenuClosed()
{
    emit dropdownStateChanged();
}

void ToolOptionsPanel::onSharedLineMenuClosed()
{
    emit dropdownStateChanged();
}
