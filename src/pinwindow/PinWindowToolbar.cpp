#include "pinwindow/PinWindowToolbar.h"
#include "ToolbarWidget.h"
#include "ColorAndWidthWidget.h"
#include "IconRenderer.h"
#include "GlassRenderer.h"
#include "ToolbarStyle.h"
#include "settings/AnnotationSettingsManager.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDebug>

namespace {
// Button IDs for PinWindow toolbar
enum PinWindowToolbarButton {
    ButtonShape = static_cast<int>(ToolId::Shape),
    ButtonArrow = static_cast<int>(ToolId::Arrow),
    ButtonPencil = static_cast<int>(ToolId::Pencil),
    ButtonMarker = static_cast<int>(ToolId::Marker),
    ButtonText = static_cast<int>(ToolId::Text),
    ButtonMosaic = static_cast<int>(ToolId::Mosaic),
    ButtonStepBadge = static_cast<int>(ToolId::StepBadge),
    ButtonEraser = static_cast<int>(ToolId::Eraser),
    ButtonUndo = 100,
    ButtonRedo = 101,
    ButtonDone = 102
};
}

PinWindowToolbar::PinWindowToolbar(QWidget* parent)
    : QWidget(parent)
    , m_toolbar(nullptr)
    , m_colorAndWidthWidget(nullptr)
{
    // Frameless, stays on top, tool window
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);

    setupUI();
    connectSignals();

    // Initial size
    updateGeometry();

    qDebug() << "PinWindowToolbar: Initialized";
}

PinWindowToolbar::~PinWindowToolbar() = default;

void PinWindowToolbar::setupUI()
{
    setupToolbar();
    setupColorWidthWidget();
}

void PinWindowToolbar::setupToolbar()
{
    m_toolbar = new ToolbarWidget(this);

    // Load icons
    auto& iconRenderer = IconRenderer::instance();
    iconRenderer.loadIcon("shape", ":/icons/icons/shape.svg");
    iconRenderer.loadIcon("arrow", ":/icons/icons/arrow.svg");
    iconRenderer.loadIcon("pencil", ":/icons/icons/pencil.svg");
    iconRenderer.loadIcon("marker", ":/icons/icons/marker.svg");
    iconRenderer.loadIcon("text", ":/icons/icons/text.svg");
    iconRenderer.loadIcon("mosaic", ":/icons/icons/mosaic.svg");
    iconRenderer.loadIcon("step-badge", ":/icons/icons/step-badge.svg");
    iconRenderer.loadIcon("eraser", ":/icons/icons/eraser.svg");
    iconRenderer.loadIcon("undo", ":/icons/icons/undo.svg");
    iconRenderer.loadIcon("redo", ":/icons/icons/redo.svg");
    iconRenderer.loadIcon("done", ":/icons/icons/done.svg");

    // Configure buttons
    QVector<ToolbarWidget::ButtonConfig> buttons;
    buttons.append({ ButtonShape, "shape", "Shape (S)", false });
    buttons.append({ ButtonArrow, "arrow", "Arrow (A)", false });
    buttons.append({ ButtonPencil, "pencil", "Pencil (P)", false });
    buttons.append({ ButtonMarker, "marker", "Marker (H)", false });
    buttons.append({ ButtonText, "text", "Text (T)", false });
    buttons.append({ ButtonMosaic, "mosaic", "Mosaic (M)", false });
    buttons.append({ ButtonStepBadge, "step-badge", "Step Badge (B)", false });
    buttons.append(ToolbarWidget::ButtonConfig(ButtonEraser, "eraser", "Eraser (E)").separator());
    buttons.append(ToolbarWidget::ButtonConfig(ButtonUndo, "undo", "Undo (Ctrl+Z)").separator());
    buttons.append({ ButtonRedo, "redo", "Redo (Ctrl+Y)", false });
    buttons.append(ToolbarWidget::ButtonConfig(ButtonDone, "done", "Done (Enter)").separator().action());

    m_toolbar->setButtons(buttons);

    // Set which buttons can be active (drawing tools)
    QVector<int> activeButtonIds = {
        ButtonShape,
        ButtonArrow,
        ButtonPencil,
        ButtonMarker,
        ButtonText,
        ButtonMosaic,
        ButtonStepBadge,
        ButtonEraser
    };
    m_toolbar->setActiveButtonIds(activeButtonIds);

    // Set default active button
    m_toolbar->setActiveButton(ButtonPencil);

    // Set icon color provider
    ToolbarStyleConfig styleConfig = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    m_toolbar->setStyleConfig(styleConfig);
    m_toolbar->setIconColorProvider([this](int buttonId, bool isActive, bool isHovered) {
        return getIconColor(buttonId, isActive, isHovered);
    });
}

