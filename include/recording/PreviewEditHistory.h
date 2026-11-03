#ifndef PREVIEWEDITHISTORY_H
#define PREVIEWEDITHISTORY_H

#include "recording/CursorEffects.h"
#include "recording/ZoomTrack.h"
#include "recording/RedactionTrack.h"
#include "recording/RecordingAppearance.h"
#include "recording/TimelineEdit.h"
#include "recording/KeyOverlay.h"
#include "recording/OverlayTrack.h"
#include "recording/AudioTracks.h"

#include <QRect>
#include <QVector>
#include <QtGlobal>

namespace SnapTray {

// The crop and trim the recording preview will export. An empty crop means
// the full frame; trimEnd < 0 means "to the end of the video".
struct PreviewEditState {
    QRect crop;
    qint64 trimStart = 0;
    qint64 trimEnd = -1;
    // Pointer effects (#160) are undoable like crop and trim, but they are a
    // look, not an edit of the footage: isUnedited() ignores them.
    CursorEffectSettings cursor;
    // Zoom and pan segments (#161); like the pointer look, undoable but kept
    // by "Reset Crop and Trim".
    ZoomTrack zoom;
    // Masks over sensitive content (#162), undoable; also kept by "Reset
    // Crop and Trim", so a reset never uncovers anything.
    RedactionTrack redaction;
    // The frame around the export (#184): a look, kept by "Reset Crop and Trim".
    RecordingAppearance appearance;
    // Cut-out parts and speed changes inside the trim (#185): footage edits.
    TimelineEdit timeline;
    // Shortcut hints (#187): a look, kept by "Reset Crop and Trim".
    KeyOverlaySettings keys;
    // Text and image overlays (#188), undoable; kept by "Reset Crop and Trim".
    OverlayTrack overlays;
    // Separate tracks mixed after recording (#189); kept by "Reset Crop and Trim".
    AudioMixSettings audio;

    bool operator==(const PreviewEditState& other) const
    {
        return crop == other.crop && trimStart == other.trimStart && trimEnd == other.trimEnd
            && cursor == other.cursor && zoom == other.zoom && redaction == other.redaction
            && appearance == other.appearance && timeline == other.timeline && keys == other.keys
            && overlays == other.overlays && audio == other.audio;
    }
    bool operator!=(const PreviewEditState& other) const { return !(*this == other); }
    bool isUnedited() const { return crop == QRect() && trimStart == 0 && trimEnd == -1 && timeline.isEmpty(); }
};

// Session-only undo/redo for preview edits. Each record() is one undo step;
// recording after an undo drops the redo branch. Only applied edits are
// recorded: a crop draft that is cancelled never reaches the history.
class PreviewEditHistory
{
public:
    static constexpr int kMaxSteps = 100;

    bool record(const PreviewEditState& state)
    {
        if (state == current()) {
            return false;
        }
        m_states.resize(m_index + 1);
        m_states.append(state);
        if (m_states.size() > kMaxSteps + 1) {
            m_states.removeFirst();
        }
        m_index = m_states.size() - 1;
        return true;
    }

    bool canUndo() const { return m_index > 0; }
    bool canRedo() const { return m_index + 1 < m_states.size(); }

    PreviewEditState undo()
    {
        if (canUndo()) --m_index;
        return current();
    }

    PreviewEditState redo()
    {
        if (canRedo()) ++m_index;
        return current();
    }

    PreviewEditState current() const { return m_states.at(m_index); }

    // Starts over from `initial` (the state the preview opened with).
    void reset(const PreviewEditState& initial)
    {
        m_states = {initial};
        m_index = 0;
    }

private:
    QVector<PreviewEditState> m_states{PreviewEditState{}};
    int m_index = 0;
};

} // namespace SnapTray

#endif // PREVIEWEDITHISTORY_H
