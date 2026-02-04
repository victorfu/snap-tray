#ifndef REGIONLAYOUTMANAGER_H
#define REGIONLAYOUTMANAGER_H

#include <QObject>
#include <QRect>
#include <QPoint>
#include <QSize>
#include <QImage>
#include <QColor>
#include <QVector>
#include <QPixmap>

#include "ResizeHandler.h"

class QPainter;
class AnnotationLayer;
class AnnotationItem;

/**
 * @brief Constants for Region Layout Mode UI and behavior.
 */
namespace LayoutModeConstants {
    constexpr int kMinRegionSize = 50;           // Minimum region dimension in pixels
    constexpr int kHandleSize = 8;               // Resize handle size in pixels
    constexpr int kHandleHitMargin = 4;          // Extra hit area for handles
    constexpr int kBorderWidth = 2;              // Region border thickness
    constexpr int kSelectedBorderWidth = 3;      // Selected region border thickness
    constexpr int kBadgeSize = 24;               // Index badge diameter
    constexpr int kBadgeFontSize = 12;           // Badge font size
    constexpr int kButtonWidth = 80;             // Confirm/Cancel button width
    constexpr int kButtonHeight = 32;            // Button height
    constexpr int kButtonSpacing = 12;           // Space between buttons
    constexpr int kButtonMargin = 16;            // Margin from canvas edge
    constexpr int kMaxCanvasSize = 10000;        // Maximum canvas dimension
    constexpr int kMaxRegionCount = 20;          // Maximum supported regions
}

/**
 * @brief Data structure representing a single region in layout mode.
 *
 * Extends the basic Region concept with image data and layout state.
 */
struct LayoutRegion {
    QRect rect;              ///< Current bounds in logical coordinates
    QRect originalRect;      ///< Bounds at layout mode entry (for cancel/restore)
    QImage image;            ///< Extracted region image at physical resolution
    QColor color;            ///< Border color (from capture, cycles through palette)
    int index = 0;           ///< 1-based display index
    bool isSelected = false; ///< Whether this region is currently selected
};

/**
 * @brief Internal state for layout mode operations.
 */
struct RegionLayoutState {
    QVector<LayoutRegion> regions;           ///< Current regions
    QVector<LayoutRegion> originalSnapshot;  ///< Snapshot for cancel/restore
    int selectedIndex = -1;                  ///< Currently selected region (-1 = none)
    int hoveredIndex = -1;                   ///< Currently hovered region (-1 = none)
    QSize canvasSize;                        ///< Current canvas size
    QSize originalCanvasSize;                ///< Original canvas size at entry

    // Drag state
    bool isDragging = false;
    QPoint dragStartPos;
    QRect dragStartRect;

    // Resize state
    bool isResizing = false;
    ResizeHandler::Edge resizeEdge = ResizeHandler::Edge::None;
    QPoint resizeStartPos;
    QRect resizeStartRect;
};

/**
 * @brief Binding between an annotation and its containing region.
 */
struct AnnotationRegionBinding {
    AnnotationItem* annotation = nullptr;  ///< Pointer to the annotation
    int regionIndex = -1;                  ///< Index of containing region (-1 = unbound)
    QPointF offsetFromRegion;              ///< Offset from region's top-left corner
};

/**
 * @brief Manages the Region Layout Mode for repositioning regions in multi-region captures.
 *
 * This class handles:
 * - Layout mode lifecycle (enter/exit)
 * - Region selection, dragging, and resizing
 * - Canvas bounds auto-adjustment
 * - Image recomposition on confirm
 * - Annotation binding and transformation
 */
class RegionLayoutManager : public QObject
{
    Q_OBJECT

public:
    explicit RegionLayoutManager(QObject* parent = nullptr);
    ~RegionLayoutManager() override = default;

    // ========================================================================
    // Mode Control
    // ========================================================================

    /**
     * @brief Enter layout mode with the given regions and canvas size.
     * @param regions The regions to edit
     * @param canvasSize The initial canvas size
     */
    void enterLayoutMode(const QVector<LayoutRegion>& regions, const QSize& canvasSize);

    /**
     * @brief Exit layout mode.
     * @param applyChanges If true, changes are applied; if false, reverted to original
     */
    void exitLayoutMode(bool applyChanges);

    /**
     * @brief Check if layout mode is currently active.
     */
    bool isActive() const;

    // ========================================================================
    // Region Data Access
    // ========================================================================

    /**
     * @brief Get the current regions (with any modifications).
     */
    QVector<LayoutRegion> regions() const;

    /**
     * @brief Get the original regions (before any modifications).
     */
    QVector<LayoutRegion> originalRegions() const;

    /**
     * @brief Get the current canvas bounding box.
     */
    QRect canvasBounds() const;

    // ========================================================================
    // Interaction / Hit Testing
    // ========================================================================

    /**
     * @brief Find which region contains the given point.
     * @param pos Position in canvas coordinates
     * @return Region index (0-based), or -1 if no region hit
     */
    int hitTestRegion(const QPoint& pos) const;

