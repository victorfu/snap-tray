#include "ScreenCanvas.h"
#include "AnnotationLayer.h"

#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QDebug>
#include <QSvgRenderer>
#include <QColorDialog>

ScreenCanvas::ScreenCanvas(QWidget *parent)
    : QWidget(parent)
    , m_currentScreen(nullptr)
    , m_devicePixelRatio(1.0)
    , m_annotationLayer(nullptr)
    , m_currentTool(CanvasTool::Pencil)
    , m_annotationColor(Qt::red)
    , m_annotationWidth(3)
    , m_isDrawing(false)
    , m_hoveredButton(-1)
    , m_hoveredColorSwatch(-1)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);

    // Initialize button rects
    m_buttonRects.resize(static_cast<int>(CanvasTool::Count));

    // Initialize annotation layer
    m_annotationLayer = new AnnotationLayer(this);

    // Initialize SVG icons
    initializeIcons();

    // Initialize color palette
    initializeColorPalette();

    qDebug() << "ScreenCanvas: Created";
}

ScreenCanvas::~ScreenCanvas()
{
    // Clean up SVG renderers
    qDeleteAll(m_iconRenderers);
    m_iconRenderers.clear();

    qDebug() << "ScreenCanvas: Destroyed";
}

void ScreenCanvas::initializeIcons()
{
    // Map buttons to their SVG resource paths (reusing existing icons)
    static const QHash<CanvasTool, QString> iconPaths = {
        {CanvasTool::Pencil, ":/icons/icons/pencil.svg"},
        {CanvasTool::Marker, ":/icons/icons/marker.svg"},
        {CanvasTool::Arrow, ":/icons/icons/arrow.svg"},
        {CanvasTool::Rectangle, ":/icons/icons/rectangle.svg"},
        {CanvasTool::Undo, ":/icons/icons/undo.svg"},
        {CanvasTool::Redo, ":/icons/icons/redo.svg"},
        {CanvasTool::Clear, ":/icons/icons/cancel.svg"},  // Reuse cancel icon for clear
        {CanvasTool::Exit, ":/icons/icons/cancel.svg"},
    };

    for (auto it = iconPaths.begin(); it != iconPaths.end(); ++it) {
        QSvgRenderer *renderer = new QSvgRenderer(it.value(), this);
        if (renderer->isValid()) {
            m_iconRenderers[it.key()] = renderer;
        } else {
            qWarning() << "ScreenCanvas: Failed to load icon:" << it.value();
            delete renderer;
        }
    }
}

void ScreenCanvas::renderIcon(QPainter &painter, const QRect &rect, CanvasTool tool, const QColor &color)
{
    QSvgRenderer *renderer = m_iconRenderers.value(tool, nullptr);
    if (!renderer) {
        painter.setPen(color);
        painter.drawText(rect, Qt::AlignCenter, "?");
        return;
    }

    int iconSize = qMin(rect.width(), rect.height()) - 8;
    QRect iconRect(0, 0, iconSize, iconSize);
    iconRect.moveCenter(rect.center());

    qreal dpr = painter.device()->devicePixelRatio();

    QPixmap iconPixmap(iconSize * dpr, iconSize * dpr);
    iconPixmap.setDevicePixelRatio(dpr);
    iconPixmap.fill(Qt::transparent);

    QPainter iconPainter(&iconPixmap);
    iconPainter.setRenderHint(QPainter::Antialiasing);
    renderer->render(&iconPainter, QRectF(0, 0, iconSize, iconSize));
    iconPainter.end();

    QPixmap tintedPixmap(iconPixmap.size());
    tintedPixmap.setDevicePixelRatio(dpr);
    tintedPixmap.fill(Qt::transparent);

    QPainter tintPainter(&tintedPixmap);
    tintPainter.drawPixmap(0, 0, iconPixmap);
    tintPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    tintPainter.fillRect(tintedPixmap.rect(), color);
    tintPainter.end();

    painter.drawPixmap(iconRect, tintedPixmap);
}

