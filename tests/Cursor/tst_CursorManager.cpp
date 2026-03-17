#include <QtTest/QtTest>

#include <QWidget>

#include "cursor/CursorAuthority.h"
#include "cursor/CursorManager.h"

class TestCursorManagerWidget : public QWidget
{
    Q_OBJECT
};

class TestCursorManager : public QObject
{
    Q_OBJECT

private slots:
    void testShadowModeKeepsLegacyCursor();
    void testAuthorityModeUsesResolvedCursorAndRestores();
    void testSelectionDragUsesClosedHandAndRestores();
};

void TestCursorManager::testShadowModeKeepsLegacyCursor()
{
    TestCursorManagerWidget widget;
    auto& manager = CursorManager::instance();
    auto& authority = CursorAuthority::instance();

    manager.registerWidget(&widget);
    authority.registerWidgetSurface(&widget, QStringLiteral("UnknownSurface"),
                                    CursorSurfaceMode::Shadow);

    manager.pushCursorForWidget(&widget, CursorContext::Tool, Qt::CrossCursor);
    authority.submitWidgetRequest(
        &widget, QStringLiteral("floating.overlay"), CursorRequestSource::Overlay,
        CursorStyleSpec::fromShape(Qt::PointingHandCursor));
    manager.reapplyCursorForWidget(&widget);

    QCOMPARE(widget.cursor().shape(), Qt::CrossCursor);

    authority.clearWidgetRequest(&widget, QStringLiteral("floating.overlay"));
    manager.unregisterWidget(&widget);
}

void TestCursorManager::testAuthorityModeUsesResolvedCursorAndRestores()
{
    TestCursorManagerWidget widget;
    auto& manager = CursorManager::instance();
    auto& authority = CursorAuthority::instance();

    manager.registerWidget(&widget);
    authority.registerWidgetSurface(&widget, QStringLiteral("RegionSelector"),
                                    CursorSurfaceMode::Authority);

    manager.pushCursorForWidget(&widget, CursorContext::Tool, Qt::CrossCursor);
    manager.reapplyCursorForWidget(&widget);
    QCOMPARE(widget.cursor().shape(), Qt::CrossCursor);

    authority.submitWidgetRequest(
        &widget, QStringLiteral("floating.overlay"), CursorRequestSource::Overlay,
        CursorStyleSpec::fromShape(Qt::PointingHandCursor));
    manager.reapplyCursorForWidget(&widget);

    QCOMPARE(widget.cursor().shape(), Qt::PointingHandCursor);
    QVERIFY(authority.lastLegacyComparisonMismatched(authority.surfaceIdForWidget(&widget)));

    authority.clearWidgetRequest(&widget, QStringLiteral("floating.overlay"));
    manager.reapplyCursorForWidget(&widget);
    QCOMPARE(widget.cursor().shape(), Qt::CrossCursor);

    manager.unregisterWidget(&widget);
}

void TestCursorManager::testSelectionDragUsesClosedHandAndRestores()
{
    TestCursorManagerWidget widget;
    auto& manager = CursorManager::instance();
    auto& authority = CursorAuthority::instance();

    manager.registerWidget(&widget);
    authority.registerWidgetSurface(&widget, QStringLiteral("RegionSelector"),
                                    CursorSurfaceMode::Authority);

    manager.pushCursorForWidget(&widget, CursorContext::Tool, Qt::CrossCursor);
    manager.reapplyCursorForWidget(&widget);
    QCOMPARE(widget.cursor().shape(), Qt::CrossCursor);

    manager.setDragStateForWidget(&widget, DragState::SelectionDrag);
    QCOMPARE(widget.cursor().shape(), Qt::ClosedHandCursor);

    manager.setDragStateForWidget(&widget, DragState::None);
    QCOMPARE(widget.cursor().shape(), Qt::CrossCursor);

    manager.unregisterWidget(&widget);
}

QTEST_MAIN(TestCursorManager)
#include "tst_CursorManager.moc"
