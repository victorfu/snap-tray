#pragma once
#include <QImage>
#include <optional>
#include <vector>

namespace SnapTray::Longshot {
// Non-destructive horizontal edits. History stores source-row ranges, never full
// copies of successive images. The original remains available even after history eviction.
class LongshotImageEdit {
public:
    struct Rows {
        int begin = 0;
        int end = 0; // exclusive, in the original image
        bool operator==(const Rows& other) const { return begin == other.begin && end == other.end; }
    };
    explicit LongshotImageEdit(QImage image = {});
    int height() const;
    bool edited() const;
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    bool keepRows(int begin, int end); // current output coordinates, end exclusive
    bool removeRows(int begin, int end);
    bool undo();
    bool redo();
    bool reset();
    QImage image() const;
    std::optional<int> outputRow(int originalRow) const;
    struct RowMapping { int originalBegin; int outputBegin; int count; };
    std::vector<RowMapping> mapRows(int originalBegin, int originalEnd) const;
private:
    using State = std::vector<Rows>;
    State slice(int begin, int end) const;
    bool apply(State rows);
    static void push(std::vector<State>& stack, State rows);
    QImage m_original;
    State m_rows;
    std::vector<State> m_undo, m_redo;
};
}
