#ifndef TOOLBARTYPES_H
#define TOOLBARTYPES_H

/**
 * @file ToolbarTypes.h
 * @brief Documentation of toolbar implementations in SnapTray.
 *
 * This header provides a reference for the different toolbar types used
 * throughout the application. Each type has specific characteristics and
 * uses shared utilities from ToolbarButtonConfig and ToolbarRenderer.
 *
 * ## Shared Utilities
 *
 * All toolbars share:
 * - Toolbar::ButtonConfig - Unified button configuration with builder pattern
 * - Toolbar::ToolbarRenderer - Shared rendering utilities (layout, hit-testing, colors)
 * - ToolbarStyleConfig - Theme configuration (Dark/Light mode)
 * - GlassRenderer - Glass panel rendering for macOS HIG compliance
 *
 * ## Toolbar Implementations
 *
 * | Type               | Component              | Uses ToolbarWidget | Notes |
 * |--------------------|------------------------|-------------------|-------|
 * | RegionCapture      | RegionSelector         | Yes               | Main capture toolbar |
 * | RecordingRegion    | RecordingRegionSelector| Yes               | Start/Cancel buttons |
 * | ScreenCanvas       | ScreenCanvas           | Yes               | Annotation tools |
 * | ScrollingCapture   | ScrollingCaptureToolbar| No (QWidget)      | Mode-based visibility |
 * | RecordingControl   | RecordingControlBar    | No (out of scope) | Complex QLayout |
 */

namespace Toolbar {

/**
 * @brief Toolbar type identifiers for documentation purposes.
 *
 * These don't affect runtime behavior - they serve as documentation
 * for which toolbar implementation is used in each context.
 */
enum class Type {
    /**
     * RegionSelector toolbar - Main capture toolbar.
     * Buttons: Selection tools, annotation tools, actions (Pin, Save, Copy, etc.)
     * Uses: ToolbarWidget
     */
    RegionCapture,

    /**
     * RecordingRegionSelector toolbar - Simple start/cancel toolbar.
     * Buttons: Start Recording (red), Cancel
     * Uses: ToolbarWidget with custom IconColorProvider
     */
    RecordingRegion,

    /**
     * ScreenCanvas toolbar - Annotation tools for screen canvas.
     * Buttons: Pencil, Marker, Arrow, Shape, LaserPointer, CursorHighlight,
     *          Undo, Redo, Clear, Exit
     * Uses: ToolbarWidget with custom IconColorProvider for color logic
     */
    ScreenCanvas,

    /**
     * RecordingControlBar - Recording control interface.
     * Complex two-mode QWidget with QLayout.
     * Out of scope for shared utilities (needs separate refactoring).
     */
    RecordingControl,

    /**
     * ScrollingCaptureToolbar - Scrolling capture toolbar.
     * Mode-based button visibility (Adjusting/Capturing/Finished).
     * Uses: Toolbar::ButtonConfig, Toolbar::ToolbarRenderer::getIconColor()
     * Keeps own QWidget base due to QLabel integration and mode logic.
     */
    ScrollingCapture
};

} // namespace Toolbar

#endif // TOOLBARTYPES_H
