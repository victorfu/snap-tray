#pragma once

#include <array>
#include <climits>

#include "tools/ToolId.h"

// Toolbar grouping and collapse order (#224). One table, no switch.
enum class ToolbarGroup { Draw = 0, Assist = 1, Finish = 2 };

constexpr int kNeverCollapse = INT_MAX;

namespace ToolbarTraitsDetail {
struct Entry {
    ToolbarGroup group;
    int collapsePriority; // lower collapses first
};

// Base priority for drawing tools without an explicit rank. The view model
// subtracts the button's row index, so later buttons collapse first.
constexpr int kDrawBasePriority = 1000;

constexpr std::array<Entry, static_cast<int>(ToolId::Count)> makeTable()
{
    std::array<Entry, static_cast<int>(ToolId::Count)> t{};
    for (auto& e : t) e = {ToolbarGroup::Draw, kDrawBasePriority};
    auto set = [&t](ToolId id, ToolbarGroup g, int p) { t[static_cast<int>(id)] = {g, p}; };
    // Assist group
    set(ToolId::QRCode, ToolbarGroup::Assist, 10);
    set(ToolId::OCR, ToolbarGroup::Assist, 20);
    set(ToolId::MultiRegion, ToolbarGroup::Assist, 30);
    set(ToolId::Measure, ToolbarGroup::Assist, 35);
    set(ToolId::Crop, ToolbarGroup::Assist, 36);
    set(ToolId::Beautify, ToolbarGroup::Assist, 37);
    set(ToolId::CanvasBlackboard, ToolbarGroup::Assist, 38);
    set(ToolId::CanvasWhiteboard, ToolbarGroup::Assist, 39);
    set(ToolId::Clear, ToolbarGroup::Assist, 45);
    set(ToolId::Undo, ToolbarGroup::Assist, kNeverCollapse);
    set(ToolId::Redo, ToolbarGroup::Assist, kNeverCollapse);
    // Draw group, explicit ranks
    set(ToolId::EmojiSticker, ToolbarGroup::Draw, 40);
    set(ToolId::StepBadge, ToolbarGroup::Draw, 50);
    set(ToolId::Eraser, ToolbarGroup::Draw, 60);
    set(ToolId::Mosaic, ToolbarGroup::Draw, 70);
    // Finish group
    set(ToolId::Pin, ToolbarGroup::Finish, kNeverCollapse);
    set(ToolId::Save, ToolbarGroup::Finish, kNeverCollapse);
    set(ToolId::Copy, ToolbarGroup::Finish, kNeverCollapse);
    set(ToolId::Cancel, ToolbarGroup::Finish, kNeverCollapse);
    set(ToolId::Exit, ToolbarGroup::Finish, kNeverCollapse);
    set(ToolId::MultiRegionDone, ToolbarGroup::Finish, kNeverCollapse);
    return t;
}
inline constexpr auto kTable = makeTable();
} // namespace ToolbarTraitsDetail

constexpr ToolbarGroup toolbarGroup(ToolId id)
{
    return ToolbarTraitsDetail::kTable[static_cast<int>(id)].group;
}

constexpr int toolbarCollapsePriority(ToolId id)
{
    return ToolbarTraitsDetail::kTable[static_cast<int>(id)].collapsePriority;
}