void ScreenCanvas::initializeColorPalette()
{
    m_colorPalette = {
        Qt::red,                    // Default - matches m_annotationColor
        QColor(0, 122, 255),        // Blue (system blue)
        QColor(52, 199, 89),        // Green (system green)
        QColor(255, 204, 0),        // Yellow
        QColor(255, 149, 0),        // Orange
        QColor(175, 82, 222),       // Purple
        Qt::black,
        Qt::white
    };
    m_colorSwatchRects.resize(m_colorPalette.size() + 1); // +1 for "more" button
}

bool ScreenCanvas::shouldShowColorPalette() const
{
    // Show palette for color-enabled tools
    switch (m_currentTool) {
    case CanvasTool::Pencil:
    case CanvasTool::Marker:
    case CanvasTool::Arrow:
    case CanvasTool::Rectangle:
        return true;
    default:
        return false;
    }
}

void ScreenCanvas::updateColorPalettePosition()
{
    int swatchCount = m_colorPalette.size() + 1; // +1 for "more" button
    int paletteWidth = swatchCount * COLOR_SWATCH_SIZE + (swatchCount - 1) * COLOR_SWATCH_SPACING + COLOR_PALETTE_PADDING * 2;

    // Position above toolbar, left-aligned with toolbar
    int paletteX = m_toolbarRect.left();
    int paletteY = m_toolbarRect.top() - COLOR_PALETTE_HEIGHT - 4;

    // Keep on screen
    if (paletteX < 5) paletteX = 5;
    if (paletteY < 5) {
        // If not enough space above, put below
        paletteY = m_toolbarRect.bottom() + 4;
    }

    m_colorPaletteRect = QRect(paletteX, paletteY, paletteWidth, COLOR_PALETTE_HEIGHT);

    // Update swatch rects
    int x = paletteX + COLOR_PALETTE_PADDING;
    int y = paletteY + (COLOR_PALETTE_HEIGHT - COLOR_SWATCH_SIZE) / 2;

    for (int i = 0; i <= m_colorPalette.size(); ++i) {
        m_colorSwatchRects[i] = QRect(x, y, COLOR_SWATCH_SIZE, COLOR_SWATCH_SIZE);
        x += COLOR_SWATCH_SIZE + COLOR_SWATCH_SPACING;
    }
}

void ScreenCanvas::drawColorPalette(QPainter &painter)
{
    updateColorPalettePosition();

    // Draw shadow
    QRect shadowRect = m_colorPaletteRect.adjusted(2, 2, 2, 2);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 50));
    painter.drawRoundedRect(shadowRect, 6, 6);

    // Draw background with gradient (matches toolbar style)
    QLinearGradient gradient(m_colorPaletteRect.topLeft(), m_colorPaletteRect.bottomLeft());
    gradient.setColorAt(0, QColor(55, 55, 55, 245));
    gradient.setColorAt(1, QColor(40, 40, 40, 245));

    painter.setBrush(gradient);
    painter.setPen(QPen(QColor(70, 70, 70), 1));
    painter.drawRoundedRect(m_colorPaletteRect, 6, 6);

    // Draw color swatches
    for (int i = 0; i < m_colorPalette.size(); ++i) {
        QRect swatchRect = m_colorSwatchRects[i];

        // Highlight if hovered
        if (i == m_hoveredColorSwatch) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(80, 80, 80));
            painter.drawRoundedRect(swatchRect.adjusted(-2, -2, 2, 2), 4, 4);
        }

        // Draw color circle
        int circleSize = swatchRect.width() - 6;
        QRect circleRect(swatchRect.center().x() - circleSize / 2,
                         swatchRect.center().y() - circleSize / 2,
                         circleSize, circleSize);

        // Draw border (white for dark colors, dark for light colors)
        QColor borderColor = (m_colorPalette[i].lightness() > 200) ? QColor(80, 80, 80) : Qt::white;
        painter.setPen(QPen(borderColor, 1));
        painter.setBrush(m_colorPalette[i]);
        painter.drawEllipse(circleRect);

        // Draw selection indicator for current color
        if (m_colorPalette[i] == m_annotationColor) {
            painter.setPen(QPen(QColor(0, 174, 255), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(circleRect.adjusted(-3, -3, 3, 3));
        }
    }

    // Draw "more colors" button (last swatch position)
    int moreIdx = m_colorPalette.size();
    QRect moreRect = m_colorSwatchRects[moreIdx];

    if (moreIdx == m_hoveredColorSwatch) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(80, 80, 80));
        painter.drawRoundedRect(moreRect.adjusted(-2, -2, 2, 2), 4, 4);
    }

    // Draw "..." text
    painter.setPen(QColor(180, 180, 180));
    QFont font = painter.font();
    font.setPointSize(12);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(moreRect, Qt::AlignCenter, "...");
}

