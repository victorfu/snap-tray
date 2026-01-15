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
    Eraser,
    StepBadge,
    EmojiSticker,

    // Toggle tools (affect canvas behavior)
    LaserPointer,
    CursorHighlight,
    Spotlight,

    // Actions (one-shot operations)
    Undo,
    Redo,
    Clear,
    Cancel,
    OCR,
    AutoBlur,   // Auto-detect and blur faces/text
    Pin,
    Record,
    Save,
    Copy,
    Exit,

    // Region-specific tools
    MultiRegion,        // Multi-region capture mode
    MultiRegionDone,    // Complete multi-region capture
    ScrollCapture,      // Scrolling capture feature

    Count
};

// Note: ShapeType and ShapeFillMode are defined in RegionSelector.h
// They will be moved here in Phase 6 cleanup when we remove the old enums
// For now, include RegionSelector.h to get these types

#endif // TOOLID_H
