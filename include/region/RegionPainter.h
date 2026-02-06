#ifndef REGIONPAINTER_H
#define REGIONPAINTER_H

#include <QObject>
#include <QRect>
#include <QColor>
#include <QPixmap>
#include <QString>
#include <QFont>
#include <QSize>

class QPainter;
class QWidget;
class SelectionStateManager;
class AnnotationLayer;
class ToolManager;
class ToolbarCore;
class RegionControlWidget;
class TextBoxAnnotation;
class EmojiStickerAnnotation;
class ArrowAnnotation;
class PolylineAnnotation;
class MultiRegionManager;

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
    void setToolbar(ToolbarCore* toolbar);
    void setRegionControlWidget(RegionControlWidget* widget);
    void setParentWidget(QWidget* widget);
    void setMultiRegionManager(MultiRegionManager* manager);

    /**
     * @brief Main paint method called from RegionSelector::paintEvent.
     *
     * Draws overlay, detected window, selection, dimension info,
     * annotations, and current annotation preview.
     *
     * @param painter The painter to draw with
     * @param background The captured background pixmap
     * @param dirtyRect Optional dirty rect for partial updates (empty = full repaint)
     */
    void paint(QPainter& painter, const QPixmap& background, const QRect& dirtyRect = QRect());

    // Configuration setters (call before paint)
    void setHighlightedWindowRect(const QRect& rect);
    void setDetectedWindowTitle(const QString& title);
    void setCornerRadius(int radius);
    void setShowSubToolbar(bool show);
    void setCurrentTool(int tool);
    void setDevicePixelRatio(qreal ratio);
    void setMultiRegionMode(bool enabled) { m_multiRegionMode = enabled; }

    /**
     * @brief Calculate the visual bounding rect of a window highlight (including the hint label).
     */
    QRect getWindowHighlightVisualRect(const QRect& windowRect, const QString& title) const;

    /**
     * @brief Invalidate caches when selection changes.
     * Call this when the selection rect is modified.
     */
    void invalidateOverlayCache();

    void buildDimmedCache(const QPixmap& background);
    const QPixmap& dimmedCache() const { return m_dimmedCache; }

private:
    // Drawing methods (extracted from RegionSelector)
    void drawOverlay(QPainter& painter);
    void drawDimmingOverlay(QPainter& painter, const QRect& clearRect, const QColor& dimColor);
    void drawSelection(QPainter& painter);
    void drawDimensionInfo(QPainter& painter);
    void drawRegionControlWidget(QPainter& painter, const QRect& dimensionInfoRect);
    void drawDetectedWindow(QPainter& painter);
    void drawWindowHint(QPainter& painter, const QString& title);
    void drawAnnotations(QPainter& painter);
    void drawCurrentAnnotation(QPainter& painter);
    void drawMultiSelection(QPainter& painter);
    QRect drawDimensionInfoPanel(QPainter& painter, const QRect& selectionRect, const QString& label) const;
    void drawRegionBadge(QPainter& painter, const QRect& selectionRect, const QColor& color,
                         int index, bool isActive) const;

    // Helper methods
    int effectiveCornerRadius() const;
    TextBoxAnnotation* getSelectedTextAnnotation() const;
    EmojiStickerAnnotation* getSelectedEmojiStickerAnnotation() const;
    ArrowAnnotation* getSelectedArrowAnnotation() const;
    PolylineAnnotation* getSelectedPolylineAnnotation() const;

    // Dependencies (non-owning pointers)
    SelectionStateManager* m_selectionManager = nullptr;
    AnnotationLayer* m_annotationLayer = nullptr;
    ToolManager* m_toolManager = nullptr;
    ToolbarCore* m_toolbar = nullptr;
    RegionControlWidget* m_regionControlWidget = nullptr;
    QWidget* m_parentWidget = nullptr;
    MultiRegionManager* m_multiRegionManager = nullptr;

    // State
    QRect m_highlightedWindowRect;
    QString m_detectedWindowTitle;
    int m_cornerRadius = 0;
    bool m_showSubToolbar = true;
    int m_currentTool = 0;
    qreal m_devicePixelRatio = 1.0;
    bool m_multiRegionMode = false;

    // Performance caches
    // Overlay cache: background + dimming overlay composited together
    QPixmap m_overlayCache;
    QRect m_cachedSelectionRect;
    QRect m_cachedHighlightRect;
    bool m_overlayCacheValid = false;

    QPixmap m_dimmedCache;
    bool m_dimmedCacheReady = false;
};

#endif // REGIONPAINTER_H
