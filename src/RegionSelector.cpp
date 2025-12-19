#include "RegionSelector.h"
#include "AnnotationLayer.h"
#include "IconRenderer.h"
#include "ColorPaletteWidget.h"

#ifdef Q_OS_MACOS
#include "OCRManager.h"
#endif

#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QDebug>
#include <QCursor>
#include <QClipboard>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDateTime>
#include <QInputDialog>
#include <QToolTip>
#include <QPointer>
#include <QColorDialog>

RegionSelector::RegionSelector(QWidget* parent)
    : QWidget(parent)
    , m_isSelecting(false)
    , m_selectionComplete(false)
    , m_currentScreen(nullptr)
    , m_devicePixelRatio(1.0)
    , m_showHexColor(true)
    , m_hoveredButton(-1)
    , m_annotationLayer(nullptr)
    , m_currentTool(ToolbarButton::Selection)
    , m_annotationColor(Qt::red)
    , m_annotationWidth(3)
    , m_isDrawing(false)
    , m_activeHandle(ResizeHandle::None)
    , m_isResizing(false)
    , m_isMoving(false)
#ifdef Q_OS_MACOS
    , m_windowDetector(nullptr)
#endif
#ifdef Q_OS_MACOS
    , m_ocrManager(nullptr)
    , m_ocrInProgress(false)
#endif
    , m_colorPalette(nullptr)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);  // 預設已選取完成，使用 ArrowCursor

    // Initialize button rects
    m_buttonRects.resize(static_cast<int>(ToolbarButton::Count));

    // Initialize annotation layer
    m_annotationLayer = new AnnotationLayer(this);

#ifdef Q_OS_MACOS
    // Initialize OCR manager if available (macOS only)
    if (OCRManager::isAvailable()) {
        m_ocrManager = new OCRManager(this);
    }
#endif

    // Initialize SVG icons
    initializeIcons();

    // Initialize color palette widget
    m_colorPalette = new ColorPaletteWidget(this);
    m_colorPalette->setCurrentColor(m_annotationColor);
    connect(m_colorPalette, &ColorPaletteWidget::colorSelected,
            this, &RegionSelector::onColorSelected);
    connect(m_colorPalette, &ColorPaletteWidget::moreColorsRequested,
            this, &RegionSelector::onMoreColorsRequested);

    // Install application-level event filter to capture ESC even when window loses focus
    qApp->installEventFilter(this);

    // 注意: 不在此處初始化螢幕，由 CaptureManager 調用 initializeForScreen()
}

RegionSelector::~RegionSelector()
{
    // Remove event filter
    qApp->removeEventFilter(this);

    qDebug() << "RegionSelector: Destroyed";
}

void RegionSelector::initializeIcons()
{
    // Use shared IconRenderer for all icons
    IconRenderer& iconRenderer = IconRenderer::instance();

    iconRenderer.loadIcon("selection", ":/icons/icons/selection.svg");
    iconRenderer.loadIcon("arrow", ":/icons/icons/arrow.svg");
    iconRenderer.loadIcon("pencil", ":/icons/icons/pencil.svg");
    iconRenderer.loadIcon("marker", ":/icons/icons/marker.svg");
    iconRenderer.loadIcon("rectangle", ":/icons/icons/rectangle.svg");
    iconRenderer.loadIcon("text", ":/icons/icons/text.svg");
    iconRenderer.loadIcon("mosaic", ":/icons/icons/mosaic.svg");
    iconRenderer.loadIcon("step-badge", ":/icons/icons/step-badge.svg");
    iconRenderer.loadIcon("eraser", ":/icons/icons/eraser.svg");
    iconRenderer.loadIcon("undo", ":/icons/icons/undo.svg");
    iconRenderer.loadIcon("redo", ":/icons/icons/redo.svg");
    iconRenderer.loadIcon("cancel", ":/icons/icons/cancel.svg");
#ifdef Q_OS_MACOS
    iconRenderer.loadIcon("ocr", ":/icons/icons/ocr.svg");
#endif
    iconRenderer.loadIcon("pin", ":/icons/icons/pin.svg");
    iconRenderer.loadIcon("save", ":/icons/icons/save.svg");
    iconRenderer.loadIcon("copy", ":/icons/icons/copy.svg");
}

QString RegionSelector::getIconKeyForButton(ToolbarButton button) const
{
    switch (button) {
    case ToolbarButton::Selection:  return "selection";
    case ToolbarButton::Arrow:      return "arrow";
    case ToolbarButton::Pencil:     return "pencil";
    case ToolbarButton::Marker:     return "marker";
    case ToolbarButton::Rectangle:  return "rectangle";
    case ToolbarButton::Text:       return "text";
    case ToolbarButton::Mosaic:     return "mosaic";
    case ToolbarButton::StepBadge:  return "step-badge";
    case ToolbarButton::Eraser:     return "eraser";
    case ToolbarButton::Undo:       return "undo";
    case ToolbarButton::Redo:       return "redo";
    case ToolbarButton::Cancel:     return "cancel";
#ifdef Q_OS_MACOS
    case ToolbarButton::OCR:        return "ocr";
#endif
    case ToolbarButton::Pin:        return "pin";
    case ToolbarButton::Save:       return "save";
    case ToolbarButton::Copy:       return "copy";
    default:                        return QString();
    }
}

void RegionSelector::renderIcon(QPainter &painter, const QRect &rect, ToolbarButton button, const QColor &color)
{
    QString key = getIconKeyForButton(button);
    IconRenderer::instance().renderIcon(painter, rect, key, color);
}

bool RegionSelector::shouldShowColorPalette() const
{
    if (!m_selectionComplete) return false;

    // Show palette for color-enabled tools (not Mosaic)
    switch (m_currentTool) {
    case ToolbarButton::Pencil:
    case ToolbarButton::Marker:
    case ToolbarButton::Arrow:
    case ToolbarButton::Rectangle:
    case ToolbarButton::Text:
    case ToolbarButton::StepBadge:
        return true;
    default:
        return false;
    }
}

void RegionSelector::onColorSelected(const QColor &color)
{
    m_annotationColor = color;
    update();
}

void RegionSelector::onMoreColorsRequested()
{
    QColor newColor = QColorDialog::getColor(m_annotationColor, this, tr("Select Color"));
    if (newColor.isValid()) {
        m_annotationColor = newColor;
        m_colorPalette->setCurrentColor(newColor);
        qDebug() << "Custom color selected:" << m_annotationColor.name();
        update();
    }
}

void RegionSelector::initializeForScreen(QScreen* screen)
{
    // 使用傳入的螢幕，若為空則使用主螢幕
    m_currentScreen = screen;
    if (!m_currentScreen) {
        m_currentScreen = QGuiApplication::primaryScreen();
    }

    m_devicePixelRatio = m_currentScreen->devicePixelRatio();

    // Capture the screen FIRST (returns device pixel resolution on HiDPI)
    m_backgroundPixmap = m_currentScreen->grabWindow(0);
    m_backgroundImageCache = m_backgroundPixmap.toImage();  // 預先轉換並快取

    qDebug() << "RegionSelector: Initialized for screen" << m_currentScreen->name()
        << "logical size:" << m_currentScreen->geometry().size()
        << "pixmap size:" << m_backgroundPixmap.size()
        << "devicePixelRatio:" << m_devicePixelRatio;

    // 不預設選取整個螢幕，等待用戶操作
    m_selectionRect = QRect();
    m_selectionComplete = false;
    m_isSelecting = false;

    // 將 cursor 全域座標轉換為 widget 本地座標，用於 crosshair/magnifier 初始位置
    QRect screenGeom = m_currentScreen->geometry();
    QPoint globalCursor = QCursor::pos();
    m_currentPoint = globalCursor - screenGeom.topLeft();

    // 鎖定視窗大小，防止 macOS 原生 resize 行為
    setFixedSize(screenGeom.size());

    setCursor(Qt::CrossCursor);
}

