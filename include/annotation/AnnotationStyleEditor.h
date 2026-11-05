#pragma once

#include <QObject>
#include <QTimer>
#include <QVariant>

#include <functional>

#include "annotations/AnnotationLayer.h"

class PinToolOptionsViewModel;

/**
 * Applies style-strip edits to the selected annotation (#224).
 *
 * Follows AnnotationLayer selection: a selected text, arrow, polyline, or
 * shape becomes the strip's target (PinToolOptionsViewModel::showForSelection);
 * strip edits arrive as selectionStyleEdited and become undoable Modify
 * commands, coalesced per property until kCoalesceIdleMs of inactivity.
 * Addresses the target by annotation id, so index shifts are harmless.
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

signals:
    void styleApplied();    // host repaints and syncs undo/redo state
    void targetShown();     // host makes sure the strip is visible
    void targetCleared();   // host re-runs its normal tool path

private:
    void onStyleEdited(int property, const QVariant& value);
    void syncStripFromTarget();

    AnnotationLayer* m_layer;
    PinToolOptionsViewModel* m_viewModel;
    AnnotationLayer::AnnotationId m_targetId = 0;
    QTimer m_idle;
    std::function<bool()> m_isInlineEditing;
};
