#include "RecordingRegionSelector.h"
#include "platform/WindowLevel.h"
#include "IconRenderer.h"
#include "GlassRenderer.h"
#include "ToolbarStyle.h"

#include <QScreen>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QTimer>
#include <QDebug>
#include <QLinearGradient>

RecordingRegionSelector::RecordingRegionSelector(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Window)
    , m_currentScreen(nullptr)
    , m_devicePixelRatio(1.0)
    , m_isSelecting(false)
    , m_selectionComplete(false)
    , m_hoveredButton(ButtonNone)
{
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_TranslucentBackground);
    setupIcons();
}

RecordingRegionSelector::~RecordingRegionSelector()
{
}

void RecordingRegionSelector::setupIcons()
{
    IconRenderer& iconRenderer = IconRenderer::instance();
    iconRenderer.loadIcon("record", ":/icons/icons/record.svg");
    iconRenderer.loadIcon("cancel", ":/icons/icons/cancel.svg");
}

void RecordingRegionSelector::initializeForScreen(QScreen *screen)
{
    m_currentScreen = screen;
    m_devicePixelRatio = screen->devicePixelRatio();

    qDebug() << "=== RecordingRegionSelector::initializeForScreen ===";
    qDebug() << "Screen name:" << screen->name();
    qDebug() << "Screen geometry:" << screen->geometry();
    qDebug() << "Screen devicePixelRatio:" << m_devicePixelRatio;
}

void RecordingRegionSelector::initializeWithRegion(QScreen *screen, const QRect &region)
{
    m_currentScreen = screen;
    m_devicePixelRatio = screen->devicePixelRatio();

    // Convert global region to local coordinates
    QRect screenGeom = screen->geometry();
    m_selectionRect = region.translated(-screenGeom.topLeft());

    // Set selection as complete (skip the selection step)
    m_selectionComplete = true;
    m_isSelecting = false;
    setCursor(Qt::ArrowCursor);

    qDebug() << "=== RecordingRegionSelector::initializeWithRegion ===";
    qDebug() << "Screen name:" << screen->name();
    qDebug() << "Screen geometry:" << screenGeom;
    qDebug() << "Global region:" << region;
    qDebug() << "Local selection rect:" << m_selectionRect;

    // Update toolbar position after the widget is shown
    QTimer::singleShot(0, this, [this]() {
        updateButtonRects();
        update();
    });
}

void RecordingRegionSelector::updateButtonRects()
{
    if (m_selectionRect.isEmpty()) return;

    // Calculate toolbar width: 2 buttons + padding
    int toolbarWidth = 2 * BUTTON_WIDTH + BUTTON_SPACING + 20;

    // Position below the selection rectangle, centered
    int x = m_selectionRect.center().x() - toolbarWidth / 2;
    int y = m_selectionRect.bottom() + 20;

    // Keep on screen
    if (x < 10) x = 10;
    if (x + toolbarWidth > width() - 10) {
        x = width() - toolbarWidth - 10;
    }

    // If below would be off screen, position above
    if (y + TOOLBAR_HEIGHT > height() - 60) {
        y = m_selectionRect.top() - TOOLBAR_HEIGHT - 20;
    }

    // If still off screen, position inside selection
    if (y < 10) {
        y = m_selectionRect.top() + 10;
    }

    m_toolbarRect = QRect(x, y, toolbarWidth, TOOLBAR_HEIGHT);

    // Button positions within toolbar
    int btnY = m_toolbarRect.top() + (TOOLBAR_HEIGHT - BUTTON_WIDTH + 4) / 2;
    int btnX = m_toolbarRect.left() + 10;

    m_startRect = QRect(btnX, btnY, BUTTON_WIDTH, BUTTON_WIDTH - 4);
    btnX += BUTTON_WIDTH + BUTTON_SPACING;
    m_cancelRect = QRect(btnX, btnY, BUTTON_WIDTH, BUTTON_WIDTH - 4);
}

