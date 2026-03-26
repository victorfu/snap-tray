#include "ui/TrayTooltipFormatter.h"

#include <QStringList>

namespace SnapTray {

QString formatWindowsTrayTooltip(const QString& applicationName,
                                 const QString& version,
                                 const QString& installSource,
                                 const QList<TrayTooltipHotkeyLine>& hotkeyLines,
                                 const QString& emptyHotkeyText)
{
    QString title = applicationName;
    if (!version.isEmpty()) {
        if (!title.isEmpty()) {
            title += QLatin1Char(' ');
        }
        title += version;
    }
    if (!installSource.isEmpty()) {
        title += QStringLiteral(" (%1)").arg(installSource);
    }

    QStringList lines;
    lines.reserve(hotkeyLines.size() + 1);
    lines.append(title);

    for (const auto& hotkeyLine : hotkeyLines) {
        const QString displayHotkey = hotkeyLine.hotkey.isEmpty()
            ? emptyHotkeyText
            : hotkeyLine.hotkey;
        lines.append(QStringLiteral("%1: %2").arg(hotkeyLine.label, displayHotkey));
    }

    return lines.join(QLatin1Char('\n'));
}

} // namespace SnapTray
