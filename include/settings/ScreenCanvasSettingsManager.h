#ifndef SCREENCANVASSETTINGSMANAGER_H
#define SCREENCANVASSETTINGSMANAGER_H

#include <QPoint>
#include <QString>

class QScreen;

struct ScreenCanvasToolbarPlacement
{
    QString screenId;
    QPoint topLeftInScreen;

    bool isValid() const
    {
        return !screenId.trimmed().isEmpty();
    }
};

class ScreenCanvasSettingsManager
{
public:
    static ScreenCanvasSettingsManager& instance();

    QString loadToolbarScreenId() const;
    QPoint loadToolbarPositionInScreen() const;
    ScreenCanvasToolbarPlacement loadToolbarPlacement() const;

    void saveToolbarPlacement(const QString& screenId, const QPoint& topLeftInScreen);
    void clearToolbarPlacement();

    static QString screenIdentifier(const QScreen* screen);

private:
    ScreenCanvasSettingsManager() = default;
    ScreenCanvasSettingsManager(const ScreenCanvasSettingsManager&) = delete;
    ScreenCanvasSettingsManager& operator=(const ScreenCanvasSettingsManager&) = delete;

    static constexpr const char* kSettingsKeyToolbarScreenId = "screenCanvas/toolbarScreenId";
    static constexpr const char* kSettingsKeyToolbarPositionInScreen = "screenCanvas/toolbarTopLeftInScreen";
};

#endif // SCREENCANVASSETTINGSMANAGER_H