int ScreenCanvas::getColorSwatchAtPosition(const QPoint &pos)
{
    if (!m_colorPaletteRect.contains(pos)) return -1;

    for (int i = 0; i < m_colorSwatchRects.size(); ++i) {
        if (m_colorSwatchRects[i].contains(pos)) {
            return i;
        }
    }
    return -1;
}

void ScreenCanvas::handleColorSwatchClick(int swatchIndex)
{
    if (swatchIndex < m_colorPalette.size()) {
        m_annotationColor = m_colorPalette[swatchIndex];
        qDebug() << "ScreenCanvas: Color selected:" << m_annotationColor.name();
    } else {
        // "More colors" button clicked
        openColorDialog();
    }
    update();
}

void ScreenCanvas::openColorDialog()
{
    QColor newColor = QColorDialog::getColor(m_annotationColor, this, tr("Select Color"));
    if (newColor.isValid()) {
        m_annotationColor = newColor;
        qDebug() << "ScreenCanvas: Custom color selected:" << m_annotationColor.name();
    }
}

void ScreenCanvas::initializeForScreen(QScreen *screen)
{
    m_currentScreen = screen;
    if (!m_currentScreen) {
        m_currentScreen = QGuiApplication::primaryScreen();
    }

    m_devicePixelRatio = m_currentScreen->devicePixelRatio();

    // Capture the screen
    m_backgroundPixmap = m_currentScreen->grabWindow(0);

    qDebug() << "ScreenCanvas: Initialized for screen" << m_currentScreen->name()
             << "logical size:" << m_currentScreen->geometry().size()
             << "pixmap size:" << m_backgroundPixmap.size()
             << "devicePixelRatio:" << m_devicePixelRatio;

    // Lock window size
    setFixedSize(m_currentScreen->geometry().size());

    // Update toolbar position
    updateToolbarPosition();
}

void ScreenCanvas::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw background screenshot
    painter.drawPixmap(rect(), m_backgroundPixmap);

    // Draw completed annotations
    drawAnnotations(painter);

    // Draw annotation in progress (preview)
    drawCurrentAnnotation(painter);

    // Draw toolbar
    drawToolbar(painter);

    // Draw color palette
    if (shouldShowColorPalette()) {
        drawColorPalette(painter);
    }

    // Draw tooltip
    drawTooltip(painter);
}

void ScreenCanvas::drawAnnotations(QPainter &painter)
{
    m_annotationLayer->draw(painter);
}

void ScreenCanvas::drawCurrentAnnotation(QPainter &painter)
{
    if (!m_isDrawing) return;

    if (m_currentPencil) {
        m_currentPencil->draw(painter);
    } else if (m_currentMarker) {
        m_currentMarker->draw(painter);
    } else if (m_currentArrow) {
        m_currentArrow->draw(painter);
    } else if (m_currentRectangle) {
        m_currentRectangle->draw(painter);
    }
}

