#include <QtTest>

#include "ui/TrayTooltipFormatter.h"

class tst_TrayTooltipFormatter : public QObject
{
    Q_OBJECT

private slots:
    void testFormatWindowsTrayTooltip_AllValuesPresent();
    void testFormatWindowsTrayTooltip_EmptyHotkeyUsesNotSet();
};

void tst_TrayTooltipFormatter::testFormatWindowsTrayTooltip_AllValuesPresent()
{
    const QList<SnapTray::TrayTooltipHotkeyLine> hotkeyLines{
        {QStringLiteral("Region Capture hotkey"), QStringLiteral("F2")},
        {QStringLiteral("Paste hotkey"), QStringLiteral("F3")},
        {QStringLiteral("Screen Canvas hotkey"), QStringLiteral("Ctrl+F2")},
    };

    const QString tooltip = SnapTray::formatWindowsTrayTooltip(
        QStringLiteral("SnapTray"),
        QStringLiteral("2.11.3"),
        QStringLiteral("Microsoft Store"),
        hotkeyLines,
        QStringLiteral("Not set"));

    QCOMPARE(tooltip,
             QStringLiteral("SnapTray 2.11.3 (Microsoft Store)\n"
                            "Region Capture hotkey: F2\n"
                            "Paste hotkey: F3\n"
                            "Screen Canvas hotkey: Ctrl+F2"));
}

void tst_TrayTooltipFormatter::testFormatWindowsTrayTooltip_EmptyHotkeyUsesNotSet()
{
    const QList<SnapTray::TrayTooltipHotkeyLine> hotkeyLines{
        {QStringLiteral("Region Capture hotkey"), QString()},
        {QStringLiteral("Paste hotkey"), QStringLiteral("F3")},
        {QStringLiteral("Screen Canvas hotkey"), QString()},
    };

    const QString tooltip = SnapTray::formatWindowsTrayTooltip(
        QStringLiteral("SnapTray"),
        QStringLiteral("2.11.3"),
        QStringLiteral("Development Build"),
        hotkeyLines,
        QStringLiteral("Not set"));

    QCOMPARE(tooltip,
             QStringLiteral("SnapTray 2.11.3 (Development Build)\n"
                            "Region Capture hotkey: Not set\n"
                            "Paste hotkey: F3\n"
                            "Screen Canvas hotkey: Not set"));
}

QTEST_MAIN(tst_TrayTooltipFormatter)
#include "tst_TrayTooltipFormatter.moc"
