#include "WindowDetectionOverlay.h"

#ifdef Q_OS_MACOS

#include <QPainter>
#include <QFont>
#include <QFontMetrics>

WindowDetectionOverlay::WindowDetectionOverlay(QObject* parent)
    : QObject(parent)
    , m_detector(nullptr)
{
}

void WindowDetectionOverlay::setWindowDetector(WindowDetector* detector)
{
    m_detector = detector;
}

void WindowDetectionOverlay::setScreenGeometry(const QRect& geometry)
{
    m_screenGeometry = geometry;
}

void WindowDetectionOverlay::updateDetection(const QPoint& screenPos)
{
    if (!m_detector) return;

    std::optional<DetectedElement> detected = m_detector->detectWindowAt(screenPos);

    if (detected.has_value()) {
        // Convert global window rect to local coordinates
        QRect globalRect = detected->bounds;
        QRect localRect(
            globalRect.x() - m_screenGeometry.x(),
            globalRect.y() - m_screenGeometry.y(),
            globalRect.width(),
            globalRect.height()
        );

        if (localRect != m_highlightedRect) {
            m_highlightedRect = localRect;
            m_detectedWindow = detected;
            emit detectionChanged(localRect);
        }
    } else {
        if (m_highlightedRect.isValid()) {
            m_highlightedRect = QRect();
            m_detectedWindow.reset();
            emit detectionChanged(QRect());
        }
    }
}

void WindowDetectionOverlay::clearDetection()
{
    m_highlightedRect = QRect();
    m_detectedWindow.reset();
}

QString WindowDetectionOverlay::detectedWindowTitle() const
{
    if (m_detectedWindow.has_value()) {
        return m_detectedWindow->windowTitle;
    }
    return QString();
}

bool WindowDetectionOverlay::hasDetectedWindow() const
{
    return m_detectedWindow.has_value() && m_highlightedRect.isValid();
}

void WindowDetectionOverlay::drawHighlight(QPainter& painter) const
{
    if (!hasDetectedWindow()) return;

    painter.save();

    // Semi-transparent blue fill
    painter.fillRect(m_highlightedRect, QColor(0, 174, 255, 30));

    // Dashed blue border
    QPen pen(QColor(0, 174, 255), 2, Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(m_highlightedRect);

    painter.restore();
}

void WindowDetectionOverlay::drawWindowHint(QPainter& painter) const
{
    if (!hasDetectedWindow()) return;

    QString title = detectedWindowTitle();
    if (title.isEmpty()) return;

    painter.save();

    QFont font = painter.font();
    font.setPointSize(11);
    painter.setFont(font);

    QFontMetrics fm(font);

    // Elide text if too long
    QString displayText = fm.elidedText(title, Qt::ElideRight, 200);
    int textWidth = fm.horizontalAdvance(displayText);

    // Position above the window
    int hintHeight = 24;
    int hintWidth = textWidth + 16;
    int hintX = m_highlightedRect.center().x() - hintWidth / 2;
    int hintY = m_highlightedRect.top() - hintHeight - 4;

    // Keep on screen
    if (hintX < 4) hintX = 4;
    if (hintY < 4) hintY = m_highlightedRect.top() + 4;

    QRect hintRect(hintX, hintY, hintWidth, hintHeight);

    // Draw background pill
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(30, 30, 30, 200));
    painter.drawRoundedRect(hintRect, hintHeight / 2, hintHeight / 2);

    // Draw text
    painter.setPen(Qt::white);
    painter.drawText(hintRect, Qt::AlignCenter, displayText);

    painter.restore();
}

#endif // Q_OS_MACOS
