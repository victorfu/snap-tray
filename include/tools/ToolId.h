#ifndef TOOLID_H
#define TOOLID_H

/**
 * @brief Unified tool identifier enum.
 *
 * Single source of truth for all tools across the application.
 * Replaces ToolbarButton, and CanvasTool.
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
    Crop,       // Crop sub-region of pin window content
    Measure,    // Pixel distance measurement between two points

    // Toggle tools (affect canvas behavior)
    CanvasWhiteboard,   // ScreenCanvas whiteboard background mode toggle
    CanvasBlackboard,   // ScreenCanvas blackboard background mode toggle

    // Actions (one-shot operations)
    Undo,
    Redo,
    Clear,
    Cancel,
    OCR,
    QRCode,     // QR Code scan/generate
    Pin,
    Record,
    Share,
    Save,
    Copy,
    Exit,

    // Region-specific tools
    MultiRegion,        // Multi-region capture mode
    MultiRegionDone,    // Complete multi-region capture

    Count
};

// Note: ShapeType and ShapeFillMode are defined in RegionSelector.h
// They will be moved here in Phase 6 cleanup when we remove the old enums
// For now, include RegionSelector.h to get these types

#endif // TOOLID_H
