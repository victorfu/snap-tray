#pragma once

#include <QObject>
#include <QTimer>
#include <QVariant>

#include <functional>

#include "annotations/AnnotationLayer.h"

class PinToolOptionsViewModel;
class QKeyEvent;

/**
 * Applies style-strip edits to the selected annotation (#224).
 *
 * Follows AnnotationLayer selection: a selected text, arrow, polyline, or
 * shape becomes the strip's target (PinToolOptionsViewModel::showForSelection);
 * strip edits arrive as selectionStyleEdited and become undoable Modify
 * commands, coalesced per property until kCoalesceIdleMs of inactivity.
 * Addresses the target by annotation id, so index shifts are harmless.
 *
 * Also carries the drawing-order commands for the selected annotation
 * (#268), from the strip's Arrange buttons and from the shortcuts in
 * AnnotationZOrderCommands, so every host handles them the same way.
 */
class AnnotationStyleEditor : public QObject
{
    Q_OBJECT

public:
    static constexpr int kCoalesceIdleMs = 500;

    AnnotationStyleEditor(AnnotationLayer* layer, PinToolOptionsViewModel* viewModel,
                          QObject* parent = nullptr);

    // True while inline text editing owns styling; no selection target then.
    void setInlineEditingPredicate(std::function<bool()> isInlineEditing);
    // Re-evaluates the target from the layer's selection.
    void refreshTarget();
    bool hasTarget() const { return m_targetId != 0; }

    // Screen reader announcements of drawing-order moves go to this object;
    // by default the parent. Hosts whose parent is not a widget set it.
    void setAnnouncementTarget(std::function<QObject*()> target);
    // Moves the selected annotation in the drawing order. Returns true if it moved.
    bool applyZOrder(AnnotationLayer::ZOrderMove move);
    // Handles a drawing-order shortcut while an annotation is selected.
    // Returns true when the key was one; it is then consumed even if the
    // annotation is already at that end, so it does not reach other actions.
    bool handleZOrderKey(const QKeyEvent* event);

signals:
    void styleApplied();    // host repaints and syncs undo/redo state
    void targetShown();     // host makes sure the strip is visible
    void targetCleared();   // host re-runs its normal tool path

private:
    void onStyleEdited(int property, const QVariant& value);
    void syncStripFromTarget();
    void syncZOrderAvailability();

    AnnotationLayer* m_layer;
    PinToolOptionsViewModel* m_viewModel;
    AnnotationLayer::AnnotationId m_targetId = 0;
    QTimer m_idle;
    std::function<bool()> m_isInlineEditing;
    std::function<QObject*()> m_announcementTarget;
};