// 保留舊方法以保持向後兼容 (但不再使用)
void RegionSelector::captureCurrentScreen()
{
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    initializeForScreen(screen);
}

#ifdef Q_OS_MACOS
void RegionSelector::setWindowDetector(WindowDetector *detector)
{
    m_windowDetector = detector;
}

void RegionSelector::updateWindowDetection(const QPoint &localPos)
{
    if (!m_windowDetector || !m_windowDetector->isEnabled() || !m_currentScreen) {
        return;
    }

    // Convert local position to screen coordinates
    QPoint screenPos = m_currentScreen->geometry().topLeft() + localPos;

    // Detect window at position
    auto detected = m_windowDetector->detectWindowAt(screenPos);

    if (detected.has_value()) {
        // Convert screen coords to local widget coords
        QRect localBounds = detected->bounds.translated(
            -m_currentScreen->geometry().topLeft()
        );

        // Clip to screen bounds
        localBounds = localBounds.intersected(rect());

        if (localBounds != m_highlightedWindowRect) {
            m_highlightedWindowRect = localBounds;
            m_detectedWindow = detected;
            update();
        }
    } else {
        if (!m_highlightedWindowRect.isNull()) {
            m_highlightedWindowRect = QRect();
            m_detectedWindow.reset();
            update();
        }
    }
}

void RegionSelector::drawDetectedWindow(QPainter &painter)
{
    if (m_highlightedWindowRect.isNull() || m_selectionComplete || m_isSelecting) {
        return;
    }

    // Semi-transparent fill for detected window
    painter.fillRect(m_highlightedWindowRect, QColor(0, 174, 255, 30));

    // Dashed border to distinguish from final selection
    QPen dashedPen(QColor(0, 174, 255), 2, Qt::DashLine);
    painter.setPen(dashedPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(m_highlightedWindowRect);

    // Show window title hint
    if (m_detectedWindow.has_value()) {
        QString displayTitle = m_detectedWindow->windowTitle;
        if (displayTitle.isEmpty()) {
            displayTitle = m_detectedWindow->ownerApp;
        }
        if (!displayTitle.isEmpty()) {
            drawWindowHint(painter, displayTitle);
        }
    }
}

void RegionSelector::drawWindowHint(QPainter &painter, const QString &title)
{
    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);

    QFontMetrics fm(font);
    QString displayTitle = fm.elidedText(title, Qt::ElideRight, 200);
    QRect textRect = fm.boundingRect(displayTitle);
    textRect.adjust(-8, -4, 8, 4);

    // Position above the detected window
    int hintX = m_highlightedWindowRect.left();
    int hintY = m_highlightedWindowRect.top() - textRect.height() - 4;
    if (hintY < 5) {
        hintY = m_highlightedWindowRect.top() + 5;
    }
    textRect.moveTo(hintX, hintY);

    // Keep on screen horizontally
    if (textRect.right() > width() - 5) {
        textRect.moveRight(width() - 5);
    }
    if (textRect.left() < 5) {
        textRect.moveLeft(5);
    }

    // Draw background pill
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.drawRoundedRect(textRect, 4, 4);

    // Draw text
    painter.setPen(Qt::white);
    painter.drawText(textRect, Qt::AlignCenter, displayTitle);
}
#endif

void RegionSelector::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw the captured background scaled to fit the widget (logical pixels)
    painter.drawPixmap(rect(), m_backgroundPixmap);

    // Draw dimmed overlay
    drawOverlay(painter);

#ifdef Q_OS_MACOS
    // Draw detected window highlight (only during hover, before selection)
    if (!m_isSelecting && !m_selectionComplete) {
        drawDetectedWindow(painter);
    }
#endif

    // Draw selection if active or complete
    if ((m_isSelecting || m_selectionComplete) && m_selectionRect.isValid()) {
        drawSelection(painter);
        drawDimensionInfo(painter);

        // Draw annotations on top of selection
        if (m_selectionComplete) {
            drawAnnotations(painter);
            drawCurrentAnnotation(painter);
            drawToolbar(painter);
            if (shouldShowColorPalette()) {
                m_colorPalette->setVisible(true);
                m_colorPalette->updatePosition(m_toolbarRect, false);  // Below toolbar for RegionSelector
                m_colorPalette->draw(painter);
            } else {
                m_colorPalette->setVisible(false);
            }
            drawTooltip(painter);
        }
    }

    // Draw crosshair at cursor - only show inside selection when selection is complete
    bool shouldShowCrosshair = !m_isSelecting && !m_isDrawing;
    if (m_selectionComplete) {
        QRect sel = m_selectionRect.normalized();
        // Only show crosshair inside selection area, not on toolbar
        shouldShowCrosshair = shouldShowCrosshair &&
                              sel.contains(m_currentPoint) &&
                              !m_toolbarRect.contains(m_currentPoint);
    }

    if (shouldShowCrosshair) {
        drawCrosshair(painter);
        drawMagnifier(painter);
    }
}

void RegionSelector::drawOverlay(QPainter& painter)
{
    QColor dimColor(0, 0, 0, 100);
    QColor lightDimColor(0, 0, 0, 30);  // 輕微遮罩用於選取區域

    if ((m_isSelecting || m_selectionComplete) && m_selectionRect.isValid()) {
        QRect sel = m_selectionRect.normalized();

        // Top region
        painter.fillRect(QRect(0, 0, width(), sel.top()), dimColor);
        // Bottom region
        painter.fillRect(QRect(0, sel.bottom() + 1, width(), height() - sel.bottom() - 1), dimColor);
        // Left region
        painter.fillRect(QRect(0, sel.top(), sel.left(), sel.height()), dimColor);
        // Right region
        painter.fillRect(QRect(sel.right() + 1, sel.top(), width() - sel.right() - 1, sel.height()), dimColor);

        // 選取區域內也加上輕微遮罩，表示進入截圖模式
        painter.fillRect(sel, lightDimColor);
    }
    else {
        painter.fillRect(rect(), dimColor);
    }
}

