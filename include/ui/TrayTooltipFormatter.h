#ifndef TRAYTOOLTIPFORMATTER_H
#define TRAYTOOLTIPFORMATTER_H

#include <QList>
#include <QString>

namespace SnapTray {

struct TrayTooltipHotkeyLine {
    QString label;
    QString hotkey;
};

QString formatWindowsTrayTooltip(const QString& applicationName,
                                 const QString& version,
                                 const QString& installSource,
                                 const QList<TrayTooltipHotkeyLine>& hotkeyLines,
                                 const QString& emptyHotkeyText,
                                 const QString& statusLine = QString());

} // namespace SnapTray

#endif // TRAYTOOLTIPFORMATTER_H