void RecordingRegionSelector::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);

    // Draw semi-transparent overlay (only outside selection area)
    drawOverlay(painter);

    // Draw selection rectangle
    if (m_isSelecting || m_selectionComplete) {
        drawSelection(painter);
        drawDimensionLabel(painter);

        if (m_selectionComplete) {
            updateButtonRects();
            drawToolbar(painter);
            drawTooltip(painter);
        }
    } else {
        // Draw crosshair when not yet selecting
        drawCrosshair(painter);
    }

    // Draw help text at bottom
    drawInstructions(painter);
}

void RecordingRegionSelector::drawToolbar(QPainter &painter)
{
    painter.setRenderHint(QPainter::Antialiasing);

    ToolbarStyleConfig config = ToolbarStyleConfig::getDarkStyle();

    // Draw glass panel background
    GlassRenderer::drawGlassPanel(painter, m_toolbarRect, config, 10);

    // Draw start button
    {
        bool isHovered = (m_hoveredButton == ButtonStart);
        if (isHovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(config.hoverBackgroundColor);
            painter.drawRoundedRect(m_startRect.adjusted(2, 2, -2, -2), 4, 4);
        }
        QColor iconColor = config.iconRecordColor;
        IconRenderer::instance().renderIcon(painter, m_startRect, "record", iconColor);
    }

    // Draw cancel button
    {
        bool isHovered = (m_hoveredButton == ButtonCancel);
        if (isHovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(config.hoverBackgroundColor);
            painter.drawRoundedRect(m_cancelRect.adjusted(2, 2, -2, -2), 4, 4);
        }
        QColor iconColor = isHovered ? config.iconCancelColor : config.iconNormalColor;
        IconRenderer::instance().renderIcon(painter, m_cancelRect, "cancel", iconColor);
    }
}

void RecordingRegionSelector::drawTooltip(QPainter &painter)
{
    if (m_hoveredButton == ButtonNone) return;

    QString tooltip = tooltipForButton(static_cast<ButtonId>(m_hoveredButton));
    if (tooltip.isEmpty()) return;

    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);

    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(tooltip);
    textRect.adjust(-8, -4, 8, 4);

    QRect btnRect;
    switch (m_hoveredButton) {
    case ButtonStart: btnRect = m_startRect; break;
    case ButtonCancel: btnRect = m_cancelRect; break;
    default: return;
    }

    int tooltipX = btnRect.center().x() - textRect.width() / 2;
    int tooltipY = m_toolbarRect.top() - textRect.height() - 6;

    if (tooltipX < 5) tooltipX = 5;
    if (tooltipX + textRect.width() > width() - 5) {
        tooltipX = width() - textRect.width() - 5;
    }

    textRect.moveTo(tooltipX, tooltipY);

    ToolbarStyleConfig tooltipConfig = ToolbarStyleConfig::getDarkStyle();
    tooltipConfig.shadowOffsetY = 2;
    tooltipConfig.shadowBlurRadius = 6;
    GlassRenderer::drawGlassPanel(painter, textRect, tooltipConfig, 4);

    painter.setPen(tooltipConfig.tooltipText);
    painter.drawText(textRect, Qt::AlignCenter, tooltip);
}

QString RecordingRegionSelector::tooltipForButton(ButtonId button) const
{
    switch (button) {
    case ButtonStart:
        return tr("Start Recording (Enter)");
    case ButtonCancel:
        return tr("Cancel (Esc)");
    default:
        return QString();
    }
}

int RecordingRegionSelector::buttonAtPosition(const QPoint &pos) const
{
    if (!m_selectionComplete) return ButtonNone;
    if (m_startRect.contains(pos)) return ButtonStart;
    if (m_cancelRect.contains(pos)) return ButtonCancel;
    return ButtonNone;
}

void RecordingRegionSelector::drawOverlay(QPainter &painter)
{
    QColor dimColor(0, 0, 0, 100);

    if ((m_isSelecting || m_selectionComplete) && !m_selectionRect.isEmpty()) {
        // Draw overlay only OUTSIDE the selection - selection area stays transparent
        QRect sel = m_selectionRect.normalized();

        // Top region
        painter.fillRect(QRect(0, 0, width(), sel.top()), dimColor);
        // Bottom region
        painter.fillRect(QRect(0, sel.bottom() + 1, width(), height() - sel.bottom() - 1), dimColor);
        // Left region
        painter.fillRect(QRect(0, sel.top(), sel.left(), sel.height()), dimColor);
        // Right region
        painter.fillRect(QRect(sel.right() + 1, sel.top(), width() - sel.right() - 1, sel.height()), dimColor);
    } else {
        // No selection yet - entire screen is dimmed
        painter.fillRect(rect(), dimColor);
    }
}

