#ifndef REGIONPAINTER_H
#define REGIONPAINTER_H

#include <QObject>
#include <QRect>
#include <QColor>
#include <QPixmap>
#include <QString>
#include <QFont>
#include <QRectF>
#include <QSize>

class QPainter;
class QWidget;
class SelectionStateManager;
class AnnotationLayer;
class ToolManager;
class TextBoxAnnotation;
class EmojiStickerAnnotation;
class ShapeAnnotation;
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
    struct PaintStats {
        qreal totalMs = 0.0;
        qreal bgMs = 0.0;
        qreal overlayMs = 0.0;
        qreal committedMs = 0.0;
        qreal previewMs = 0.0;
        bool usedBaseLayerCache = false;
        bool usedDirectBackgroundBlit = false;
    };

    explicit RegionPainter(QObject* parent = nullptr);
    ~RegionPainter() override = default;

    // Dependency injection
    void setSelectionManager(SelectionStateManager* manager);
    void setAnnotationLayer(AnnotationLayer* layer);
    void setToolManager(ToolManager* manager);
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
    void setReplacePreview(int targetIndex, const QRect& previewRect);

    /**
     * @brief Calculate the visual bounding rect of a window highlight (including the hint label).
     */
    QRect getWindowHighlightVisualRect(const QRect& windowRect, const QString& title) const;

    QRect lastDimensionInfoRect() const { return m_lastDimensionInfoRect; }
    const PaintStats& lastPaintStats() const { return m_lastPaintStats; }

private:
    // Drawing methods (extracted from RegionSelector)
    void drawOverlay(QPainter& painter);
    void drawDimmingOverlay(QPainter& painter, const QRect& clearRect, const QColor& dimColor);
    void drawSelection(QPainter& painter);
    void drawDimensionInfo(QPainter& painter);
    void drawDetectedWindow(QPainter& painter);
    void drawWindowHint(QPainter& painter, const QString& title);
    void drawAnnotations(QPainter& painter);
    void drawCurrentAnnotation(QPainter& painter);
    void drawMultiSelection(QPainter& painter);
    QRect drawDimensionInfoPanel(QPainter& painter, const QRect& selectionRect, const QString& label) const;
    void drawRegionBadge(QPainter& painter, const QRect& selectionRect, const QColor& color,
                         int index, bool isActive) const;
    QRectF alignedSelectionBorderRect(const QRect& selectionRect, qreal penWidth) const;
    QRect physicalSelectionRect(const QRect& selectionRect) const;
    QString selectionSizeLabel(const QRect& selectionRect) const;
    bool canUseBaseLayerCache() const;
    bool ensureBaseLayerCache(const QPixmap& background);
    void drawBackground(QPainter& painter,
                        const QPixmap& background,
                        const QRect& updateRect,
                        bool* usedDirectBlit = nullptr) const;

    // Helper methods
    int effectiveCornerRadius() const;
    TextBoxAnnotation* getSelectedTextAnnotation() const;
    EmojiStickerAnnotation* getSelectedEmojiStickerAnnotation() const;
    ShapeAnnotation* getSelectedShapeAnnotation() const;
    ArrowAnnotation* getSelectedArrowAnnotation() const;
    PolylineAnnotation* getSelectedPolylineAnnotation() const;

    // Dependencies (non-owning pointers)
    SelectionStateManager* m_selectionManager = nullptr;
    AnnotationLayer* m_annotationLayer = nullptr;
    ToolManager* m_toolManager = nullptr;
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
    int m_replaceTargetIndex = -1;
    QRect m_replacePreviewRect;

    QRect m_lastDimensionInfoRect;
    QRect m_cachedDimensionInfoRect;
    PaintStats m_lastPaintStats;
    QPixmap m_baseLayerCache;
    QSize m_baseLayerLogicalSize;
    QRect m_baseLayerSelectionRect;
    qreal m_baseLayerDevicePixelRatio = 0.0;
    quint64 m_baseLayerBackgroundKey = 0;
    int m_baseLayerCornerRadius = 0;
    bool m_baseLayerCacheValid = false;
};

#endif // REGIONPAINTER_H
