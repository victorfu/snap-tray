#include "RecordingRegionSelector.h"
#include "platform/WindowLevel.h"
#include "IconRenderer.h"
#include "GlassRenderer.h"
#include "ToolbarStyle.h"
#include "toolbar/ToolbarCore.h"
#include "cursor/CursorManager.h"

#include <QScreen>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QTimer>
#include <QDebug>
#include <QLinearGradient>
#include <QPointer>

RecordingRegionSelector::RecordingRegionSelector(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Window)
    , m_currentScreen(nullptr)
    , m_devicePixelRatio(1.0)
    , m_isSelecting(false)
    , m_selectionComplete(false)
    , m_toolbar(nullptr)
{
    setMouseTracking(true);
    CursorManager::instance().pushCursorForWidget(this, CursorContext::Selection, Qt::CrossCursor);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_TranslucentBackground);
    setupIcons();
    setupToolbar();

    // Connect to screen removal signal for graceful handling
    connect(qApp, &QGuiApplication::screenRemoved,
            this, [this](QScreen *screen) {
                if (m_currentScreen == screen || m_currentScreen.isNull()) {
                    qWarning() << "RecordingRegionSelector: Screen disconnected, closing";
                    emit cancelled();
                    close();
                }
            });
}

RecordingRegionSelector::~RecordingRegionSelector()
{
    // Clean up per-widget cursor state before destruction
    CursorManager::instance().clearAllForWidget(this);
}

void RecordingRegionSelector::setupIcons()
{
    IconRenderer& iconRenderer = IconRenderer::instance();
    iconRenderer.loadIcon("record", ":/icons/icons/record.svg");
    iconRenderer.loadIcon("cancel", ":/icons/icons/cancel.svg");
}

void RecordingRegionSelector::setupToolbar()
{
    m_toolbar = new ToolbarCore(this);

    // Configure buttons using Toolbar::ButtonConfig with builder pattern
    QVector<ToolbarCore::ButtonConfig> buttons = {
        ToolbarCore::ButtonConfig(ButtonStart, "record", tr("Start Recording (Enter)")).record(),
        ToolbarCore::ButtonConfig(ButtonCancel, "cancel", tr("Cancel (Esc)")).cancel()
    };
    m_toolbar->setButtons(buttons);

    // Set custom icon color provider for record/cancel buttons
    ToolbarStyleConfig styleConfig = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    m_toolbar->setStyleConfig(styleConfig);

    m_toolbar->setIconColorProvider([styleConfig](int buttonId, bool isActive, bool isHovered) -> QColor {
        Q_UNUSED(isActive)
        if (buttonId == ButtonStart) {
            return styleConfig.iconRecordColor;
        } else if (buttonId == ButtonCancel) {
            return isHovered ? styleConfig.iconCancelColor : styleConfig.iconNormalColor;
        }
        return styleConfig.iconNormalColor;
    });
}

void RecordingRegionSelector::initializeForScreen(QScreen *screen)
{
    m_currentScreen = screen;
    if (m_currentScreen.isNull()) {
        qWarning() << "RecordingRegionSelector: No valid screen available";
        emit cancelled();
        QTimer::singleShot(0, this, &QWidget::close);
        return;
    }
    m_devicePixelRatio = m_currentScreen->devicePixelRatio();

    qDebug() << "=== RecordingRegionSelector::initializeForScreen ===";
    qDebug() << "Screen name:" << m_currentScreen->name();
    qDebug() << "Screen geometry:" << m_currentScreen->geometry();
    qDebug() << "Screen devicePixelRatio:" << m_devicePixelRatio;
}

void RecordingRegionSelector::initializeWithRegion(QScreen *screen, const QRect &region)
{
    m_currentScreen = screen;
    if (m_currentScreen.isNull()) {
        qWarning() << "RecordingRegionSelector: No valid screen available";
        emit cancelled();
        QTimer::singleShot(0, this, &QWidget::close);
        return;
    }
    m_devicePixelRatio = m_currentScreen->devicePixelRatio();

    // Convert global region to local coordinates
    QRect screenGeom = m_currentScreen->geometry();
    m_selectionRect = region.translated(-screenGeom.topLeft());

    // Set selection as complete (skip the selection step)
    m_selectionComplete = true;
    m_isSelecting = false;
    CursorManager::instance().popCursorForWidget(this, CursorContext::Selection);

    qDebug() << "=== RecordingRegionSelector::initializeWithRegion ===";
    qDebug() << "Screen name:" << screen->name();
    qDebug() << "Screen geometry:" << screenGeom;
    qDebug() << "Global region:" << region;
    qDebug() << "Local selection rect:" << m_selectionRect;

    // Update toolbar position after the widget is shown
    QTimer::singleShot(0, this, [this]() {
        updateToolbarPosition();
        update();
    });
}