void RegionSelector::drawSelection(QPainter& painter)
{
    QRect sel = m_selectionRect.normalized();

    // Draw selection border
    painter.setPen(QPen(QColor(0, 174, 255), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(sel);

    // Draw corner and edge handles
    const int handleSize = 8;
    QColor handleColor(0, 174, 255);
    painter.setBrush(handleColor);
    painter.setPen(Qt::white);

    // Corner handles (circles for Snipaste style)
    auto drawHandle = [&](int x, int y) {
        painter.drawEllipse(QPoint(x, y), handleSize / 2, handleSize / 2);
        };

    drawHandle(sel.left(), sel.top());
    drawHandle(sel.right(), sel.top());
    drawHandle(sel.left(), sel.bottom());
    drawHandle(sel.right(), sel.bottom());
    drawHandle(sel.center().x(), sel.top());
    drawHandle(sel.center().x(), sel.bottom());
    drawHandle(sel.left(), sel.center().y());
    drawHandle(sel.right(), sel.center().y());
}

void RegionSelector::drawCrosshair(QPainter& painter)
{
    // 深藍色實線
    QPen pen(QColor(65, 105, 225), 1, Qt::SolidLine);
    painter.setPen(pen);

    // 水平線和垂直線延伸到邊緣
    painter.drawLine(0, m_currentPoint.y(), width(), m_currentPoint.y());
    painter.drawLine(m_currentPoint.x(), 0, m_currentPoint.x(), height());

    // 中心小方框 (8x8 像素)
    int boxSize = 8;
    QRect centerBox(m_currentPoint.x() - boxSize / 2, m_currentPoint.y() - boxSize / 2, boxSize, boxSize);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(centerBox);
}

void RegionSelector::drawMagnifier(QPainter& painter)
{
    const int magnifierSize = 120;  // 放大區域顯示大小
    const int gridCount = 15;       // 顯示 15x15 個像素格子
    const int panelWidth = 180;     // 面板寬度
    const int panelPadding = 10;

    // 取得當前像素顏色 (設備座標)
    int deviceX = static_cast<int>(m_currentPoint.x() * m_devicePixelRatio);
    int deviceY = static_cast<int>(m_currentPoint.y() * m_devicePixelRatio);
    const QImage &img = m_backgroundImageCache;
    QColor pixelColor;
    if (deviceX >= 0 && deviceX < img.width() && deviceY >= 0 && deviceY < img.height()) {
        pixelColor = QColor(img.pixel(deviceX, deviceY));
    }

    // 計算面板位置 (在 cursor 下方)
    int panelX = m_currentPoint.x() - panelWidth / 2;
    int panelY = m_currentPoint.y() + 25;

    // 計算面板總高度
    int totalHeight = magnifierSize + 75;  // 放大鏡 + 座標 + 顏色

    // 邊界檢查
    panelX = qMax(10, qMin(panelX, width() - panelWidth - 10));
    if (panelY + totalHeight > height()) {
        panelY = m_currentPoint.y() - totalHeight - 25;  // 改放上方
    }

    // 繪製面板背景
    QRect panelRect(panelX, panelY, panelWidth, totalHeight);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(30, 35, 45, 240));
    painter.drawRoundedRect(panelRect, 4, 4);

    // 計算放大鏡顯示區域
    int magX = panelX + (panelWidth - magnifierSize) / 2;
    int magY = panelY + panelPadding;

    // 從設備像素 pixmap 中取樣
    // 取 gridCount 個「邏輯像素」，每個邏輯像素 = devicePixelRatio 個設備像素
    int deviceGridCount = static_cast<int>(gridCount * m_devicePixelRatio);
    int sampleX = deviceX - deviceGridCount / 2;
    int sampleY = deviceY - deviceGridCount / 2;

    // 建立一個以游標為中心的取樣圖像，超出邊界的部分填充黑色
    QImage sampleImage(deviceGridCount, deviceGridCount, QImage::Format_ARGB32);
    sampleImage.fill(Qt::black);

    // 複製有效區域 - 直接從源圖像複製到 sampleImage (reuse img from above)
    for (int y = 0; y < deviceGridCount; ++y) {
        for (int x = 0; x < deviceGridCount; ++x) {
            int srcPixelX = sampleX + x;
            int srcPixelY = sampleY + y;
            if (srcPixelX >= 0 && srcPixelX < img.width() &&
                srcPixelY >= 0 && srcPixelY < img.height()) {
                sampleImage.setPixel(x, y, img.pixel(srcPixelX, srcPixelY));
            }
        }
    }

    // 使用 IgnoreAspectRatio 確保填滿整個區域
    QPixmap magnified = QPixmap::fromImage(sampleImage).scaled(magnifierSize, magnifierSize,
        Qt::IgnoreAspectRatio,
        Qt::FastTransformation);

    painter.drawPixmap(magX, magY, magnified);

    // 繪製網格線
    painter.setPen(QPen(QColor(100, 100, 100, 100), 1));
    int pixelSize = magnifierSize / gridCount;
    for (int i = 1; i < gridCount; ++i) {
        int xPos = magX + i * pixelSize;
        int yPos = magY + i * pixelSize;
        painter.drawLine(xPos, magY, xPos, magY + magnifierSize);
        painter.drawLine(magX, yPos, magX + magnifierSize, yPos);
    }

    // 繪製中心十字標記
    painter.setPen(QPen(QColor(65, 105, 225), 1));
    int cx = magX + magnifierSize / 2;
    int cy = magY + magnifierSize / 2;
    painter.drawLine(cx - pixelSize / 2, cy, cx + pixelSize / 2, cy);
    painter.drawLine(cx, cy - pixelSize / 2, cx, cy + pixelSize / 2);

    // 2. 座標資訊
    int infoY = magY + magnifierSize + 8;
    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);

    QString coordText = QString("(%1, %2)").arg(m_currentPoint.x()).arg(m_currentPoint.y());
    painter.drawText(panelX + panelPadding, infoY, panelWidth - 2 * panelPadding, 20, Qt::AlignCenter, coordText);

    // 3. 顏色預覽 + HEX/RGB (置中對齊)
    infoY += 20;
    int colorBoxSize = 14;

    // 計算顏色文字
    QString colorText;
    if (m_showHexColor) {
        colorText = QString("#%1%2%3")
            .arg(pixelColor.red(), 2, 16, QChar('0'))
            .arg(pixelColor.green(), 2, 16, QChar('0'))
            .arg(pixelColor.blue(), 2, 16, QChar('0'));
    }
    else {
        colorText = QString("RGB(%1, %2, %3)")
            .arg(pixelColor.red())
            .arg(pixelColor.green())
            .arg(pixelColor.blue());
    }

    // 計算整體寬度以置中：色塊 + 間距 + 文字
    QFontMetrics fm(font);
    int textWidth = fm.horizontalAdvance(colorText);
    int totalWidth = colorBoxSize + 8 + textWidth;
    int startX = panelX + (panelWidth - totalWidth) / 2;

    // 繪製顏色方塊
    painter.fillRect(startX, infoY, colorBoxSize, colorBoxSize, pixelColor);
    painter.setPen(QPen(QColor(80, 80, 80), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(startX, infoY, colorBoxSize, colorBoxSize);

    // 繪製顏色文字
    painter.setPen(Qt::white);
    painter.drawText(startX + colorBoxSize + 8, infoY, textWidth, colorBoxSize, Qt::AlignVCenter, colorText);
}

void RegionSelector::drawDimensionInfo(QPainter& painter)
{
    QRect sel = m_selectionRect.normalized();

    // Show dimensions with "pt" suffix like Snipaste
    QString dimensions = QString("%1 x %2  pt").arg(sel.width()).arg(sel.height());

    QFont font = painter.font();
    font.setPointSize(12);
    painter.setFont(font);

    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(dimensions);
    textRect.adjust(-12, -6, 12, 6);

    // Position above selection
    int textX = sel.left();
    int textY = sel.top() - textRect.height() - 8;
    if (textY < 5) {
        textY = sel.top() + 5;
        textX = sel.left() + 5;
    }

    textRect.moveTo(textX, textY);

    // Draw rounded background
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(40, 40, 40, 220));
    painter.drawRoundedRect(textRect, 6, 6);

    // Draw text
    painter.setPen(Qt::white);
    painter.drawText(textRect, Qt::AlignCenter, dimensions);
}

void RegionSelector::drawToolbar(QPainter& painter)
{
    updateToolbarPosition();

    // Draw shadow for depth effect
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

    // Render SVG icons
    for (int i = 0; i < static_cast<int>(ToolbarButton::Count); ++i) {
        QRect btnRect = m_buttonRects[i];
        ToolbarButton btn = static_cast<ToolbarButton>(i);

        // Highlight active tool (annotation tools only)
        bool isActive = (btn == m_currentTool) && isAnnotationTool(btn);
        if (isActive) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 120, 200));
            painter.drawRoundedRect(btnRect.adjusted(2, 2, -2, -2), 4, 4);
        }
        // Highlight hovered button
        else if (i == m_hoveredButton) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(80, 80, 80));
            painter.drawRoundedRect(btnRect.adjusted(2, 2, -2, -2), 4, 4);
        }

        // Draw separator before Cancel button
        if (i == static_cast<int>(ToolbarButton::Cancel)) {
            painter.setPen(QColor(80, 80, 80));
            painter.drawLine(btnRect.left() - 4, btnRect.top() + 6,
                btnRect.left() - 4, btnRect.bottom() - 6);
        }

