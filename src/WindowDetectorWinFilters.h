#ifndef WINDOWDETECTORWINFILTERS_H
#define WINDOWDETECTORWINFILTERS_H

#include <string_view>

namespace WindowDetectorWinFilters {

enum class TopLevelWindowKind {
    Window,
    ContextMenu,
    PopupMenu,
    Dialog,
    ToolWindow
};

struct TopLevelWindowTraits {
    std::wstring_view className;
    bool modernUiEnabled = false;
    bool toolWindow = false;
    bool modalFrame = false;
    bool appWindow = false;
    bool hasOwner = false;
};

inline bool isModernUiPopupClass(std::wstring_view className)
{
    return className == L"IME" ||
           className.find(L"MSCTFIME") != std::wstring_view::npos ||
           className == L"tooltips_class32";
}

inline bool isOwnedPopupClass(std::wstring_view className)
{
    return className == L"ComboLBox" ||
           className.find(L"DropDown") != std::wstring_view::npos;
}

inline TopLevelWindowKind classifyTopLevelWindow(const TopLevelWindowTraits& traits)
{
    if (traits.className == L"#32768") {
        return TopLevelWindowKind::ContextMenu;
    }
    if (traits.className == L"#32770") {
        return TopLevelWindowKind::Dialog;
    }
    if (traits.modernUiEnabled && isModernUiPopupClass(traits.className)) {
        return TopLevelWindowKind::PopupMenu;
    }
    if (traits.toolWindow) {
        return TopLevelWindowKind::ToolWindow;
    }
    if (traits.modalFrame) {
        return TopLevelWindowKind::Dialog;
    }
    if (!traits.appWindow && traits.hasOwner) {
        return isOwnedPopupClass(traits.className)
            ? TopLevelWindowKind::PopupMenu
            : TopLevelWindowKind::Dialog;
    }
    return TopLevelWindowKind::Window;
}

} // namespace WindowDetectorWinFilters

#endif // WINDOWDETECTORWINFILTERS_H