    /**
     * @brief Find which resize handle is at the given point (for selected region).
     * @param pos Position in canvas coordinates
     * @return The edge/corner handle, or Edge::None if no handle hit
     */
    ResizeHandler::Edge hitTestHandle(const QPoint& pos) const;

    /**
     * @brief Select a region by index.
     * @param index Region index (0-based), or -1 to deselect
     */
    void selectRegion(int index);

    /**
     * @brief Get the currently selected region index.
     * @return Selected index (0-based), or -1 if none selected
     */
    int selectedIndex() const;

    /**
     * @brief Set the hovered region index (for visual feedback).
     * @param index Region index (0-based), or -1 for no hover
     */
    void setHoveredIndex(int index);

    /**
     * @brief Get the currently hovered region index.
     */
    int hoveredIndex() const;

    // ========================================================================
    // Drag Operations
    // ========================================================================

    /**
     * @brief Start dragging the selected region.
     * @param pos Starting position in canvas coordinates
     */
    void startDrag(const QPoint& pos);

    /**
     * @brief Update the drag position.
     * @param pos Current position in canvas coordinates
     */
    void updateDrag(const QPoint& pos);

    /**
     * @brief Finish the drag operation.
     */
    void finishDrag();

    /**
     * @brief Check if currently dragging.
     */
    bool isDragging() const;

    // ========================================================================
    // Resize Operations
    // ========================================================================

    /**
     * @brief Start resizing the selected region.
     * @param edge Which edge/corner to resize from
     * @param pos Starting position in canvas coordinates
     */
    void startResize(ResizeHandler::Edge edge, const QPoint& pos);

    /**
     * @brief Update the resize position.
     * @param pos Current position in canvas coordinates
     * @param maintainAspectRatio If true, maintain aspect ratio (Shift key)
     */
    void updateResize(const QPoint& pos, bool maintainAspectRatio);

    /**
     * @brief Finish the resize operation.
     */
    void finishResize();

    /**
     * @brief Check if currently resizing.
     */
    bool isResizing() const;

    // ========================================================================
    // Rendering
    // ========================================================================

    /**
     * @brief Render the layout mode visualization.
     * @param painter The painter to draw with
     * @param dpr Device pixel ratio for DPI-aware rendering
     */
    void render(QPainter& painter, qreal dpr) const;

    // ========================================================================
    // Image Recomposition
    // ========================================================================

    /**
     * @brief Recompose the regions into a single image.
     * @param dpr Device pixel ratio
     * @return The composited pixmap
     */
    QPixmap recomposeImage(qreal dpr) const;

    // ========================================================================
    // Annotation Integration
    // ========================================================================

    /**
     * @brief Bind annotations to their containing regions.
     * @param layer The annotation layer containing annotations
     */
    void bindAnnotations(AnnotationLayer* layer);

    /**
     * @brief Update annotation positions based on region movements.
     */
    void updateAnnotationPositions();

    /**
     * @brief Restore annotations to their original positions (for cancel).
     * @param layer The annotation layer to restore
     */
    void restoreAnnotations(AnnotationLayer* layer);

    // ========================================================================
    // Serialization
    // ========================================================================

    /// Magic number for region metadata files: "SNPR"
    static constexpr quint32 kSerializationMagic = 0x534E5052;
    /// Serialization format version
    static constexpr quint16 kSerializationVersion = 1;

    /**
     * @brief Serialize regions to a byte array.
     * @param regions The regions to serialize
     * @return Serialized data
     */
    static QByteArray serializeRegions(const QVector<LayoutRegion>& regions);

    /**
     * @brief Deserialize regions from a byte array.
     * @param data The serialized data
     * @return Deserialized regions, or empty vector on error
     */
    static QVector<LayoutRegion> deserializeRegions(const QByteArray& data);

signals:
    /**
     * @brief Emitted when the layout has changed (region moved/resized).
     */
    void layoutChanged();

    /**
     * @brief Emitted when the selection has changed.
     * @param index The newly selected region index (-1 if deselected)
     */
    void selectionChanged(int index);

    /**
     * @brief Emitted when the canvas size has changed.
     * @param newSize The new canvas size
     */
    void canvasSizeChanged(const QSize& newSize);

    /**
     * @brief Emitted when user requests to confirm (Enter key or button).
     */
    void confirmRequested();

    /**
     * @brief Emitted when user requests to cancel (Escape key or button).
     */
    void cancelRequested();

private:
    /**
     * @brief Recalculate canvas bounds based on current region positions.
     */
    void recalculateBounds();

    /**
     * @brief Get handle rect for a given edge of the selected region.
     */
    QRect handleRectForEdge(ResizeHandler::Edge edge) const;

    bool m_active = false;
    RegionLayoutState m_state;
    QVector<AnnotationRegionBinding> m_annotationBindings;
    QVector<QRectF> m_annotationOriginalRects;  ///< Original annotation rects for restore
};

#endif // REGIONLAYOUTMANAGER_H