#ifdef Q_OS_MACOS
        // Draw separator before OCR button
        if (i == static_cast<int>(ToolbarButton::OCR)) {
            painter.setPen(QColor(80, 80, 80));
            painter.drawLine(btnRect.left() - 4, btnRect.top() + 6,
                btnRect.left() - 4, btnRect.bottom() - 6);
        }
#endif

        // Draw separator before Pin button
        if (i == static_cast<int>(ToolbarButton::Pin)) {
            painter.setPen(QColor(80, 80, 80));
            painter.drawLine(btnRect.left() - 4, btnRect.top() + 6,
                btnRect.left() - 4, btnRect.bottom() - 6);
        }

        // Determine icon color based on state
        QColor iconColor;
        if (i == static_cast<int>(ToolbarButton::Cancel)) {
            iconColor = QColor(255, 100, 100);  // Red for cancel
        }
#ifdef Q_OS_MACOS
        else if (btn == ToolbarButton::OCR) {
            if (m_ocrInProgress) {
                iconColor = QColor(255, 200, 100);  // Yellow when processing
            } else if (!m_ocrManager) {
                iconColor = QColor(128, 128, 128);  // Gray if unavailable
            } else {
                iconColor = QColor(100, 200, 255);  // Blue accent
            }
        }
#endif
        else if (i >= static_cast<int>(ToolbarButton::Pin)) {
            iconColor = QColor(100, 200, 255);  // Blue for action buttons
        }
        else if (isActive) {
            iconColor = Qt::white;
        }
        else {
            iconColor = QColor(220, 220, 220);  // Off-white for normal state
        }

        // Render the SVG icon with the determined color
        renderIcon(painter, btnRect, btn, iconColor);
    }
}

void RegionSelector::updateToolbarPosition()
{
    QRect sel = m_selectionRect.normalized();

#ifdef Q_OS_MACOS
    int separatorCount = 3;  // Cancel, OCR, Pin 前各有分隔線
#else
    int separatorCount = 2;  // Cancel, Pin 前各有分隔線
#endif
    int toolbarWidth = static_cast<int>(ToolbarButton::Count) * (BUTTON_WIDTH + BUTTON_SPACING) + 20 + separatorCount * 6;
    int toolbarX = sel.right() - toolbarWidth;
    int toolbarY;

    // 計算可用空間
    int spaceBelow = height() - sel.bottom() - 1;
    int spaceAbove = sel.top();
    int requiredSpace = TOOLBAR_HEIGHT + 16;  // toolbar 高度 + 間距

    if (spaceBelow >= requiredSpace) {
        // 下方有足夠空間 - 放在選取框下方
        toolbarY = sel.bottom() + 8;
    } else if (spaceAbove >= requiredSpace) {
        // 上方有足夠空間 - 放在選取框上方
        toolbarY = sel.top() - TOOLBAR_HEIGHT - 8;
    } else {
        // 全螢幕或接近全螢幕 - 放在選取區域內的右上角
        toolbarY = sel.top() + 8;
    }

    // Keep toolbar on screen horizontally
    if (toolbarX < 5) toolbarX = 5;
    if (toolbarX + toolbarWidth > width() - 5) {
        toolbarX = width() - toolbarWidth - 5;
    }

    m_toolbarRect = QRect(toolbarX, toolbarY, toolbarWidth, TOOLBAR_HEIGHT);

    // Update button rects
    int x = toolbarX + 10;
    int y = toolbarY + (TOOLBAR_HEIGHT - BUTTON_WIDTH + 4) / 2;

    for (int i = 0; i < static_cast<int>(ToolbarButton::Count); ++i) {
        m_buttonRects[i] = QRect(x, y, BUTTON_WIDTH, BUTTON_WIDTH - 4);
        x += BUTTON_WIDTH + BUTTON_SPACING;

        // Add extra spacing for separators
        if (i == static_cast<int>(ToolbarButton::Redo)) {
            x += 6;  // Space before Cancel separator
        }
        if (i == static_cast<int>(ToolbarButton::Cancel)) {
            x += 6;  // Space before OCR (macOS) or Pin separator
        }
#ifdef Q_OS_MACOS
        if (i == static_cast<int>(ToolbarButton::OCR)) {
            x += 6;  // Space before Pin separator
        }
#endif
    }
}

int RegionSelector::getButtonAtPosition(const QPoint& pos)
{
    for (int i = 0; i < m_buttonRects.size(); ++i) {
        if (m_buttonRects[i].contains(pos)) {
            return i;
        }
    }
    return -1;
}

QString RegionSelector::getButtonTooltip(int buttonIndex)
{
    static const QStringList tooltips = {
        "Selection",
        "Arrow",
        "Pencil",
        "Marker",
        "Rectangle",
        "Text",
        "Mosaic",
        "Step Badge",
        "Eraser",
        "Undo",
        "Redo",
        "Cancel (Esc)",
#ifdef Q_OS_MACOS
        "OCR Text Recognition",
#endif
        "Pin to Screen (Enter)",
        "Save (Ctrl+S)",
        "Copy (Ctrl+C)"
    };

    if (buttonIndex >= 0 && buttonIndex < tooltips.size()) {
        return tooltips[buttonIndex];
    }
    return QString();
}

