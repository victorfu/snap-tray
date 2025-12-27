#ifndef TOOLID_H
#define TOOLID_H

/**
 * @brief Unified tool identifier enum.
 *
 * Single source of truth for all tools across the application.
 * Replaces ToolbarButton, CanvasTool, and AnnotationController::Tool.
 */
enum class ToolId {
    // Selection tool
    Selection = 0,

    // Drawing tools (create AnnotationItems)
    Arrow,
    Polyline,   // Multi-segment line with optional arrowhead
    Pencil,
    Marker,
    Shape,      // Unified Rectangle/Ellipse with ShapeType
    Text,
    Mosaic,
    StepBadge,
    Eraser,

    // Toggle tools (affect canvas behavior)
    LaserPointer,
    CursorHighlight,

    // Actions (one-shot operations)
    Undo,
    Redo,
    Clear,
    Cancel,
    OCR,
    Pin,
    Record,
    Save,
    Copy,
    Exit,

    Count
};

// Note: ShapeType and ShapeFillMode are defined in RegionSelector.h
// They will be moved here in Phase 6 cleanup when we remove the old enums
// For now, include RegionSelector.h to get these types

#endif // TOOLID_H
