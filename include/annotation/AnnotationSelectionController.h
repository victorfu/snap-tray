#pragma once

#include <QElapsedTimer>
#include <QPoint>
#include <QRect>

#include <functional>
#include <vector>

#include "annotations/AnnotationLayer.h"

class QKeyEvent;
class QPainter;

/**
 * Multi-object selection input shared by Region Selector and PinWindow (#370).
 *
 * Hosts forward pointer events in annotation coordinates before their
 * single-object editors and keys before their own shortcuts; a true return
 * means the event was consumed.
 *
 * - Shift+click on an annotation adds it to, or removes it from, the selection.
 * - Shift+drag from empty space draws a marquee when the host allows it
 *   (Region Selector's Selection tool); what it touches joins the selection.
 * - With several selected, dragging any of them moves them all as one undo
 *   step; a click elsewhere clears the group and falls through.
 * - Ctrl/Cmd+A selects every selectable annotation; for a group,
 *   Delete/Backspace removes it in one step and arrows nudge it (Shift:
 *   larger step); Esc cancels a drag or marquee, then clears a group.
 *
 * The group drag does not snap; snapping (#282) stays single-object.
 */
class AnnotationSelectionController
{
public:
    static constexpr int kNudgeStep = 1;
    static constexpr int kNudgeLargeStep = 10;
    // A Shift+click that moves less than this is a click, not a marquee.
    static constexpr int kMarqueeMinimumDrag = 3;
    // Nudges this close together extend one undo step.
    static constexpr int kNudgeCoalesceMs = 500;

    explicit AnnotationSelectionController(AnnotationLayer* layer);

    bool handlePress(const QPoint& pos, Qt::KeyboardModifiers modifiers, bool marqueeAllowed);
    bool handleMove(const QPoint& pos, Qt::MouseButtons buttons);
    bool handleRelease(const QPoint& pos);
    // Cancels a group drag or marquee in progress. True if there was one.
    bool cancel();
    bool isDragging() const { return m_mode == Mode::Dragging; }
    bool isMarqueeActive() const { return m_mode == Mode::Marquee; }
    bool isActive() const { return m_mode != Mode::Idle; }

    // stepToAnnotation maps an on-screen arrow step (for example (1, 0) times
    // the step size) to annotation coordinates; hosts with rotation pass one.
    bool handleKey(const QKeyEvent* event,
                   const std::function<QPointF(const QPoint&)>& stepToAnnotation = {});

    // Draws group outlines and the marquee, in annotation coordinates.
    void draw(QPainter& painter) const;
    // Area draw() covers, for repaints.
    QRect visualRect() const;
    QRect marqueeRect() const;

private:
    enum class Mode { Idle, Pending, Dragging, Marquee };

    void updatePointerPosition(const QPoint& pos);

    AnnotationLayer* m_layer;
    Mode m_mode = Mode::Idle;
    QPoint m_pressPos;
    QPoint m_currentPos;
    std::vector<AnnotationLayer::AnnotationId> m_marqueeBase;
    QElapsedTimer m_lastNudge;
};
