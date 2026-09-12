#include <QtTest>
#include "region/RegionToolbarHandler.h"
#include "tools/ToolManager.h"

class tst_ToolbarState : public QObject
{
    Q_OBJECT
private slots:
    void togglesSynchronizeBeforeNotification_data();
    void togglesSynchronizeBeforeNotification();
};

void tst_ToolbarState::togglesSynchronizeBeforeNotification_data()
{
    QTest::addColumn<ToolId>("tool");
    QTest::newRow("step-badge") << ToolId::StepBadge;
    QTest::newRow("mosaic") << ToolId::Mosaic;
}

void tst_ToolbarState::togglesSynchronizeBeforeNotification()
{
    QFETCH(ToolId, tool);
    ToolManager manager;
    RegionToolbarHandler handler;
    handler.setToolManager(&manager);
    ToolId notified = ToolId::Selection;
    int notifications = 0;
    connect(&handler, &RegionToolbarHandler::toolChanged, this, [&](ToolId selected, bool shown) {
        ++notifications;
        notified = selected;
        QVERIFY(shown);
        QCOMPARE(manager.currentTool(), selected);
    });
    for (int i = 0; i < 3; ++i) {
        handler.handleToolbarClick(tool);
        QCOMPARE(notified, tool);
        handler.handleToolbarClick(tool);
        QCOMPARE(notified, ToolId::Selection);
        QCOMPARE(manager.currentTool(), ToolId::Selection);
    }
    QCOMPARE(notifications, 6);
    handler.handleToolbarClick(ToolId::Pencil);
    QCOMPARE(manager.currentTool(), ToolId::Pencil);
}

QTEST_MAIN(tst_ToolbarState)
#include "tst_ToolbarState.moc"