void PinWindowToolbar::setupColorWidthWidget()
{
    m_colorAndWidthWidget = new ColorAndWidthWidget(this);

    // Load settings
    auto& settings = AnnotationSettingsManager::instance();
    m_colorAndWidthWidget->setCurrentColor(settings.loadColor());
    m_colorAndWidthWidget->setCurrentWidth(settings.loadWidth());

    // Configure initial visibility
    updateColorWidthVisibility();
}

void PinWindowToolbar::connectSignals()
{
    // Toolbar button clicks
    connect(m_toolbar, &ToolbarWidget::buttonClicked, this, [this](int buttonId) {
        switch (buttonId) {
        case ButtonUndo:
            emit undoRequested();
            break;
        case ButtonRedo:
            emit redoRequested();
            break;
        case ButtonDone:
            emit closeRequested();
            break;
        default:
            // Tool button
            m_currentTool = static_cast<ToolId>(buttonId);
            m_toolbar->setActiveButton(buttonId);
            updateColorWidthVisibility();
            emit toolSelected(m_currentTool);
            update();
            break;
        }
    });

    // Color widget signals
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::colorSelected, this, [this](const QColor& color) {
        AnnotationSettingsManager::instance().saveColor(color);
        emit colorChanged(color);
        update();
    });

    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::widthChanged, this, [this](int width) {
        AnnotationSettingsManager::instance().saveWidth(width);
        emit widthChanged(width);
        update();
    });

    // Arrow style
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::arrowStyleChanged,
            this, &PinWindowToolbar::arrowStyleChanged);

    // Line style
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::lineStyleChanged,
            this, &PinWindowToolbar::lineStyleChanged);

    // Shape signals
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::shapeTypeChanged,
            this, &PinWindowToolbar::shapeTypeChanged);
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::shapeFillModeChanged,
            this, &PinWindowToolbar::shapeFillModeChanged);

    // Step badge size
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::stepBadgeSizeChanged,
            this, &PinWindowToolbar::stepBadgeSizeChanged);

    // Mosaic blur type
    connect(m_colorAndWidthWidget, &ColorAndWidthWidget::mosaicBlurTypeChanged,
            this, &PinWindowToolbar::mosaicBlurTypeChanged);
}

void PinWindowToolbar::updateColorWidthVisibility()
{
    // Configure color/width widget based on current tool
    bool showColor = true;
    bool showWidth = true;
    bool showArrowStyle = false;
    bool showLineStyle = false;
    bool showShapeSection = false;
    bool showTextSection = false;
    bool showMosaicWidth = false;
    bool showMosaicBlurType = false;
    bool showSizeSection = false;

    switch (m_currentTool) {
    case ToolId::Shape:
        showShapeSection = true;
        showLineStyle = true;
        break;
    case ToolId::Arrow:
        showArrowStyle = true;
        showLineStyle = true;
        break;
    case ToolId::Pencil:
        showLineStyle = true;
        break;
    case ToolId::Marker:
        showWidth = false;
        break;
    case ToolId::Text:
        showWidth = false;
        showTextSection = true;
        break;
    case ToolId::Mosaic:
        showColor = false;
        showWidth = false;
        showMosaicWidth = true;
        showMosaicBlurType = true;
        break;
    case ToolId::StepBadge:
        showWidth = false;
        showSizeSection = true;
        break;
    case ToolId::Eraser:
        showColor = false;
        break;
    default:
        break;
    }

    m_colorAndWidthWidget->setShowColorSection(showColor);
    m_colorAndWidthWidget->setShowWidthSection(showWidth);
    m_colorAndWidthWidget->setShowArrowStyleSection(showArrowStyle);
    m_colorAndWidthWidget->setShowLineStyleSection(showLineStyle);
    m_colorAndWidthWidget->setShowShapeSection(showShapeSection);
    m_colorAndWidthWidget->setShowTextSection(showTextSection);
    m_colorAndWidthWidget->setShowMosaicWidthSection(showMosaicWidth);
    m_colorAndWidthWidget->setShowMosaicBlurTypeSection(showMosaicBlurType);
    m_colorAndWidthWidget->setShowSizeSection(showSizeSection);
}

QColor PinWindowToolbar::getIconColor(int buttonId, bool isActive, bool isHovered) const
{
    ToolbarStyleConfig styleConfig = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());

    // Done button uses action color
    if (buttonId == ButtonDone) {
        return styleConfig.iconActionColor;
    }

    // Undo/Redo - check enabled state
    if (buttonId == ButtonUndo && !m_undoEnabled) {
        return styleConfig.iconNormalColor.darker(150);
    }
    if (buttonId == ButtonRedo && !m_redoEnabled) {
        return styleConfig.iconNormalColor.darker(150);
    }

    // Active tool
    if (isActive) {
        return styleConfig.iconActiveColor;
    }

    // Hovered
    if (isHovered) {
        return styleConfig.iconNormalColor;
    }

    return styleConfig.iconNormalColor;
}

