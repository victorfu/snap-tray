#include <QtTest/QtTest>

#include "RegionSelector.h"
#include "cursor/CursorAuthority.h"
#include "cursor/CursorManager.h"
#include "tools/ToolManager.h"

class TestRegionSelectorStyleSync : public QObject
{
    Q_OBJECT

private slots:
    void testUsesAuthorityModeByDefault();
    void testPopupRestoreReturnsMosaicCursor();
};

void TestRegionSelectorStyleSync::testUsesAuthorityModeByDefault()
{
    RegionSelector selector;
    QCOMPARE(CursorAuthority::instance().modeForWidget(&selector), CursorSurfaceMode::Authority);
}

void TestRegionSelectorStyleSync::testPopupRestoreReturnsMosaicCursor()
{
    RegionSelector selector;
    auto& authority = CursorAuthority::instance();
    auto& cursorManager = CursorManager::instance();

    selector.m_selectionManager->setSelectionRect(QRect(40, 40, 160, 120));
    selector.m_toolManager->setCurrentTool(ToolId::Mosaic);
    selector.m_toolManager->setWidth(18);
    cursorManager.updateToolCursorForWidget(&selector);
    cursorManager.reapplyCursorForWidget(&selector);

    const QCursor toolCursor = selector.cursor();
    QVERIFY(!toolCursor.pixmap().isNull());

    authority.submitWidgetRequest(
        &selector, QStringLiteral("floating.popup"), CursorRequestSource::Popup,
        CursorStyleSpec::fromShape(Qt::ArrowCursor));
    cursorManager.reapplyCursorForWidget(&selector);
    QCOMPARE(selector.cursor().shape(), Qt::ArrowCursor);

    authority.clearWidgetRequest(&selector, QStringLiteral("floating.popup"));
    cursorManager.reapplyCursorForWidget(&selector);

    const QCursor restoredCursor = selector.cursor();
    QVERIFY(!restoredCursor.pixmap().isNull());
    QCOMPARE(restoredCursor.shape(), Qt::BitmapCursor);
    QCOMPARE(restoredCursor.hotSpot(), toolCursor.hotSpot());
    QCOMPARE(restoredCursor.pixmap().deviceIndependentSize(),
             toolCursor.pixmap().deviceIndependentSize());
}

QTEST_MAIN(TestRegionSelectorStyleSync)
#include "tst_StyleSync.moc"