void ScreenCanvas::drawToolbar(QPainter &painter)
{
    // Draw shadow
    QRect shadowRect = m_toolbarRect.adjusted(2, 2, 2, 2);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 50));
    painter.drawRoundedRect(shadowRect, 8, 8);

    // Draw toolbar background with gradient
    QLinearGradient gradient(m_toolbarRect.topLeft(), m_toolbarRect.bottomLeft());
    gradient.setColorAt(0, QColor(55, 55, 55, 245));
    gradient.setColorAt(1, QColor(40, 40, 40, 245));

    painter.setBrush(gradient);
    painter.setPen(QPen(QColor(70, 70, 70), 1));
    painter.drawRoundedRect(m_toolbarRect, 8, 8);

    // Render icons
    for (int i = 0; i < static_cast<int>(CanvasTool::Count); ++i) {
        QRect btnRect = m_buttonRects[i];
        CanvasTool tool = static_cast<CanvasTool>(i);

        // Highlight active tool (annotation tools only)
        bool isActive = (tool == m_currentTool) && isAnnotationTool(tool);
        if (isActive) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 120, 200));
            painter.drawRoundedRect(btnRect.adjusted(2, 2, -2, -2), 4, 4);
        } else if (i == m_hoveredButton) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(80, 80, 80));
            painter.drawRoundedRect(btnRect.adjusted(2, 2, -2, -2), 4, 4);
        }

        // Draw separator before Undo button
        if (i == static_cast<int>(CanvasTool::Undo)) {
            painter.setPen(QColor(80, 80, 80));
            painter.drawLine(btnRect.left() - 4, btnRect.top() + 6,
                             btnRect.left() - 4, btnRect.bottom() - 6);
        }

        // Draw separator before Exit button
        if (i == static_cast<int>(CanvasTool::Exit)) {
            painter.setPen(QColor(80, 80, 80));
            painter.drawLine(btnRect.left() - 4, btnRect.top() + 6,
                             btnRect.left() - 4, btnRect.bottom() - 6);
        }

        // Determine icon color
        QColor iconColor;
        if (tool == CanvasTool::Exit) {
            iconColor = QColor(255, 100, 100);  // Red for exit
        } else if (tool == CanvasTool::Clear) {
            iconColor = QColor(255, 180, 100);  // Orange for clear
        } else if (isActive) {
            iconColor = Qt::white;
        } else {
            iconColor = QColor(220, 220, 220);
        }

        renderIcon(painter, btnRect, tool, iconColor);
    }
}

void ScreenCanvas::updateToolbarPosition()
{
    int separatorCount = 2;  // Undo, Exit 前各有分隔線
    int toolbarWidth = static_cast<int>(CanvasTool::Count) * (BUTTON_WIDTH + BUTTON_SPACING) + 20 + separatorCount * 6;

    // Center horizontally, 30px from bottom
    int toolbarX = (width() - toolbarWidth) / 2;
    int toolbarY = height() - TOOLBAR_HEIGHT - 30;

    m_toolbarRect = QRect(toolbarX, toolbarY, toolbarWidth, TOOLBAR_HEIGHT);

    // Update button rects
    int x = toolbarX + 10;
    int y = toolbarY + (TOOLBAR_HEIGHT - BUTTON_WIDTH + 4) / 2;

    for (int i = 0; i < static_cast<int>(CanvasTool::Count); ++i) {
        m_buttonRects[i] = QRect(x, y, BUTTON_WIDTH, BUTTON_WIDTH - 4);
        x += BUTTON_WIDTH + BUTTON_SPACING;

        // Add extra spacing for separators
        if (i == static_cast<int>(CanvasTool::Rectangle) ||
            i == static_cast<int>(CanvasTool::Clear)) {
            x += 6;
        }
    }
}

int ScreenCanvas::getButtonAtPosition(const QPoint &pos)
{
    for (int i = 0; i < m_buttonRects.size(); ++i) {
        if (m_buttonRects[i].contains(pos)) {
            return i;
        }
    }
    return -1;
}

QString ScreenCanvas::getButtonTooltip(int buttonIndex)
{
    static const QStringList tooltips = {
        "Pencil",
        "Marker",
        "Arrow",
        "Rectangle",
        "Undo (Ctrl+Z)",
        "Redo (Ctrl+Y)",
        "Clear All",
        "Exit (Esc)"
    };

    if (buttonIndex >= 0 && buttonIndex < tooltips.size()) {
        return tooltips[buttonIndex];
    }
    return QString();
}