void PinWindowToolbar::setCurrentTool(ToolId tool)
{
    m_currentTool = tool;
    m_toolbar->setActiveButton(static_cast<int>(tool));
    updateColorWidthVisibility();
    update();
}

void PinWindowToolbar::setColor(const QColor& color)
{
    m_colorAndWidthWidget->setCurrentColor(color);
}

QColor PinWindowToolbar::color() const
{
    return m_colorAndWidthWidget->currentColor();
}

void PinWindowToolbar::setWidth(int width)
{
    m_colorAndWidthWidget->setCurrentWidth(width);
}

int PinWindowToolbar::width() const
{
    return m_colorAndWidthWidget->currentWidth();
}

void PinWindowToolbar::setUndoEnabled(bool enabled)
{
    m_undoEnabled = enabled;
    update();
}

void PinWindowToolbar::setRedoEnabled(bool enabled)
{
    m_redoEnabled = enabled;
    update();
}

void PinWindowToolbar::positionRelativeTo(const QRect& parentRect)
{
    // Position toolbar below the parent window, centered
    int x = parentRect.center().x() - width() / 2;
    int y = parentRect.bottom() + kToolbarSpacing;

    move(x, y);
}

void PinWindowToolbar::setColorWidthWidgetVisible(bool visible)
{
    m_colorAndWidthWidget->setVisible(visible);
    update();
}

void PinWindowToolbar::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    ToolbarStyleConfig styleConfig = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());

    // Draw glass background
    QRect bgRect = rect().adjusted(kMargin, kMargin, -kMargin, -kMargin);
    GlassRenderer::drawGlassPanel(painter, bgRect, styleConfig);

    // Calculate toolbar position within the widget
    QRect toolbarRect = m_toolbar->boundingRect();
    int toolbarX = (width() - toolbarRect.width()) / 2;
    int toolbarY = kMargin + (32 - toolbarRect.height()) / 2 + 4;

    // Translate painter for toolbar drawing
    painter.save();
    painter.translate(toolbarX, toolbarY);
    m_toolbar->draw(painter);
    m_toolbar->drawTooltip(painter);
    painter.restore();

    // Draw color/width widget if visible
    if (m_colorAndWidthWidget->isVisible()) {
        // Position color widget below toolbar
        QRect colorRect(kMargin, kMargin + 32 + 4, width() - 2 * kMargin, 32);
        m_colorAndWidthWidget->updatePosition(colorRect, false, width());
        m_colorAndWidthWidget->draw(painter);
    }
}

void PinWindowToolbar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // Check if click is on toolbar
        QRect toolbarRect = m_toolbar->boundingRect();
        int toolbarX = (width() - toolbarRect.width()) / 2;
        int toolbarY = kMargin + (32 - toolbarRect.height()) / 2 + 4;

        QPoint toolbarPos = event->pos() - QPoint(toolbarX, toolbarY);
        int buttonId = m_toolbar->buttonAtPosition(toolbarPos);

        if (buttonId >= 0) {
            emit m_toolbar->buttonClicked(buttonId);
            event->accept();
            return;
        }

        // Check color/width widget
        if (m_colorAndWidthWidget->isVisible() && m_colorAndWidthWidget->contains(event->pos())) {
            m_colorAndWidthWidget->handleMousePress(event->pos());
            event->accept();
            return;
        }

        // Start dragging
        m_isDragging = true;
        m_dragStartPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
    }
    event->accept();
}

void PinWindowToolbar::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isDragging) {
        move(event->globalPosition().toPoint() - m_dragStartPos);
        return;
    }

    // Update hover state
    QRect toolbarRect = m_toolbar->boundingRect();
    int toolbarX = (width() - toolbarRect.width()) / 2;
    int toolbarY = kMargin + (32 - toolbarRect.height()) / 2 + 4;

    QPoint toolbarPos = event->pos() - QPoint(toolbarX, toolbarY);
    if (m_toolbar->updateHoveredButton(toolbarPos)) {
        update();
    }

    // Color/width widget hover
    if (m_colorAndWidthWidget->isVisible()) {
        bool pressed = event->buttons() & Qt::LeftButton;
        if (m_colorAndWidthWidget->handleMouseMove(event->pos(), pressed)) {
            update();
        }
    }
}

void PinWindowToolbar::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDragging = false;

        // Color/width widget release
        if (m_colorAndWidthWidget->isVisible()) {
            m_colorAndWidthWidget->handleMouseRelease(event->pos());
        }
    }
    event->accept();
}

void PinWindowToolbar::wheelEvent(QWheelEvent* event)
{
    // Forward to color/width widget
    if (m_colorAndWidthWidget->isVisible()) {
        if (m_colorAndWidthWidget->handleWheel(event->angleDelta().y())) {
            update();
        }
    }
    event->accept();
}

void PinWindowToolbar::leaveEvent(QEvent*)
{
    m_toolbar->updateHoveredButton(QPoint(-1, -1));
    update();
}