void RecordingRegionSelector::drawSelection(QPainter &painter)
{
    if (m_selectionRect.isEmpty()) {
        return;
    }

    painter.setRenderHint(QPainter::Antialiasing);

    const int cornerRadius = 10;
    const int borderWidth = 3;
    QRectF selRect = QRectF(m_selectionRect).adjusted(0.5, 0.5, -0.5, -0.5);

    if (m_selectionComplete) {
        // Draw outer glow effect
        for (int i = 3; i >= 1; --i) {
            QColor glowColor(88, 86, 214, 25 / i);  // Indigo with fading alpha
            QPen glowPen(glowColor, borderWidth + i * 2);
            glowPen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(glowPen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(selRect, cornerRadius, cornerRadius);
        }

        // Create gradient for the border (blue to purple)
        QLinearGradient gradient(selRect.topLeft(), selRect.bottomRight());
        gradient.setColorAt(0.0, QColor(0, 122, 255));      // Apple Blue
        gradient.setColorAt(0.5, QColor(88, 86, 214));      // Indigo
        gradient.setColorAt(1.0, QColor(175, 82, 222));     // Purple

        QPen pen(QBrush(gradient), borderWidth);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(selRect, cornerRadius, cornerRadius);
    } else {
        // Blue dashed line while selecting
        QPen pen(QColor(0, 122, 255), 1);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(selRect, cornerRadius, cornerRadius);
    }
}

void RecordingRegionSelector::drawCrosshair(QPainter &painter)
{
    QPoint pos = mapFromGlobal(QCursor::pos());

    painter.setPen(QPen(QColor(255, 255, 255, 200), 1));

    // Horizontal line
    painter.drawLine(0, pos.y(), width(), pos.y());

    // Vertical line
    painter.drawLine(pos.x(), 0, pos.x(), height());
}

void RecordingRegionSelector::drawDimensionLabel(QPainter &painter)
{
    if (m_selectionRect.isEmpty()) {
        return;
    }

    QString dimensionText = QString("%1 x %2")
        .arg(m_selectionRect.width())
        .arg(m_selectionRect.height());

    QFont font = painter.font();
    font.setPointSize(12);
    font.setBold(true);
    painter.setFont(font);

    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(dimensionText);
    textRect.adjust(-8, -4, 8, 4);

    // Position above the selection rectangle
    int x = m_selectionRect.center().x() - textRect.width() / 2;
    int y = m_selectionRect.top() - textRect.height() - 8;

    // Keep on screen
    if (y < 5) {
        y = m_selectionRect.bottom() + 8;
    }
    if (x < 5) {
        x = 5;
    }
    if (x + textRect.width() > width() - 5) {
        x = width() - textRect.width() - 5;
    }

    textRect.moveTo(x, y);

    // Draw background
    painter.fillRect(textRect, QColor(0, 0, 0, 180));

    // Draw text
    painter.setPen(Qt::white);
    painter.drawText(textRect, Qt::AlignCenter, dimensionText);
}

void RecordingRegionSelector::drawInstructions(QPainter &painter)
{
    QString helpText;
    if (m_selectionComplete) {
        helpText = "Press Enter or click Start Recording, Escape to cancel";
    } else if (m_isSelecting) {
        helpText = "Release to confirm selection";
    } else {
        helpText = "Click and drag to select recording region";
    }

    QFont font = painter.font();
    font.setPointSize(13);
    painter.setFont(font);

    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(helpText);
    textRect.adjust(-12, -6, 12, 6);
    textRect.moveCenter(QPoint(width() / 2, height() - 40));

    // Background for readability
    painter.fillRect(textRect, QColor(0, 0, 0, 200));

    // Draw text
    painter.setPen(Qt::white);
    painter.drawText(textRect, Qt::AlignCenter, helpText);
}

void RecordingRegionSelector::handleCancel()
{
    // Check if we have a valid selection to return to capture mode
    if (m_selectionComplete && !m_selectionRect.isEmpty()) {
        QRect globalRect = m_selectionRect.normalized();
        globalRect.translate(m_currentScreen->geometry().topLeft());
        emit cancelledWithRegion(globalRect, m_currentScreen);
    } else {
        // No valid selection, just cancel without returning to capture
        emit cancelled();
    }
    close();
}

void RecordingRegionSelector::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        qDebug() << "=== MousePress ===";
        qDebug() << "event->pos() (widget local):" << event->pos();
        qDebug() << "event->globalPosition():" << event->globalPosition();
        qDebug() << "Widget geometry:" << geometry();

        // Check if clicking on toolbar buttons
        if (m_selectionComplete) {
            int button = buttonAtPosition(event->pos());
            if (button == ButtonStart) {
                finishSelection();
                return;
            } else if (button == ButtonCancel) {
                handleCancel();
                return;
            }

            // Allow re-selection by clicking outside current selection and toolbar
            if (!m_selectionRect.contains(event->pos()) && !m_toolbarRect.contains(event->pos())) {
                m_selectionComplete = false;
                m_startPoint = event->pos();
                m_currentPoint = event->pos();
                m_isSelecting = true;
                m_selectionRect = QRect();
                m_hoveredButton = ButtonNone;
            }
        } else {
            m_startPoint = event->pos();
            m_currentPoint = event->pos();
            m_isSelecting = true;
        }
        update();
    }
}

