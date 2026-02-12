#include <QtTest>

#include <QMouseEvent>
#include <QWidget>

#include "region/RegionInputHandler.h"

class tst_MultiRegionReplaceFlow : public QObject
{
    Q_OBJECT

private slots:
    void testValidReplaceSelectionCompletes();
    void testInvalidReplaceSelectionCancels();
};

void tst_MultiRegionReplaceFlow::testValidReplaceSelectionCompletes()
{
    QWidget host;
    RegionInputState state;
    state.multiRegionMode = true;
    state.replaceTargetIndex = 0;

    SelectionStateManager selectionManager;
    selectionManager.setBounds(QRect(0, 0, 500, 300));

    RegionInputHandler handler;
    handler.setParentWidget(&host);
    handler.setSelectionManager(&selectionManager);
    handler.setSharedState(&state);

    QSignalSpy finishedSpy(&handler, &RegionInputHandler::selectionFinished);
    QSignalSpy cancelledSpy(&handler, &RegionInputHandler::replaceSelectionCancelled);

    selectionManager.startSelection(QPoint(20, 20));
    selectionManager.updateSelection(QPoint(80, 70));

    QMouseEvent releaseEvent(
        QEvent::MouseButtonRelease,
        QPointF(80, 70),
        QPointF(80, 70),
        Qt::LeftButton,
        Qt::NoButton,
        Qt::NoModifier);
    handler.handleMouseRelease(&releaseEvent);

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(cancelledSpy.count(), 0);
    QVERIFY(selectionManager.isComplete());
}

void tst_MultiRegionReplaceFlow::testInvalidReplaceSelectionCancels()
{
    QWidget host;
    RegionInputState state;
    state.multiRegionMode = true;
    state.replaceTargetIndex = 1;

    SelectionStateManager selectionManager;
    selectionManager.setBounds(QRect(0, 0, 500, 300));

    RegionInputHandler handler;
    handler.setParentWidget(&host);
    handler.setSelectionManager(&selectionManager);
    handler.setSharedState(&state);

    QSignalSpy finishedSpy(&handler, &RegionInputHandler::selectionFinished);
    QSignalSpy cancelledSpy(&handler, &RegionInputHandler::replaceSelectionCancelled);

    selectionManager.startSelection(QPoint(30, 30));
    selectionManager.updateSelection(QPoint(32, 32));

    QMouseEvent releaseEvent(
        QEvent::MouseButtonRelease,
        QPointF(32, 32),
        QPointF(32, 32),
        Qt::LeftButton,
        Qt::NoButton,
        Qt::NoModifier);
    handler.handleMouseRelease(&releaseEvent);

    QCOMPARE(finishedSpy.count(), 0);
    QCOMPARE(cancelledSpy.count(), 1);
    QCOMPARE(selectionManager.state(), SelectionStateManager::State::None);
}

QTEST_MAIN(tst_MultiRegionReplaceFlow)
#include "tst_MultiRegionReplaceFlow.moc"
