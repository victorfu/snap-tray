#include "longshot/LongshotImageEdit.h"
#include <QColorSpace>
#include <algorithm>
#include <cstring>

namespace SnapTray::Longshot {
namespace { constexpr size_t kUndoLimit = 32; }
LongshotImageEdit::LongshotImageEdit(QImage image) : m_original(std::move(image))
{
    if (!m_original.isNull()) m_rows.push_back({0,m_original.height()});
}
int LongshotImageEdit::height() const
{
    int value = 0;
    for (const auto& range : m_rows) value += range.end - range.begin;
    return value;
}
bool LongshotImageEdit::edited() const
{
    return !m_original.isNull() && m_rows != State{{0,m_original.height()}};
}
LongshotImageEdit::State LongshotImageEdit::slice(int begin, int end) const
{
    State result;
    int offset = 0;
    for (const auto& range : m_rows) {
        const int count = range.end - range.begin;
        const int a = std::max(begin,offset), b = std::min(end,offset+count);
        if (a < b) result.push_back({range.begin+a-offset,range.begin+b-offset});
        offset += count;
    }
    return result;
}
void LongshotImageEdit::push(std::vector<State>& stack, State rows)
{
    if (stack.size() == kUndoLimit) stack.erase(stack.begin());
    stack.push_back(std::move(rows));
}
bool LongshotImageEdit::apply(State rows)
{
    if (rows.empty() || rows == m_rows) return false;
    push(m_undo,m_rows); m_redo.clear(); m_rows = std::move(rows); return true;
}
bool LongshotImageEdit::keepRows(int begin, int end)
{
    if (begin < 0 || end > height() || begin >= end) return false;
    return apply(slice(begin,end));
}
bool LongshotImageEdit::removeRows(int begin, int end)
{
    if (begin < 0 || end > height() || begin >= end || end-begin == height()) return false;
    State result = slice(0,begin), tail = slice(end,height());
    result.insert(result.end(),tail.begin(),tail.end());
    return apply(std::move(result));
}
bool LongshotImageEdit::undo()
{
    if (!canUndo()) return false;
    push(m_redo,m_rows); m_rows = std::move(m_undo.back()); m_undo.pop_back(); return true;
}
bool LongshotImageEdit::redo()
{
    if (!canRedo()) return false;
    push(m_undo,m_rows); m_rows = std::move(m_redo.back()); m_redo.pop_back(); return true;
}
bool LongshotImageEdit::reset()
{
    return !m_original.isNull() && apply({{0,m_original.height()}});
}
QImage LongshotImageEdit::image() const
{
    if (!edited()) return m_original;
    QImage result(m_original.width(),height(),m_original.format());
    if (result.isNull()) return {};
    result.setColorTable(m_original.colorTable());
    result.setColorSpace(m_original.colorSpace());
    result.setDevicePixelRatio(m_original.devicePixelRatio());
    int row = 0;
    for (const auto& range : m_rows) {
        const int count = range.end-range.begin;
        for (int i = 0; i < count; ++i)
            std::memcpy(result.scanLine(row+i),m_original.constScanLine(range.begin+i),size_t(result.bytesPerLine()));
        row += count;
    }
    return result;
}
std::optional<int> LongshotImageEdit::outputRow(int originalRow) const
{
    int offset = 0;
    for (const auto& range : m_rows) {
        if (originalRow >= range.begin && originalRow < range.end) return offset+originalRow-range.begin;
        offset += range.end-range.begin;
    }
    return {};
}
std::vector<LongshotImageEdit::RowMapping> LongshotImageEdit::mapRows(int begin, int end) const
{
    std::vector<RowMapping> mapped;
    int offset = 0;
    for (const auto& range : m_rows) {
        const int a = std::max(begin,range.begin), b = std::min(end,range.end);
        if (a < b) mapped.push_back({a,offset+a-range.begin,b-a});
        offset += range.end-range.begin;
    }
    return mapped;
}

}
