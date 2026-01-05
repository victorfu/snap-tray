#ifndef SELECTIONRESIZEHELPER_H
#define SELECTIONRESIZEHELPER_H

#include <QPoint>
#include <QRect>
#include <Qt>

#include "SelectionStateManager.h"

/**
 * @brief Provides static helper functions for selection resize operations.
 *
 * Handles cursor mapping for resize handles and edge adjustment
 * calculations. These are pure utility functions with no state.
 */
class SelectionResizeHelper
{
public:
    using ResizeHandle = SelectionStateManager::ResizeHandle;

    /**
     * @brief Get the appropriate cursor shape for a resize handle.
     */
    static Qt::CursorShape cursorForHandle(ResizeHandle handle);

    /**
     * @brief Determine which handle/edge to use when clicking outside selection.
     *
     * When user clicks outside the selection rectangle, this determines
     * which edge(s) should be adjusted to include the click point.
     *
     * @param clickPos The click position
     * @param selectionRect The current selection rectangle
     * @return The handle representing which edge(s) to adjust
     */
    static ResizeHandle determineHandleFromOutsideClick(
        const QPoint& clickPos, const QRect& selectionRect);

    /**
     * @brief Adjust selection edges to include the given position.
     *
     * @param pos The position to include in selection
     * @param handle Which edge(s) to adjust
     * @param selectionRect The current selection
     * @return The adjusted selection rectangle (normalized)
     */
    static QRect adjustEdgesToPosition(
        const QPoint& pos, ResizeHandle handle, const QRect& selectionRect);

    /**
     * @brief Check if a rectangle meets minimum size requirements.
     *
     * @param rect The rectangle to check
     * @param minWidth Minimum width (default 10)
     * @param minHeight Minimum height (default 10)
     * @return true if rect meets minimum size
     */
    static bool meetsMinimumSize(const QRect& rect, int minWidth = 10, int minHeight = 10);

private:
    // Static utility class - no instances
    SelectionResizeHelper() = delete;
};

#endif // SELECTIONRESIZEHELPER_H