void ScreenCanvas::drawTooltip(QPainter &painter)
{
    if (m_hoveredButton < 0) return;

    QString tooltip = getButtonTooltip(m_hoveredButton);
    if (tooltip.isEmpty()) return;

    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);

    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(tooltip);
    textRect.adjust(-8, -4, 8, 4);

    // Position tooltip above the toolbar
    QRect btnRect = m_buttonRects[m_hoveredButton];
    int tooltipX = btnRect.center().x() - textRect.width() / 2;
    int tooltipY = m_toolbarRect.top() - textRect.height() - 6;

    // Keep on screen
    if (tooltipX < 5) tooltipX = 5;
    if (tooltipX + textRect.width() > width() - 5) {
        tooltipX = width() - textRect.width() - 5;
    }

    textRect.moveTo(tooltipX, tooltipY);

    // Draw tooltip background
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(30, 30, 30, 230));
    painter.drawRoundedRect(textRect, 4, 4);

    // Draw tooltip border
    painter.setPen(QColor(80, 80, 80));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(textRect, 4, 4);

    // Draw tooltip text
    painter.setPen(Qt::white);
    painter.drawText(textRect, Qt::AlignCenter, tooltip);
}

void ScreenCanvas::handleToolbarClick(CanvasTool button)
{
    switch (button) {
    case CanvasTool::Pencil:
    case CanvasTool::Marker:
    case CanvasTool::Arrow:
    case CanvasTool::Rectangle:
        m_currentTool = button;
        qDebug() << "ScreenCanvas: Tool selected:" << static_cast<int>(button);
        update();
        break;

    case CanvasTool::Undo:
        if (m_annotationLayer->canUndo()) {
            m_annotationLayer->undo();
            qDebug() << "ScreenCanvas: Undo";
            update();
        }
        break;

    case CanvasTool::Redo:
        if (m_annotationLayer->canRedo()) {
            m_annotationLayer->redo();
            qDebug() << "ScreenCanvas: Redo";
            update();
        }
        break;

    case CanvasTool::Clear:
        m_annotationLayer->clear();
        qDebug() << "ScreenCanvas: Clear all annotations";
        update();
        break;

    case CanvasTool::Exit:
        close();
        break;

    default:
        break;
    }
}

bool ScreenCanvas::isAnnotationTool(CanvasTool tool) const
{
    switch (tool) {
    case CanvasTool::Pencil:
    case CanvasTool::Marker:
    case CanvasTool::Arrow:
    case CanvasTool::Rectangle:
        return true;
    default:
        return false;
    }
}

void ScreenCanvas::startAnnotation(const QPoint &pos)
{
    m_isDrawing = true;
    m_drawStartPoint = pos;
    m_currentPath.clear();
    m_currentPath.append(pos);

    switch (m_currentTool) {
    case CanvasTool::Pencil:
        m_currentPencil = std::make_unique<PencilStroke>(m_currentPath, m_annotationColor, m_annotationWidth);
        break;

    case CanvasTool::Marker:
        m_currentMarker = std::make_unique<MarkerStroke>(m_currentPath, m_annotationColor, 20);
        break;

    case CanvasTool::Arrow:
        m_currentArrow = std::make_unique<ArrowAnnotation>(pos, pos, m_annotationColor, m_annotationWidth);
        break;

    case CanvasTool::Rectangle:
        m_currentRectangle = std::make_unique<RectangleAnnotation>(QRect(pos, pos), m_annotationColor, m_annotationWidth);
        break;

    default:
        m_isDrawing = false;
        break;
    }

    update();
}

void ScreenCanvas::updateAnnotation(const QPoint &pos)
{
    if (!m_isDrawing) return;

    switch (m_currentTool) {
    case CanvasTool::Pencil:
        if (m_currentPencil) {
            m_currentPath.append(pos);
            m_currentPencil->addPoint(pos);
        }
        break;

    case CanvasTool::Marker:
        if (m_currentMarker) {
            m_currentPath.append(pos);
            m_currentMarker->addPoint(pos);
        }
        break;

    case CanvasTool::Arrow:
        if (m_currentArrow) {
            m_currentArrow->setEnd(pos);
        }
        break;

    case CanvasTool::Rectangle:
        if (m_currentRectangle) {
            m_currentRectangle->setRect(QRect(m_drawStartPoint, pos));
        }
        break;

    default:
        break;
    }

    update();
}

