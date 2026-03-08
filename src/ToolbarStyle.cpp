#include "ToolbarStyle.h"
#include "settings/Settings.h"
#include "ui/DesignSystem.h"

#include <QSettings>

const ToolbarStyleConfig& ToolbarStyleConfig::getDarkStyle()
{
    static ToolbarStyleConfig config = DesignSystem::instance().buildToolbarStyleConfig(true);
    return config;
}

const ToolbarStyleConfig& ToolbarStyleConfig::getLightStyle()
{
    static ToolbarStyleConfig config = DesignSystem::instance().buildToolbarStyleConfig(false);
    return config;
}

const ToolbarStyleConfig& ToolbarStyleConfig::getStyle(ToolbarStyleType type)
{
    return (type == ToolbarStyleType::Light) ? getLightStyle() : getDarkStyle();
}

ToolbarStyleType ToolbarStyleConfig::loadStyle()
{
    auto settings = SnapTray::getSettings();
    int styleValue = settings.value("appearance/toolbarStyle", 0).toInt();
    return static_cast<ToolbarStyleType>(styleValue);
}

void ToolbarStyleConfig::saveStyle(ToolbarStyleType type)
{
    auto settings = SnapTray::getSettings();
    settings.setValue("appearance/toolbarStyle", static_cast<int>(type));
}

QString ToolbarStyleConfig::styleTypeName(ToolbarStyleType type)
{
    switch (type) {
    case ToolbarStyleType::Light:
        return QObject::tr("Light");
    case ToolbarStyleType::Dark:
    default:
        return QObject::tr("Dark");
    }
}
