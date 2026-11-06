#include "platform/MacTrayAccessibility.h"

#include <QMenu>
#include <QSystemTrayIcon>
#include <QtTest>

// #248: the tray icon had only its tooltip, so VoiceOver announced "status
// menu" without saying it is SnapTray or that a recording is running.
class tst_MacTrayAccessibility : public QObject
{
    Q_OBJECT

private slots:
    void labelsTheStatusItem();
};

void tst_MacTrayAccessibility::labelsTheStatusItem()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QSKIP("No system tray in this session");
    }
    QVERIFY(!SnapTray::setMacTrayAccessibilityLabel(QStringLiteral("before any tray")));

    QSystemTrayIcon tray;
    QPixmap icon(16, 16);
    icon.fill(Qt::black);
    tray.setIcon(QIcon(icon));
    QMenu menu;
    tray.setContextMenu(&menu);
    tray.show();
    QTRY_VERIFY(SnapTray::setMacTrayAccessibilityLabel(QStringLiteral("SnapTray")));
    QCOMPARE(SnapTray::macTrayAccessibilityLabel(), QStringLiteral("SnapTray"));

    // The status changes while recording; the label follows.
    QVERIFY(SnapTray::setMacTrayAccessibilityLabel(QStringLiteral("SnapTray, Recording · 0:11")));
    QCOMPARE(SnapTray::macTrayAccessibilityLabel(), QStringLiteral("SnapTray, Recording · 0:11"));
    tray.hide();
}

QTEST_MAIN(tst_MacTrayAccessibility)
#include "tst_MacTrayAccessibility.moc"
