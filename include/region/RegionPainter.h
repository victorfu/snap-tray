#ifndef REGIONPAINTER_H
#define REGIONPAINTER_H

#include <QObject>
#include <QRect>
#include <QColor>
#include <QPixmap>
#include <QString>

class QPainter;
class QWidget;
class SelectionStateManager;
class AnnotationLayer;
class ToolManager;
class ToolbarWidget;
class RadiusSliderWidget;
class TextAnnotation;

/**
 * @brief Handles all painting operations for RegionSelector.
 *
 * Extracted from RegionSelector to reduce file size and improve maintainability.
 * This class is responsible for drawing overlays, selection rectangles,
 * dimension info, detected windows, and annotations.
 */
class RegionPainter : public QObject
{
    Q_OBJECT

public:
    explicit RegionPainter(QObject* parent = nullptr);
    ~RegionPainter() override = default;

    // Dependency injection
    void setSelectionManager(SelectionStateManager* manager);
    void setAnnotationLayer(AnnotationLayer* layer);
    void setToolManager(ToolManager* manager);
    void setToolbar(ToolbarWidget* toolbar);
    void setRadiusSliderWidget(RadiusSliderWidget* widget);
    void setParentWidget(QWidget* widget);

    /**
     * @brief Main paint method called from RegionSelector::paintEvent.
     *
     * Draws overlay, detected window, selection, dimension info,
     * annotations, and current annotation preview.
     *
     * @param painter The painter to draw with
     * @param background The captured background pixmap
     */
    void paint(QPainter& painter, const QPixmap& background);

    // Configuration setters (call before paint)
    void setHighlightedWindowRect(const QRect& rect);
    void setDetectedWindowTitle(const QString& title);
    void setCornerRadius(int radius);
    void setShowSubToolbar(bool show);
    void setCurrentTool(int tool);
    void setDevicePixelRatio(qreal ratio);

private:
    // Drawing methods (extracted from RegionSelector)
    void drawOverlay(QPainter& painter);
    void drawDimmingOverlay(QPainter& painter, const QRect& clearRect, const QColor& dimColor);
    void drawSelection(QPainter& painter);
    void drawDimensionInfo(QPainter& painter);
    void drawRadiusSlider(QPainter& painter, const QRect& dimensionInfoRect);
    void drawDetectedWindow(QPainter& painter);
    void drawWindowHint(QPainter& painter, const QString& title);
    void drawAnnotations(QPainter& painter);
    void drawCurrentAnnotation(QPainter& painter);

    // Helper methods
    int effectiveCornerRadius() const;
    TextAnnotation* getSelectedTextAnnotation() const;

    // Dependencies (non-owning pointers)
    SelectionStateManager* m_selectionManager = nullptr;
    AnnotationLayer* m_annotationLayer = nullptr;
    ToolManager* m_toolManager = nullptr;
    ToolbarWidget* m_toolbar = nullptr;
    RadiusSliderWidget* m_radiusSliderWidget = nullptr;
    QWidget* m_parentWidget = nullptr;

    // State
    QRect m_highlightedWindowRect;
    QString m_detectedWindowTitle;
    int m_cornerRadius = 0;
    bool m_showSubToolbar = true;
    int m_currentTool = 0;
    qreal m_devicePixelRatio = 1.0;
};

#endif // REGIONPAINTER_H
