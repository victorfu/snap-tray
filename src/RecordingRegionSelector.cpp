#include "RecordingRegionSelector.h"
#include "platform/WindowLevel.h"

#include <QScreen>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QPushButton>
#include <QHBoxLayout>
#include <QTimer>
#include <QDebug>
#include <QLinearGradient>

RecordingRegionSelector::RecordingRegionSelector(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Window)
    , m_currentScreen(nullptr)
    , m_devicePixelRatio(1.0)
    , m_isSelecting(false)
    , m_selectionComplete(false)
    , m_buttonContainer(nullptr)
    , m_startButton(nullptr)
    , m_cancelButton(nullptr)
{
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_TranslucentBackground);
    setupButtons();
}

RecordingRegionSelector::~RecordingRegionSelector()
{
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

    // Position and show buttons after the widget is shown
    // We need to defer this because the widget might not be sized yet
    QTimer::singleShot(0, this, [this]() {
        if (m_buttonContainer) {
            positionButtons();
            m_buttonContainer->show();
            m_buttonContainer->raise();
        }
        update();
    });
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

        // Draw corner handles
        int handleSize = 10;
        QColor handleColor(175, 82, 222);  // Purple
        painter.setBrush(handleColor);
        painter.setPen(Qt::NoPen);

        QRect r = m_selectionRect;
        // Corners - rounded rectangles
        painter.drawRoundedRect(r.left() - handleSize/2, r.top() - handleSize/2, handleSize, handleSize, 3, 3);
        painter.drawRoundedRect(r.right() - handleSize/2, r.top() - handleSize/2, handleSize, handleSize, 3, 3);
        painter.drawRoundedRect(r.left() - handleSize/2, r.bottom() - handleSize/2, handleSize, handleSize, 3, 3);
        painter.drawRoundedRect(r.right() - handleSize/2, r.bottom() - handleSize/2, handleSize, handleSize, 3, 3);
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

void RecordingRegionSelector::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        qDebug() << "=== MousePress ===";
        qDebug() << "event->pos() (widget local):" << event->pos();
        qDebug() << "event->globalPosition():" << event->globalPosition();
        qDebug() << "Widget geometry:" << geometry();

        if (m_selectionComplete) {
            // Allow re-selection by clicking outside current selection
            if (!m_selectionRect.contains(event->pos())) {
                m_selectionComplete = false;
                m_startPoint = event->pos();
                m_currentPoint = event->pos();
                m_isSelecting = true;
                m_selectionRect = QRect();

                // Hide buttons when re-selecting
                if (m_buttonContainer) {
                    m_buttonContainer->hide();
                }
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
    } else if (!m_selectionComplete) {
        // Update crosshair position
        update();
    }
}

void RecordingRegionSelector::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_isSelecting) {
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

            // Show and position the buttons
            if (m_buttonContainer) {
                positionButtons();
                m_buttonContainer->show();
                m_buttonContainer->raise();
            }
        } else {
            // Too small, reset
            m_selectionRect = QRect();
            if (m_buttonContainer) {
                m_buttonContainer->hide();
            }
        }

        update();
    }
}

void RecordingRegionSelector::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
        case Qt::Key_Escape: {
            // Convert local rect to global coordinates and emit with region
            QRect globalRect = m_selectionRect.normalized();
            globalRect.translate(m_currentScreen->geometry().topLeft());
            emit cancelledWithRegion(globalRect, m_currentScreen);
            close();
            break;
        }

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

void RecordingRegionSelector::setupButtons()
{
    // Create button container with gradient border style
    m_buttonContainer = new QWidget(this);
    m_buttonContainer->setStyleSheet(
        "background-color: rgba(25, 25, 28, 245);"
        "border: 1px solid rgba(88, 86, 214, 0.5);"
        "border-radius: 10px;");
    m_buttonContainer->hide();

    QHBoxLayout *layout = new QHBoxLayout(m_buttonContainer);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(12);

    // Start Recording button (blue-purple gradient style)
    m_startButton = new QPushButton("Start Recording", m_buttonContainer);
    m_startButton->setStyleSheet(
        "QPushButton {"
        "  background-color: #5856D6;"
        "  color: white;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 9px 22px;"
        "  font-weight: 600;"
        "  font-size: 14px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #6D6BE0;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #4A48C0;"
        "}");
    connect(m_startButton, &QPushButton::clicked, this, &RecordingRegionSelector::finishSelection);
    layout->addWidget(m_startButton);

    // Cancel button (transparent style)
    m_cancelButton = new QPushButton("Cancel", m_buttonContainer);
    m_cancelButton->setStyleSheet(
        "QPushButton {"
        "  background-color: rgba(255, 255, 255, 0.15);"
        "  color: white;"
        "  border: 1px solid rgba(255, 255, 255, 0.2);"
        "  border-radius: 6px;"
        "  padding: 9px 18px;"
        "  font-size: 14px;"
        "}"
        "QPushButton:hover {"
        "  background-color: rgba(255, 255, 255, 0.25);"
        "}"
        "QPushButton:pressed {"
        "  background-color: rgba(255, 255, 255, 0.1);"
        "}");
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        // Convert local rect to global coordinates and emit with region
        QRect globalRect = m_selectionRect.normalized();
        globalRect.translate(m_currentScreen->geometry().topLeft());
        emit cancelledWithRegion(globalRect, m_currentScreen);
        close();
    });
    layout->addWidget(m_cancelButton);

    m_buttonContainer->adjustSize();
}

void RecordingRegionSelector::positionButtons()
{
    if (!m_buttonContainer || m_selectionRect.isEmpty()) {
        return;
    }

    // Position below the selection rectangle, centered
    int x = m_selectionRect.center().x() - m_buttonContainer->width() / 2;
    int y = m_selectionRect.bottom() + 20;

    // Keep on screen
    if (x < 10) {
        x = 10;
    }
    if (x + m_buttonContainer->width() > width() - 10) {
        x = width() - m_buttonContainer->width() - 10;
    }

    // If below would be off screen, position above
    if (y + m_buttonContainer->height() > height() - 60) {  // Leave room for instructions
        y = m_selectionRect.top() - m_buttonContainer->height() - 20;
    }

    // If still off screen, position inside selection
    if (y < 10) {
        y = m_selectionRect.top() + 10;
    }

    m_buttonContainer->move(x, y);
}
