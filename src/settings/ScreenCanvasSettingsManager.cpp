#include "settings/ScreenCanvasSettingsManager.h"
#include "settings/Settings.h"

#include <QScreen>
#include <QStringList>

ScreenCanvasSettingsManager& ScreenCanvasSettingsManager::instance()
{
    static ScreenCanvasSettingsManager instance;
    return instance;
}

QString ScreenCanvasSettingsManager::loadToolbarScreenId() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyToolbarScreenId).toString().trimmed();
}

QPoint ScreenCanvasSettingsManager::loadToolbarPositionInScreen() const
{
    auto settings = SnapTray::getSettings();
    return settings.value(kSettingsKeyToolbarPositionInScreen, QPoint()).toPoint();
}

ScreenCanvasToolbarPlacement ScreenCanvasSettingsManager::loadToolbarPlacement() const
{
    return ScreenCanvasToolbarPlacement{
        loadToolbarScreenId(),
        loadToolbarPositionInScreen()
    };
}

void ScreenCanvasSettingsManager::saveToolbarPlacement(const QString& screenId,
                                                       const QPoint& topLeftInScreen)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyToolbarScreenId, screenId.trimmed());
    settings.setValue(kSettingsKeyToolbarPositionInScreen, topLeftInScreen);
}

void ScreenCanvasSettingsManager::clearToolbarPlacement()
{
    auto settings = SnapTray::getSettings();
    settings.remove(kSettingsKeyToolbarScreenId);
    settings.remove(kSettingsKeyToolbarPositionInScreen);
}

QString ScreenCanvasSettingsManager::screenIdentifier(const QScreen* screen)
{
    if (!screen) {
        return {};
    }

    const QString manufacturer = screen->manufacturer().trimmed();
    const QString model = screen->model().trimmed();
    const QString name = screen->name().trimmed();
    const QString serialNumber = screen->serialNumber().trimmed();
    if (!serialNumber.isEmpty()) {
        QStringList parts;
        if (!manufacturer.isEmpty()) {
            parts << manufacturer;
        }
        if (!model.isEmpty()) {
            parts << model;
        }
        if (!name.isEmpty()) {
            parts << name;
        }
        parts << QStringLiteral("sn:%1").arg(serialNumber);
        return parts.join(QLatin1Char('|'));
    }

    QStringList parts;
    if (!manufacturer.isEmpty()) {
        parts << manufacturer;
    }
    if (!model.isEmpty()) {
        parts << model;
    }
    if (!name.isEmpty()) {
        parts << name;
    }

    const QRect geometry = screen->geometry();
    parts << QStringLiteral("geom:%1,%2,%3,%4")
                 .arg(geometry.x())
                 .arg(geometry.y())
                 .arg(geometry.width())
                 .arg(geometry.height());
    return parts.join(QLatin1Char('|'));
}