void RegionSelector::drawTooltip(QPainter& painter)
{
    if (m_hoveredButton < 0 || !m_selectionComplete) {
        return;
    }

    QString tooltip = getButtonTooltip(m_hoveredButton);
    if (tooltip.isEmpty()) {
        return;
    }

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
    if (tooltipY < 5) {
        tooltipY = m_toolbarRect.bottom() + 6;
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

void RegionSelector::handleToolbarClick(ToolbarButton button)
{
    switch (button) {
    case ToolbarButton::Selection:
    case ToolbarButton::Arrow:
    case ToolbarButton::Pencil:
    case ToolbarButton::Marker:
    case ToolbarButton::Rectangle:
    case ToolbarButton::Mosaic:
        m_currentTool = button;
        qDebug() << "Tool selected:" << static_cast<int>(button);
        update();
        break;

    case ToolbarButton::Text:
        m_currentTool = button;
        qDebug() << "Text tool selected - click in selection to add text";
        update();
        break;

    case ToolbarButton::StepBadge:
        m_currentTool = button;
        qDebug() << "StepBadge tool selected - click in selection to place numbered badge";
        update();
        break;

    case ToolbarButton::Eraser:
        m_currentTool = button;
        qDebug() << "Eraser tool selected - drag over annotations to erase them";
        update();
        break;

    case ToolbarButton::Undo:
        if (m_annotationLayer->canUndo()) {
            m_annotationLayer->undo();
            qDebug() << "Undo";
            update();
        }
        break;

    case ToolbarButton::Redo:
        if (m_annotationLayer->canRedo()) {
            m_annotationLayer->redo();
            qDebug() << "Redo";
            update();
        }
        break;

    case ToolbarButton::Cancel:
        emit selectionCancelled();
        close();
        break;

#ifdef Q_OS_MACOS
    case ToolbarButton::OCR:
        performOCR();
        break;
#endif

    case ToolbarButton::Pin:
        finishSelection();
        break;

    case ToolbarButton::Save:
        saveToFile();
        break;

    case ToolbarButton::Copy:
        copyToClipboard();
        break;

    default:
        break;
    }
}

QPixmap RegionSelector::getSelectedRegion()
{
    QRect sel = m_selectionRect.normalized();

    // 使用 device pixels 座標來裁切
    QPixmap selectedRegion = m_backgroundPixmap.copy(
        static_cast<int>(sel.x() * m_devicePixelRatio),
        static_cast<int>(sel.y() * m_devicePixelRatio),
        static_cast<int>(sel.width() * m_devicePixelRatio),
        static_cast<int>(sel.height() * m_devicePixelRatio)
    );

    // KEY FIX: Set DPR BEFORE painting so Qt handles scaling automatically
    // This ensures annotation sizes (radius, pen width, font) stay in logical units
    // while positions are automatically converted to device pixels by Qt
    selectedRegion.setDevicePixelRatio(m_devicePixelRatio);

    // Draw annotations onto the selected region
    if (!m_annotationLayer->isEmpty()) {
        QPainter painter(&selectedRegion);
        painter.setRenderHint(QPainter::Antialiasing, true);

        // Only translate - no manual scale() needed
        // Qt automatically handles DPR conversion because we set devicePixelRatio above
        // Annotation at screen position P will appear at (P - sel.topLeft) in logical coords
        painter.translate(-sel.x(), -sel.y());

        qDebug() << "Drawing annotations: sel=" << sel << "DPR=" << m_devicePixelRatio
                 << "pixmap=" << selectedRegion.size()
                 << "transform=" << painter.transform();
        m_annotationLayer->draw(painter);
    }

    return selectedRegion;
}

void RegionSelector::copyToClipboard()
{
    QPixmap selectedRegion = getSelectedRegion();
    QGuiApplication::clipboard()->setPixmap(selectedRegion);
    qDebug() << "RegionSelector: Copied to clipboard";

    emit copyRequested(selectedRegion);
    close();
}

void RegionSelector::saveToFile()
{
    QPixmap selectedRegion = getSelectedRegion();

    QString picturesPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString defaultName = QString("%1/Screenshot_%2.png").arg(picturesPath).arg(timestamp);

    QString filePath = QFileDialog::getSaveFileName(
        this,
        "Save Screenshot",
        defaultName,
        "PNG Image (*.png);;JPEG Image (*.jpg *.jpeg);;All Files (*)"
    );

    if (!filePath.isEmpty()) {
        if (selectedRegion.save(filePath)) {
            qDebug() << "RegionSelector: Saved to" << filePath;
        }
        else {
            qDebug() << "RegionSelector: Failed to save to" << filePath;
        }
        emit saveRequested(selectedRegion);
        close();
    }
}

void RegionSelector::finishSelection()
{
    QPixmap selectedRegion = getSelectedRegion();

    // 直接計算全局座標：螢幕左上角 + 選區左上角
    QPoint globalPos = m_currentScreen->geometry().topLeft() + m_selectionRect.normalized().topLeft();

    qDebug() << "finishSelection: selectionRect=" << m_selectionRect.normalized()
             << "globalPos=" << globalPos;

    emit regionSelected(selectedRegion, globalPos);
    close();
}

void RegionSelector::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_selectionComplete) {
            // Check if clicked on color palette
            if (shouldShowColorPalette() && m_colorPalette->handleClick(event->pos())) {
                update();
                return;
            }

            // Check if clicked on toolbar
            int buttonIdx = getButtonAtPosition(event->pos());
            if (buttonIdx >= 0) {
                handleToolbarClick(static_cast<ToolbarButton>(buttonIdx));
                return;
            }

            // Check if we're using an annotation tool and clicking inside selection
            QRect sel = m_selectionRect.normalized();
            if (isAnnotationTool(m_currentTool) && m_currentTool != ToolbarButton::Selection && sel.contains(event->pos())) {
                // Handle Text tool specially - show input dialog
                if (m_currentTool == ToolbarButton::Text) {
                    showTextInputDialog(event->pos());
                    return;
                }
                // Handle StepBadge tool - place badge on click
                if (m_currentTool == ToolbarButton::StepBadge) {
                    placeStepBadge(event->pos());
                    return;
                }
                // Start annotation drawing
                qDebug() << "Starting annotation with tool:" << static_cast<int>(m_currentTool) << "at pos:" << event->pos();
                startAnnotation(event->pos());
                return;
            }

            // Check for resize handles or move (Selection tool)
            if (m_currentTool == ToolbarButton::Selection) {
                ResizeHandle handle = getHandleAtPosition(event->pos());
                if (handle != ResizeHandle::None) {
                    if (handle == ResizeHandle::Center) {
                        // Start moving
                        m_isMoving = true;
                        m_isResizing = false;
                    } else {
                        // Start resizing
                        m_isResizing = true;
                        m_isMoving = false;
                    }
                    m_activeHandle = handle;
                    m_resizeStartPoint = event->pos();
                    m_originalRect = m_selectionRect;
                    update();
                    return;
                }

                // Click outside selection - cancel selection (same as ESC)
                qDebug() << "RegionSelector: Cancelled via click outside selection";
                emit selectionCancelled();
                close();
                return;
            }
        }
        else {
            m_isSelecting = true;
            m_startPoint = event->pos();
            m_currentPoint = m_startPoint;
            m_selectionRect = QRect();
        }
        update();
    }
    else if (event->button() == Qt::RightButton) {
        if (m_selectionComplete) {
            // If drawing, cancel current annotation
            if (m_isDrawing) {
                m_isDrawing = false;
                m_currentPath.clear();
                m_currentPencil.reset();
                m_currentMarker.reset();
                m_currentArrow.reset();
                m_currentRectangle.reset();
                m_currentMosaicStroke.reset();
                update();
                return;
            }
            // If resizing/moving, cancel
            if (m_isResizing || m_isMoving) {
                m_selectionRect = m_originalRect;
                m_isResizing = false;
                m_isMoving = false;
                m_activeHandle = ResizeHandle::None;
                update();
                return;
            }
            // Cancel selection, go back to selection mode
            m_selectionComplete = false;
            m_selectionRect = QRect();
            m_annotationLayer->clear();
            setCursor(Qt::CrossCursor);
            update();
        }
        else {
            emit selectionCancelled();
            close();
        }
    }
}