void RecordingRegionSelector::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isSelecting) {
        m_currentPoint = event->pos();

        // Calculate selection rectangle
        m_selectionRect = QRect(m_startPoint, m_currentPoint).normalized();

        // Constrain to widget bounds
        m_selectionRect = m_selectionRect.intersected(rect());

        update();
    } else if (m_selectionComplete) {
        // Update hover state for toolbar buttons
        int newHovered = buttonAtPosition(event->pos());
        if (newHovered != m_hoveredButton) {
            m_hoveredButton = newHovered;
            if (m_hoveredButton != ButtonNone) {
                setCursor(Qt::PointingHandCursor);
            } else if (!m_selectionRect.contains(event->pos())) {
                setCursor(Qt::CrossCursor);
            } else {
                setCursor(Qt::ArrowCursor);
            }
            update();
        }
    } else {
        // Update crosshair position
        update();
    }
}

void RecordingRegionSelector::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_isSelecting) {
            m_isSelecting = false;

            qDebug() << "=== MouseRelease ===";
            qDebug() << "event->pos() (widget local):" << event->pos();
            qDebug() << "m_selectionRect (widget local):" << m_selectionRect;
            qDebug() << "m_startPoint:" << m_startPoint;
            qDebug() << "m_currentPoint:" << m_currentPoint;

            // Ensure minimum size
            if (m_selectionRect.width() >= 10 && m_selectionRect.height() >= 10) {
                m_selectionComplete = true;
                setCursor(Qt::ArrowCursor);
            } else {
                // Too small, reset
                m_selectionRect = QRect();
            }

            update();
        }
    }
}

void RecordingRegionSelector::leaveEvent(QEvent *event)
{
    if (m_hoveredButton != ButtonNone) {
        m_hoveredButton = ButtonNone;
        update();
    }
    QWidget::leaveEvent(event);
}

void RecordingRegionSelector::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
        case Qt::Key_Escape:
            handleCancel();
            break;

        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (m_selectionComplete) {
                finishSelection();
            }
            break;

        default:
            QWidget::keyPressEvent(event);
            break;
    }
}

void RecordingRegionSelector::focusOutEvent(QFocusEvent *event)
{
    Q_UNUSED(event);
    // Reclaim window level to stay above other windows on macOS
    raiseWindowAboveMenuBar(this);
    activateWindow();
}

void RecordingRegionSelector::finishSelection()
{
    if (!m_selectionComplete || m_selectionRect.isEmpty()) {
        return;
    }

    // Convert to global coordinates
    QRect globalRect = m_selectionRect;
    globalRect.translate(m_currentScreen->geometry().topLeft());

    qDebug() << "RecordingRegionSelector: Selection complete, region:" << globalRect;

    emit regionSelected(globalRect, m_currentScreen);
    close();
}