void ScreenCanvas::finishAnnotation()
{
    if (!m_isDrawing) return;

    m_isDrawing = false;

    switch (m_currentTool) {
    case CanvasTool::Pencil:
        if (m_currentPencil && m_currentPath.size() > 1) {
            m_annotationLayer->addItem(std::move(m_currentPencil));
        }
        m_currentPencil.reset();
        break;

    case CanvasTool::Marker:
        if (m_currentMarker && m_currentPath.size() > 1) {
            m_annotationLayer->addItem(std::move(m_currentMarker));
        }
        m_currentMarker.reset();
        break;

    case CanvasTool::Arrow:
        if (m_currentArrow) {
            m_annotationLayer->addItem(std::move(m_currentArrow));
        }
        m_currentArrow.reset();
        break;

    case CanvasTool::Rectangle:
        if (m_currentRectangle) {
            m_annotationLayer->addItem(std::move(m_currentRectangle));
        }
        m_currentRectangle.reset();
        break;

    default:
        break;
    }

    m_currentPath.clear();
}

void ScreenCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // Check if clicked on color palette
        if (shouldShowColorPalette()) {
            int swatchIdx = getColorSwatchAtPosition(event->pos());
            if (swatchIdx >= 0) {
                handleColorSwatchClick(swatchIdx);
                return;
            }
        }

        // Check if clicked on toolbar
        int buttonIdx = getButtonAtPosition(event->pos());
        if (buttonIdx >= 0) {
            handleToolbarClick(static_cast<CanvasTool>(buttonIdx));
            return;
        }

        // Start annotation drawing
        if (isAnnotationTool(m_currentTool)) {
            startAnnotation(event->pos());
        }
    } else if (event->button() == Qt::RightButton) {
        // Cancel current annotation
        if (m_isDrawing) {
            m_isDrawing = false;
            m_currentPath.clear();
            m_currentPencil.reset();
            m_currentMarker.reset();
            m_currentArrow.reset();
            m_currentRectangle.reset();
            update();
        }
    }
}

void ScreenCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDrawing) {
        updateAnnotation(event->pos());
    } else {
        // Update hovered color swatch
        if (shouldShowColorPalette()) {
            int newHoveredSwatch = getColorSwatchAtPosition(event->pos());
            if (newHoveredSwatch != m_hoveredColorSwatch) {
                m_hoveredColorSwatch = newHoveredSwatch;
                if (m_hoveredColorSwatch >= 0) {
                    setCursor(Qt::PointingHandCursor);
                }
                update();
            }
        } else {
            m_hoveredColorSwatch = -1;
        }

        // Update hovered button
        int newHovered = getButtonAtPosition(event->pos());
        if (newHovered != m_hoveredButton) {
            m_hoveredButton = newHovered;
            if (m_hoveredButton >= 0) {
                setCursor(Qt::PointingHandCursor);
            } else if (m_hoveredColorSwatch >= 0) {
                // Already set cursor for color swatch
            } else {
                setCursor(Qt::CrossCursor);
            }
            update();
        }
    }
}

void ScreenCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_isDrawing) {
        finishAnnotation();
        update();
    }
}

void ScreenCanvas::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        qDebug() << "ScreenCanvas: Closed via Escape";
        close();
    } else if (event->matches(QKeySequence::Undo)) {
        if (m_annotationLayer->canUndo()) {
            m_annotationLayer->undo();
            update();
        }
    } else if (event->matches(QKeySequence::Redo)) {
        if (m_annotationLayer->canRedo()) {
            m_annotationLayer->redo();
            update();
        }
    }
}

void ScreenCanvas::closeEvent(QCloseEvent *event)
{
    emit closed();
    QWidget::closeEvent(event);
}