void RegionSelector::mouseMoveEvent(QMouseEvent* event)
{
    m_currentPoint = event->pos();

#ifdef Q_OS_MACOS
    // Window detection during hover (before any selection or dragging)
    if (!m_isSelecting && !m_selectionComplete && m_windowDetector) {
        updateWindowDetection(event->pos());
    }
#endif

    if (m_isSelecting) {
#ifdef Q_OS_MACOS
        // Clear window detection when dragging
        m_highlightedWindowRect = QRect();
        m_detectedWindow.reset();
#endif
        m_selectionRect = QRect(m_startPoint, m_currentPoint).normalized();
    }
    else if (m_isResizing) {
        // Update selection rect based on resize handle
        updateResize(event->pos());
    }
    else if (m_isMoving) {
        // Move selection rect
        QPoint delta = event->pos() - m_resizeStartPoint;
        m_selectionRect = m_originalRect.translated(delta);

        // Clamp to screen bounds
        if (m_selectionRect.left() < 0)
            m_selectionRect.moveLeft(0);
        if (m_selectionRect.top() < 0)
            m_selectionRect.moveTop(0);
        if (m_selectionRect.right() >= width())
            m_selectionRect.moveRight(width() - 1);
        if (m_selectionRect.bottom() >= height())
            m_selectionRect.moveBottom(height() - 1);
    }
    else if (m_isDrawing) {
        // Update current annotation
        updateAnnotation(event->pos());
    }
    else if (m_selectionComplete) {
        bool colorPaletteHovered = false;

        // Update hovered color swatch
        if (shouldShowColorPalette()) {
            if (m_colorPalette->updateHoveredSwatch(event->pos())) {
                if (m_colorPalette->contains(event->pos())) {
                    setCursor(Qt::PointingHandCursor);
                    colorPaletteHovered = true;
                }
            } else if (m_colorPalette->contains(event->pos())) {
                colorPaletteHovered = true;
            }
        }

        // Update hovered button
        int newHovered = getButtonAtPosition(event->pos());
        if (newHovered != m_hoveredButton) {
            m_hoveredButton = newHovered;
            if (m_hoveredButton >= 0) {
                setCursor(Qt::PointingHandCursor);
            }
            else if (colorPaletteHovered) {
                // Already set cursor for color swatch
            }
            else if (isAnnotationTool(m_currentTool) && m_currentTool != ToolbarButton::Selection) {
                setCursor(Qt::CrossCursor);
            }
            else {
                // Selection tool: update cursor based on handle position
                ResizeHandle handle = getHandleAtPosition(event->pos());
                updateCursorForHandle(handle);
            }
        }
        else if (m_hoveredButton < 0 && !colorPaletteHovered && m_currentTool == ToolbarButton::Selection) {
            // Update cursor for resize handles when not hovering button or color swatch
            ResizeHandle handle = getHandleAtPosition(event->pos());
            updateCursorForHandle(handle);
        }
    }

    update();
}

void RegionSelector::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_isSelecting) {
            m_isSelecting = false;

            QRect sel = m_selectionRect.normalized();
            if (sel.width() > 5 && sel.height() > 5) {
                // 有拖曳 - 使用拖曳選取的區域
                m_selectionComplete = true;
                setCursor(Qt::ArrowCursor);
                qDebug() << "RegionSelector: Selection complete via drag";
            }
#ifdef Q_OS_MACOS
            else if (m_detectedWindow.has_value() && m_highlightedWindowRect.isValid()) {
                // Click on detected window - use its bounds
                m_selectionRect = m_highlightedWindowRect;
                m_selectionComplete = true;
                setCursor(Qt::ArrowCursor);
                qDebug() << "RegionSelector: Selection complete via detected window:" << m_detectedWindow->windowTitle;

                // Clear detection state
                m_highlightedWindowRect = QRect();
                m_detectedWindow.reset();
            }
#endif
            else {
                // 點擊無拖曳且無偵測到視窗 - 選取整個螢幕（排除 menu bar）
                // 使用 availableGeometry() 取得不含系統 UI（menu bar、dock）的區域
                QRect screenGeom = m_currentScreen->geometry();
                QRect availableGeom = m_currentScreen->availableGeometry();

                // 計算 menu bar 高度（availableGeometry 與 geometry 的 top 差異）
                int menuBarHeight = availableGeom.top() - screenGeom.top();

                // 選取區域從 menu bar 下方開始，到螢幕底部
                // 注意：這裡使用相對於 widget 的座標（widget 覆蓋整個螢幕）
                m_selectionRect = QRect(0, menuBarHeight, screenGeom.width(), screenGeom.height() - menuBarHeight);
                m_selectionComplete = true;
                setCursor(Qt::ArrowCursor);
                qDebug() << "RegionSelector: Click without drag - selecting full screen (excluding menu bar height:" << menuBarHeight << ")";
            }

            update();
        }
        else if (m_isResizing || m_isMoving) {
            // Finish resize/move
            m_isResizing = false;
            m_isMoving = false;
            m_activeHandle = ResizeHandle::None;
            m_selectionRect = m_selectionRect.normalized();
            update();
        }
        else if (m_isDrawing) {
            // Finish annotation
            finishAnnotation();
            update();
        }
    }
}

void RegionSelector::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        // ESC 直接離開 capture mode
        qDebug() << "RegionSelector: Cancelled via Escape";
        emit selectionCancelled();
        close();
    }
    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_selectionComplete) {
            finishSelection();
        }
    }
    else if (event->matches(QKeySequence::Copy)) {
        if (m_selectionComplete) {
            copyToClipboard();
        }
    }
    else if (event->matches(QKeySequence::Save)) {
        if (m_selectionComplete) {
            saveToFile();
        }
    }
    else if (event->key() == Qt::Key_Shift) {
        // 切換 RGB/HEX 顯示格式
        m_showHexColor = !m_showHexColor;
        update();
    }
    else if (event->key() == Qt::Key_C && !m_selectionComplete) {
        // 複製顏色到剪貼板 (僅在未選取完成時有效)
        int deviceX = static_cast<int>(m_currentPoint.x() * m_devicePixelRatio);
        int deviceY = static_cast<int>(m_currentPoint.y() * m_devicePixelRatio);
        const QImage &img = m_backgroundImageCache;
        if (deviceX >= 0 && deviceX < img.width() && deviceY >= 0 && deviceY < img.height()) {
            QColor pixelColor = QColor(img.pixel(deviceX, deviceY));
            QString colorText;
            if (m_showHexColor) {
                colorText = QString("#%1%2%3")
                    .arg(pixelColor.red(), 2, 16, QChar('0'))
                    .arg(pixelColor.green(), 2, 16, QChar('0'))
                    .arg(pixelColor.blue(), 2, 16, QChar('0'));
            }
            else {
                colorText = QString("rgb(%1, %2, %3)")
                    .arg(pixelColor.red())
                    .arg(pixelColor.green())
                    .arg(pixelColor.blue());
            }
            QGuiApplication::clipboard()->setText(colorText);
            qDebug() << "RegionSelector: Copied color to clipboard:" << colorText;
        }
    }
    else if (event->matches(QKeySequence::Undo)) {
        if (m_selectionComplete && m_annotationLayer->canUndo()) {
            m_annotationLayer->undo();
            update();
        }
    }
    else if (event->matches(QKeySequence::Redo)) {
        if (m_selectionComplete && m_annotationLayer->canRedo()) {
            m_annotationLayer->redo();
            update();
        }
    }
}