void RecordingRegionSelector::updateToolbarPosition()
{
    if (m_selectionRect.isEmpty()) return;

    // Position toolbar centered below the selection
    int centerX = m_selectionRect.center().x();
    int bottomY = m_selectionRect.bottom() + 8 + 32; // 8px margin + toolbar height

    // Check if it would go off screen
    if (bottomY > height() - 60) {
        // Position above instead
        bottomY = m_selectionRect.top() - 8;
    }

    // If still off screen, position inside selection
    if (bottomY - 32 < 10) {
        bottomY = m_selectionRect.top() + 10 + 32;
    }

    m_toolbar->setPosition(centerX, bottomY);
    m_toolbar->setViewportWidth(width());
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
            updateToolbarPosition();
            m_toolbar->draw(painter);
            m_toolbar->drawTooltip(painter);
        }
    } else {
        // Draw crosshair when not yet selecting
        drawCrosshair(painter);
    }

    // Draw help text at bottom
    drawInstructions(painter);
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

    const int borderWidth = 3;
    QRectF selRect = QRectF(m_selectionRect).adjusted(0.5, 0.5, -0.5, -0.5);

    if (m_selectionComplete) {
        // Draw outer glow effect (sharp corners)
        for (int i = 3; i >= 1; --i) {
            QColor glowColor(88, 86, 214, 25 / i);  // Indigo with fading alpha
            QPen glowPen(glowColor, borderWidth + i * 2);
            glowPen.setJoinStyle(Qt::MiterJoin);
            painter.setPen(glowPen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(selRect);
        }

        // Create gradient for the border (blue to purple)
        QLinearGradient gradient(selRect.topLeft(), selRect.bottomRight());
        gradient.setColorAt(0.0, QColor(0, 122, 255));      // Apple Blue
        gradient.setColorAt(0.5, QColor(88, 86, 214));      // Indigo
        gradient.setColorAt(1.0, QColor(175, 82, 222));     // Purple

        QPen pen(QBrush(gradient), borderWidth);
        pen.setJoinStyle(Qt::MiterJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(selRect);
    } else {
        // Blue dashed line while selecting (sharp corners)
        QPen pen(QColor(0, 122, 255), 1);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(selRect);
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

    QString dimensionText = tr("%1 x %2")
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

    // Draw background with glass effect
    ToolbarStyleConfig config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    ToolbarStyleConfig dimConfig = config;
    dimConfig.shadowOffsetY = 2;
    dimConfig.shadowBlurRadius = 4;
    GlassRenderer::drawGlassPanel(painter, textRect, dimConfig, 4);

    // Draw text
    painter.setPen(config.tooltipText);
    painter.drawText(textRect, Qt::AlignCenter, dimensionText);
}

void RecordingRegionSelector::drawInstructions(QPainter &painter)
{
    QString helpText;
    if (m_selectionComplete) {
        helpText = tr("Press Enter or click Start Recording, Escape to cancel");
    } else if (m_isSelecting) {
        helpText = tr("Release to confirm selection");
    } else {
        helpText = tr("Click and drag to select recording region");
    }

    QFont font = painter.font();
    font.setPointSize(13);
    painter.setFont(font);

    QFontMetrics fm(font);
    QRect textRect = fm.boundingRect(helpText);
    textRect.adjust(-12, -6, 12, 6);
    textRect.moveCenter(QPoint(width() / 2, height() - 40));

    // Draw background with glass effect
    ToolbarStyleConfig config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    ToolbarStyleConfig helpConfig = config;
    helpConfig.shadowOffsetY = 2;
    helpConfig.shadowBlurRadius = 6;
    GlassRenderer::drawGlassPanel(painter, textRect, helpConfig, 6);

    // Draw text
    painter.setPen(config.tooltipText);
    painter.drawText(textRect, Qt::AlignCenter, helpText);
}

void RecordingRegionSelector::handleCancel()
{
    // Check if we have a valid selection to return to capture mode
    if (m_selectionComplete && !m_selectionRect.isEmpty() && !m_currentScreen.isNull()) {
        QRect globalRect = m_selectionRect.normalized();
        globalRect.translate(m_currentScreen->geometry().topLeft());
        emit cancelledWithRegion(globalRect, m_currentScreen.data());
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
            int buttonIndex = m_toolbar->buttonAtPosition(event->pos());
            if (buttonIndex >= 0) {
                int buttonId = m_toolbar->buttonIdAt(buttonIndex);
                if (buttonId == ButtonStart) {
                    finishSelection();
                    return;
                } else if (buttonId == ButtonCancel) {
                    handleCancel();
                    return;
                }
            }

            // Allow re-selection by clicking outside current selection and toolbar
            if (!m_selectionRect.contains(event->pos()) && !m_toolbar->contains(event->pos())) {
                m_selectionComplete = false;
                m_startPoint = event->pos();
                m_currentPoint = event->pos();
                m_isSelecting = true;
                m_selectionRect = QRect();
                m_toolbar->updateHoveredButton(QPoint(-1, -1));
                CursorManager::instance().popCursorForWidget(this, CursorContext::Hover);
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
        bool hoverChanged = m_toolbar->updateHoveredButton(event->pos());
        auto& cm = CursorManager::instance();
        if (m_toolbar->contains(event->pos())) {
            cm.pushCursorForWidget(this, CursorContext::Hover, Qt::ArrowCursor);
        } else {
            cm.popCursorForWidget(this, CursorContext::Hover);
        }

        if (hoverChanged) {
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
                CursorManager::instance().popCursorForWidget(this, CursorContext::Selection);
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
    // Clear hover state when leaving the widget
    bool hoverChanged = m_toolbar->updateHoveredButton(QPoint(-1, -1));
    CursorManager::instance().popCursorForWidget(this, CursorContext::Hover);
    if (hoverChanged) {
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

    if (m_currentScreen.isNull()) {
        qWarning() << "RecordingRegionSelector: Screen invalid, cannot finish selection";
        emit cancelled();
        close();
        return;
    }

    // Convert to global coordinates
    QRect globalRect = m_selectionRect;
    globalRect.translate(m_currentScreen->geometry().topLeft());

    qDebug() << "RecordingRegionSelector: Selection complete, region:" << globalRect;

    emit regionSelected(globalRect, m_currentScreen.data());
    close();
}
