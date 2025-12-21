#ifndef WINDOWDETECTIONOVERLAY_H
#define WINDOWDETECTIONOVERLAY_H

#include <QObject>
#include <QRect>
#include <QPoint>
#include <QString>
#include <optional>

#ifdef Q_OS_MACOS
#include "WindowDetector.h"

class QPainter;

/**
 * @brief Handles macOS window detection and overlay drawing.
 */
class WindowDetectionOverlay : public QObject
{
    Q_OBJECT

public:
    explicit WindowDetectionOverlay(QObject* parent = nullptr);

    /**
     * @brief Set the window detector.
     */
    void setWindowDetector(WindowDetector* detector);

    /**
     * @brief Set the screen geometry for coordinate conversion.
     */
    void setScreenGeometry(const QRect& geometry);

    /**
     * @brief Update detection at the given screen position.
     */
    void updateDetection(const QPoint& screenPos);

    /**
     * @brief Clear detection state.
     */
    void clearDetection();

    /**
     * @brief Get the detected window rectangle.
     */
    QRect detectedWindowRect() const { return m_highlightedRect; }

    /**
     * @brief Get the detected window title.
     */
    QString detectedWindowTitle() const;

    /**
     * @brief Check if a window is detected.
     */
    bool hasDetectedWindow() const;

    /**
     * @brief Get the detected element.
     */
    std::optional<DetectedElement> detectedElement() const { return m_detectedWindow; }

    /**
     * @brief Draw window highlight overlay.
     */
    void drawHighlight(QPainter& painter) const;

    /**
     * @brief Draw window title hint.
     */
    void drawWindowHint(QPainter& painter) const;

signals:
    void detectionChanged(const QRect& rect);

private:
    WindowDetector* m_detector;
    std::optional<DetectedElement> m_detectedWindow;
    QRect m_highlightedRect;
    QRect m_screenGeometry;
};

#endif // Q_OS_MACOS

#endif // WINDOWDETECTIONOVERLAY_H