bool RegionSelector::eventFilter(QObject* obj, QEvent* event)
{
    // Capture ESC key at application level to handle when window loses focus
    // (e.g., user clicks on macOS menu bar)
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            qDebug() << "RegionSelector: Cancelled via Escape (event filter)";
            emit selectionCancelled();
            close();
            return true;  // Event handled
        }
    }
    return QWidget::eventFilter(obj, event);
}

// ============================================================================
// Annotation Helper Functions
// ============================================================================

void RegionSelector::drawAnnotations(QPainter &painter)
{
    m_annotationLayer->draw(painter);
}

void RegionSelector::drawCurrentAnnotation(QPainter &painter)
{
    if (!m_isDrawing) return;

    if (m_currentPencil) {
        m_currentPencil->draw(painter);
    }
    else if (m_currentMarker) {
        m_currentMarker->draw(painter);
    }
    else if (m_currentArrow) {
        m_currentArrow->draw(painter);
    }
    else if (m_currentRectangle) {
        m_currentRectangle->draw(painter);
    }
    else if (m_currentMosaicStroke) {
        m_currentMosaicStroke->draw(painter);
    }
    else if (m_currentTool == ToolbarButton::Eraser && !m_eraserPath.isEmpty()) {
        // Draw eraser cursor at the last position
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);

        QPoint lastPos = m_eraserPath.last();
        int radius = ERASER_WIDTH / 2;

        // Draw semi-transparent circle
        painter.setPen(QPen(Qt::white, 2));
        painter.setBrush(QColor(255, 255, 255, 50));
        painter.drawEllipse(lastPos, radius, radius);

        painter.restore();
    }
}

void RegionSelector::startAnnotation(const QPoint &pos)
{
    m_isDrawing = true;
    m_drawStartPoint = pos;
    m_currentPath.clear();
    m_currentPath.append(pos);

    switch (m_currentTool) {
    case ToolbarButton::Pencil:
        m_currentPencil = std::make_unique<PencilStroke>(m_currentPath, m_annotationColor, m_annotationWidth);
        break;

    case ToolbarButton::Marker:
        m_currentMarker = std::make_unique<MarkerStroke>(m_currentPath, m_annotationColor, 20);
        break;

    case ToolbarButton::Arrow:
        m_currentArrow = std::make_unique<ArrowAnnotation>(pos, pos, m_annotationColor, m_annotationWidth);
        break;

    case ToolbarButton::Rectangle:
        m_currentRectangle = std::make_unique<RectangleAnnotation>(QRect(pos, pos), m_annotationColor, m_annotationWidth);
        break;

    case ToolbarButton::Mosaic:
        m_currentMosaicStroke = std::make_unique<MosaicStroke>(m_currentPath, m_backgroundPixmap, 15, 5);
        break;

    case ToolbarButton::Eraser:
        m_eraserPath.clear();
        m_eraserPath.append(pos);
        m_erasedItems.clear();
        // Immediately check for items to erase at start position
        {
            auto removed = m_annotationLayer->removeItemsIntersecting(pos, ERASER_WIDTH);
            for (auto &item : removed) {
                m_erasedItems.push_back(std::move(item));
            }
        }
        break;

    default:
        m_isDrawing = false;
        break;
    }

    update();
}

void RegionSelector::updateAnnotation(const QPoint &pos)
{
    if (!m_isDrawing) return;

    switch (m_currentTool) {
    case ToolbarButton::Pencil:
        if (m_currentPencil) {
            m_currentPath.append(pos);
            m_currentPencil->addPoint(pos);
        }
        break;

    case ToolbarButton::Marker:
        if (m_currentMarker) {
            m_currentPath.append(pos);
            m_currentMarker->addPoint(pos);
        }
        break;

    case ToolbarButton::Arrow:
        if (m_currentArrow) {
            m_currentArrow->setEnd(pos);
        }
        break;

    case ToolbarButton::Rectangle:
        if (m_currentRectangle) {
            m_currentRectangle->setRect(QRect(m_drawStartPoint, pos));
        }
        break;

    case ToolbarButton::Mosaic:
        if (m_currentMosaicStroke) {
            m_currentPath.append(pos);
            m_currentMosaicStroke->addPoint(pos);
        }
        break;

    case ToolbarButton::Eraser:
        m_eraserPath.append(pos);
        // Check for items to erase at this position
        {
            auto removed = m_annotationLayer->removeItemsIntersecting(pos, ERASER_WIDTH);
            for (auto &item : removed) {
                m_erasedItems.push_back(std::move(item));
            }
        }
        break;

    default:
        break;
    }

    update();
}

void RegionSelector::finishAnnotation()
{
    if (!m_isDrawing) return;

    m_isDrawing = false;

    // Add the annotation to the layer
    switch (m_currentTool) {
    case ToolbarButton::Pencil:
        if (m_currentPencil && m_currentPath.size() > 1) {
            m_annotationLayer->addItem(std::move(m_currentPencil));
        }
        m_currentPencil.reset();
        break;

    case ToolbarButton::Marker:
        if (m_currentMarker && m_currentPath.size() > 1) {
            m_annotationLayer->addItem(std::move(m_currentMarker));
        }
        m_currentMarker.reset();
        break;

    case ToolbarButton::Arrow:
        if (m_currentArrow) {
            m_annotationLayer->addItem(std::move(m_currentArrow));
        }
        m_currentArrow.reset();
        break;

    case ToolbarButton::Rectangle:
        if (m_currentRectangle) {
            m_annotationLayer->addItem(std::move(m_currentRectangle));
        }
        m_currentRectangle.reset();
        break;

    case ToolbarButton::Mosaic:
        if (m_currentMosaicStroke && m_currentPath.size() > 1) {
            m_annotationLayer->addItem(std::move(m_currentMosaicStroke));
        }
        m_currentMosaicStroke.reset();
        break;

    case ToolbarButton::Eraser:
        // If items were erased, add an ErasedItemsGroup for undo support
        if (!m_erasedItems.empty()) {
            auto erasedGroup = std::make_unique<ErasedItemsGroup>(std::move(m_erasedItems));
            m_annotationLayer->addItem(std::move(erasedGroup));
        }
        m_eraserPath.clear();
        m_erasedItems.clear();
        break;

    default:
        break;
    }

    m_currentPath.clear();
}

bool RegionSelector::isAnnotationTool(ToolbarButton tool) const
{
    switch (tool) {
    case ToolbarButton::Pencil:
    case ToolbarButton::Marker:
    case ToolbarButton::Arrow:
    case ToolbarButton::Rectangle:
    case ToolbarButton::Text:
    case ToolbarButton::Mosaic:
    case ToolbarButton::StepBadge:
    case ToolbarButton::Eraser:
        return true;
    default:
        return false;
    }
}

