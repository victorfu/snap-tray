#include <QtTest/QtTest>

#include "RegionSelector.h"
#include "cursor/CursorManager.h"
#include "tools/ToolManager.h"

#include <QApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QMenu>

class TestRegionSelectorStyleSync : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testPopupAwayFromCursorKeepsRegionCrossCursor();
    void testPopupUnderCursorUsesArrowCursor();
};

void TestRegionSelectorStyleSync::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for RegionSelector tests in this environment.");
    }
}

void TestRegionSelectorStyleSync::testPopupAwayFromCursorKeepsRegionCrossCursor()
{
    RegionSelector selector;
    selector.resize(420, 260);
    selector.show();
    QTRY_VERIFY(selector.isVisible());

    QVERIFY(selector.m_toolManager);

    selector.m_inputState.currentTool = ToolId::Pencil;
    selector.m_toolManager->setCurrentTool(ToolId::Pencil);
    selector.setToolCursor();

    const QPoint regionCenter = selector.rect().center();
    QCursor::setPos(selector.mapToGlobal(regionCenter));
    QTRY_VERIFY(selector.rect().contains(selector.mapFromGlobal(QCursor::pos())));

    auto& cursorManager = CursorManager::instance();
    cursorManager.reapplyCursorForWidget(&selector);
    QTRY_COMPARE(selector.cursor().shape(), Qt::CrossCursor);

    QMenu popup(&selector);
    popup.addAction(QStringLiteral("Solid"));
    popup.addAction(QStringLiteral("Dashed"));
    popup.popup(selector.mapToGlobal(QPoint(12, 12)));
    QTRY_VERIFY(popup.isVisible());
    QTRY_COMPARE(QApplication::activePopupWidget(), static_cast<QWidget*>(&popup));
    QVERIFY(!popup.frameGeometry().contains(QCursor::pos()));

    selector.syncFloatingUiCursor();
    QTRY_COMPARE(selector.cursor().shape(), Qt::CrossCursor);

    popup.close();
    QTRY_VERIFY(!popup.isVisible());
}

void TestRegionSelectorStyleSync::testPopupUnderCursorUsesArrowCursor()
{
    RegionSelector selector;
    selector.resize(420, 260);
    selector.show();
    QTRY_VERIFY(selector.isVisible());

    QVERIFY(selector.m_toolManager);

    selector.m_inputState.currentTool = ToolId::Pencil;
    selector.m_toolManager->setCurrentTool(ToolId::Pencil);
    selector.setToolCursor();

    QMenu popup(&selector);
    popup.addAction(QStringLiteral("Solid"));
    popup.addAction(QStringLiteral("Dashed"));
    popup.popup(selector.mapToGlobal(QPoint(12, 12)));
    QTRY_VERIFY(popup.isVisible());

    const QPoint popupCenter = popup.frameGeometry().center();
    QCursor::setPos(popupCenter);
    QTRY_VERIFY(popup.frameGeometry().contains(QCursor::pos()));

    selector.syncFloatingUiCursor();
    QTRY_COMPARE(selector.cursor().shape(), Qt::ArrowCursor);

    popup.close();
    QTRY_VERIFY(!popup.isVisible());
}

QTEST_MAIN(TestRegionSelectorStyleSync)
#include "tst_StyleSync.moc"
