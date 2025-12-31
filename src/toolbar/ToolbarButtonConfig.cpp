#include "toolbar/ToolbarButtonConfig.h"

namespace Toolbar {

ButtonConfig::ButtonConfig(int id, const QString& iconKey, const QString& tooltip,
                           bool separatorBefore)
    : id(id)
    , iconKey(iconKey)
    , tooltip(tooltip)
    , separatorBefore(separatorBefore)
{
}

ButtonConfig& ButtonConfig::action()
{
    isAction = true;
    return *this;
}

ButtonConfig& ButtonConfig::cancel()
{
    isCancel = true;
    return *this;
}

ButtonConfig& ButtonConfig::record()
{
    isRecord = true;
    return *this;
}

ButtonConfig& ButtonConfig::toggle()
{
    isToggle = true;
    return *this;
}

ButtonConfig& ButtonConfig::separator()
{
    separatorBefore = true;
    return *this;
}

} // namespace Toolbar
