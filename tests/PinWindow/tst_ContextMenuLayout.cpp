#include <QtTest>
#include <QAction>
#include <QMenu>
#include <QtQml/qqmlextensionplugin.h>

#include "PinWindow.h"
#include "PlatformFeatures.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

// #225: the pin context menu, grouped by task.
class TestPinContextMenuLayout : public QObject
{
    Q_OBJECT
private slots:
    void testContextMenuGroups();
    void testZoomAndOpacityResetItems();
    void testShortcutHintsShown();

private:
    static QPixmap pixmap()
    {
        QPixmap p(240, 160);
        p.fill(Qt::darkYellow);
        return p;
    }
    static QList<QStringList> groupsOf(QMenu* menu)
    {
        QList<QStringList> groups{{}};
        for (QAction* action : menu->actions()) {
            if (action->isSeparator()) {
                groups.append(QStringList());
                continue;
            }
            groups.last().append(action->objectName());
        }
        return groups;
    }
    static QAction* findAction(QMenu* menu, const QString& name)
    {
        for (QAction* action : menu->findChildren<QAction*>())
            if (action->objectName() == name) return action;
        for (QAction* action : menu->actions())
            if (action->objectName() == name) return action;
        return nullptr;
    }
};

void TestPinContextMenuLayout::testContextMenuGroups()
{
    PinWindow window(pixmap(), QPoint(0, 0), nullptr, false, false);
    window.createContextMenu();
    QStringList modes = {"pinMenuLive", "pinMenuLivePause", "pinMenuFrameRate", "pinMenuRegionLayout"};
    if (PlatformFeatures::instance().capabilities().supportsClickThrough)
        modes.prepend("pinMenuClickThrough");
    // Arrange (#268) leads; it is hidden unless an annotation is selected.
    const QList<QStringList> expected = {
        {"pinMenuArrange"},
        {"pinMenuShowToolbar", "pinMenuShowBorder", "pinMenuZoom", "pinMenuOpacity", "pinMenuImageProcessing"},
        {"pinMenuCopy", "pinMenuSave", "pinMenuWatermark", "pinMenuInfo", "pinMenuHistoryFolder"},
        modes,
        {"pinMenuMoveToScreen", "pinMenuMerge", "pinMenuCloseAll"},
        {"pinMenuClose"}};
    QCOMPARE(groupsOf(window.m_contextMenu), expected);
}

void TestPinContextMenuLayout::testZoomAndOpacityResetItems()
{
    PinWindow window(pixmap(), QPoint(0, 0), nullptr, false, false);
    window.createContextMenu();
    window.setZoomLevel(2.0);
    window.setOpacity(0.4);
    QAction* resetZoom = findAction(window.m_contextMenu, "pinMenuZoomReset");
    QAction* resetOpacity = findAction(window.m_contextMenu, "pinMenuOpacityReset");
    QVERIFY(resetZoom && resetOpacity);
    resetZoom->trigger();
    resetOpacity->trigger();
    QCOMPARE(window.zoomLevel(), 1.0);
    QCOMPARE(window.opacity(), 1.0);
}

void TestPinContextMenuLayout::testShortcutHintsShown()
{
    PinWindow window(pixmap(), QPoint(0, 0), nullptr, false, false);
    window.createContextMenu();
    for (const char* name : {"pinMenuZoomReset", "pinMenuZoomIn", "pinMenuOpacityReset", "pinMenuLive", "pinMenuLivePause"})
        QVERIFY2(findAction(window.m_contextMenu, QString::fromLatin1(name)), name);
    QCOMPARE(findAction(window.m_contextMenu, "pinMenuZoomReset")->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_0));
    QCOMPARE(findAction(window.m_contextMenu, "pinMenuZoomIn")->shortcut(), QKeySequence(QKeySequence::ZoomIn));
    QCOMPARE(findAction(window.m_contextMenu, "pinMenuOpacityReset")->shortcut(),
             QKeySequence(Qt::CTRL | Qt::Key_BracketRight));
    QCOMPARE(findAction(window.m_contextMenu, "pinMenuLive")->shortcut(), QKeySequence(Qt::Key_L));
    QCOMPARE(findAction(window.m_contextMenu, "pinMenuLivePause")->shortcut(), QKeySequence(Qt::Key_P));
}

QTEST_MAIN(TestPinContextMenuLayout)
#include "tst_ContextMenuLayout.moc"