void RegionSelector::showTextInputDialog(const QPoint &pos)
{
    bool ok;
    QString text = QInputDialog::getText(this, "Add Text", "Enter text:",
                                          QLineEdit::Normal, QString(), &ok);
    if (ok && !text.isEmpty()) {
        QFont font;
        font.setPointSize(16);
        font.setBold(true);

        auto textAnnotation = std::make_unique<TextAnnotation>(pos, text, font, m_annotationColor);
        m_annotationLayer->addItem(std::move(textAnnotation));
        update();
    }
}

void RegionSelector::placeStepBadge(const QPoint &pos)
{
    int nextNumber = m_annotationLayer->countStepBadges() + 1;
    auto badge = std::make_unique<StepBadgeAnnotation>(pos, m_annotationColor, nextNumber);
    m_annotationLayer->addItem(std::move(badge));
    qDebug() << "Placed step badge" << nextNumber << "at" << pos;
    update();
}

// ============================================================================
// Selection Resize/Move Helper Functions
// ============================================================================

ResizeHandle RegionSelector::getHandleAtPosition(const QPoint &pos)
{
    if (!m_selectionComplete) return ResizeHandle::None;

    QRect sel = m_selectionRect.normalized();
    const int handleSize = 16;  // Hit area for handles

    // Define handle hit areas
    QRect topLeft(sel.left() - handleSize/2, sel.top() - handleSize/2, handleSize, handleSize);
    QRect top(sel.center().x() - handleSize/2, sel.top() - handleSize/2, handleSize, handleSize);
    QRect topRight(sel.right() - handleSize/2, sel.top() - handleSize/2, handleSize, handleSize);
    QRect left(sel.left() - handleSize/2, sel.center().y() - handleSize/2, handleSize, handleSize);
    QRect right(sel.right() - handleSize/2, sel.center().y() - handleSize/2, handleSize, handleSize);
    QRect bottomLeft(sel.left() - handleSize/2, sel.bottom() - handleSize/2, handleSize, handleSize);
    QRect bottom(sel.center().x() - handleSize/2, sel.bottom() - handleSize/2, handleSize, handleSize);
    QRect bottomRight(sel.right() - handleSize/2, sel.bottom() - handleSize/2, handleSize, handleSize);

    // Check corners first (higher priority)
    if (topLeft.contains(pos)) return ResizeHandle::TopLeft;
    if (topRight.contains(pos)) return ResizeHandle::TopRight;
    if (bottomLeft.contains(pos)) return ResizeHandle::BottomLeft;
    if (bottomRight.contains(pos)) return ResizeHandle::BottomRight;

    // Check edges
    if (top.contains(pos)) return ResizeHandle::Top;
    if (bottom.contains(pos)) return ResizeHandle::Bottom;
    if (left.contains(pos)) return ResizeHandle::Left;
    if (right.contains(pos)) return ResizeHandle::Right;

    // Check if inside selection (for move)
    if (sel.contains(pos)) return ResizeHandle::Center;

    return ResizeHandle::None;
}

void RegionSelector::updateResize(const QPoint &pos)
{
    QPoint delta = pos - m_resizeStartPoint;
    QRect newRect = m_originalRect;

    switch (m_activeHandle) {
    case ResizeHandle::TopLeft:
        newRect.setTopLeft(m_originalRect.topLeft() + delta);
        break;
    case ResizeHandle::Top:
        newRect.setTop(m_originalRect.top() + delta.y());
        break;
    case ResizeHandle::TopRight:
        newRect.setTopRight(m_originalRect.topRight() + delta);
        break;
    case ResizeHandle::Left:
        newRect.setLeft(m_originalRect.left() + delta.x());
        break;
    case ResizeHandle::Right:
        newRect.setRight(m_originalRect.right() + delta.x());
        break;
    case ResizeHandle::BottomLeft:
        newRect.setBottomLeft(m_originalRect.bottomLeft() + delta);
        break;
    case ResizeHandle::Bottom:
        newRect.setBottom(m_originalRect.bottom() + delta.y());
        break;
    case ResizeHandle::BottomRight:
        newRect.setBottomRight(m_originalRect.bottomRight() + delta);
        break;
    default:
        break;
    }

    // Ensure minimum size
    if (newRect.width() >= 10 && newRect.height() >= 10) {
        m_selectionRect = newRect;
    }
}

void RegionSelector::updateCursorForHandle(ResizeHandle handle)
{
    switch (handle) {
    case ResizeHandle::TopLeft:
    case ResizeHandle::BottomRight:
        setCursor(Qt::SizeFDiagCursor);
        break;
    case ResizeHandle::TopRight:
    case ResizeHandle::BottomLeft:
        setCursor(Qt::SizeBDiagCursor);
        break;
    case ResizeHandle::Top:
    case ResizeHandle::Bottom:
        setCursor(Qt::SizeVerCursor);
        break;
    case ResizeHandle::Left:
    case ResizeHandle::Right:
        setCursor(Qt::SizeHorCursor);
        break;
    case ResizeHandle::Center:
        setCursor(Qt::SizeAllCursor);
        break;
    default:
        setCursor(Qt::ArrowCursor);
        break;
    }
}

#ifdef Q_OS_MACOS
void RegionSelector::performOCR()
{
    if (!m_ocrManager || m_ocrInProgress || !m_selectionComplete) {
        if (!m_ocrManager) {
            qDebug() << "RegionSelector: OCR not available (requires macOS 10.15+)";
        }
        return;
    }

    m_ocrInProgress = true;
    update();

    qDebug() << "RegionSelector: Starting OCR recognition...";

    // Get the selected region (without annotations for OCR)
    QRect sel = m_selectionRect.normalized();
    QPixmap selectedRegion = m_backgroundPixmap.copy(
        static_cast<int>(sel.x() * m_devicePixelRatio),
        static_cast<int>(sel.y() * m_devicePixelRatio),
        static_cast<int>(sel.width() * m_devicePixelRatio),
        static_cast<int>(sel.height() * m_devicePixelRatio)
    );
    selectedRegion.setDevicePixelRatio(m_devicePixelRatio);

    QPointer<RegionSelector> safeThis = this;
    m_ocrManager->recognizeText(selectedRegion,
        [safeThis](bool success, const QString &text, const QString &error) {
            if (safeThis) {
                safeThis->onOCRComplete(success, text, error);
            } else {
                qDebug() << "RegionSelector: OCR completed but widget was destroyed, ignoring result";
            }
        });
}

void RegionSelector::onOCRComplete(bool success, const QString &text, const QString &error)
{
    m_ocrInProgress = false;

    if (success && !text.isEmpty()) {
        QGuiApplication::clipboard()->setText(text);
        qDebug() << "RegionSelector: OCR complete, copied" << text.length() << "characters to clipboard";
        QToolTip::showText(QCursor::pos(),
            tr("OCR: Copied %1 characters to clipboard").arg(text.length()),
            this, QRect(), 2000);
    } else {
        QString msg = error.isEmpty() ? tr("No text found") : error;
        qDebug() << "RegionSelector: OCR failed:" << msg;
        QToolTip::showText(QCursor::pos(),
            tr("OCR: %1").arg(msg),
            this, QRect(), 2000);
    }

    update();
}
#endif
