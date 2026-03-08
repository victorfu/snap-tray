#include "ui/DesignTokens.h"
#include "ui/DesignSystem.h"

namespace SnapTray {

const DesignTokens& DesignTokens::forStyle(ToolbarStyleType type)
{
    // Rebuild the requested variant each call so theme previews do not cache
    // colors from whatever mode the DesignSystem singleton happened to be in.
    auto build = [](bool dark) -> DesignTokens {
        const auto& ds = DesignSystem::instance();
        return DesignTokens{
            dark ? ds.green400()  : ds.green500(),   // successAccent
            dark ? ds.red400()    : ds.red500(),     // errorAccent
            dark ? ds.amber400()  : ds.amber500(),   // warningAccent
            dark ? ds.blue400()   : ds.blue500(),    // infoAccent
            dark ? QColor::fromRgbF(0.12, 0.12, 0.12, 0.90)
                 : QColor::fromRgbF(1, 1, 1, 0.92),  // toastBackground
            ds.badgeHighlight(),                     // toastHighlight (reuses badge highlight)
            dark ? QColor::fromRgbF(1, 1, 1, 0.10)
                 : QColor::fromRgbF(0, 0, 0, 0.06),  // toastBorder
            dark ? ds.gray100() : ds.gray900(),      // toastTitleColor
            dark ? ds.gray400() : ds.gray600(),      // toastMessageColor
            dark ? QColor::fromRgbF(0, 0, 0, 0.71)
                 : QColor::fromRgbF(1, 1, 1, 0.78),  // badgeBackground
            dark ? ds.white()
                 : QColor::fromRgbF(0.16, 0.16, 0.16, 1.0), // badgeText
        };
    };

    bool dark = (type == ToolbarStyleType::Dark);
    thread_local DesignTokens s_darkTokens;
    thread_local DesignTokens s_lightTokens;
    s_darkTokens = build(true);
    s_lightTokens = build(false);
    return dark ? s_darkTokens : s_lightTokens;
}

const DesignTokens& DesignTokens::current()
{
    return forStyle(ToolbarStyleConfig::loadStyle());
}

} // namespace SnapTray
