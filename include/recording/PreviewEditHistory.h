#ifndef PREVIEWEDITHISTORY_H
#define PREVIEWEDITHISTORY_H

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

    bool operator==(const PreviewEditState& other) const
    {
        return crop == other.crop && trimStart == other.trimStart && trimEnd == other.trimEnd;
    }
    bool operator!=(const PreviewEditState& other) const { return !(*this == other); }
    bool isUnedited() const { return *this == PreviewEditState{}; }
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

private:
    QVector<PreviewEditState> m_states{PreviewEditState{}};
    int m_index = 0;
};

} // namespace SnapTray

#endif // PREVIEWEDITHISTORY_H
