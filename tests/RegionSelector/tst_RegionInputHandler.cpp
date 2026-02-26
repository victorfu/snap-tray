#include <QtTest>

#include <QMouseEvent>
#include <QSignalSpy>

#include "region/RegionInputHandler.h"
#include "region/RegionInputState.h"
#include "region/SelectionStateManager.h"

namespace {

QMouseEvent makeMouseEvent(QEvent::Type type,
                           const QPoint& pos,
                           Qt::MouseButton button,
                           Qt::MouseButtons buttons)
{
    const QPointF point(pos);
    return QMouseEvent(type, point, point, point, button, buttons, Qt::NoModifier);
}

} // namespace

class tst_RegionInputHandler : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testTinyMoveKeepsDetectedWindowSelection();
    void testNoMoveKeepsDetectedWindowSelection();
    void testTinyMoveWithoutDetectionFallsBackToFullScreen();
    void testLargeDragUsesDragSelectionInsteadOfPendingWindow();

private:
    RegionInputHandler* m_handler = nullptr;
    SelectionStateManager* m_selectionManager = nullptr;
    RegionInputState m_state;
};

void tst_RegionInputHandler::init()
{
    m_handler = new RegionInputHandler();
    m_selectionManager = new SelectionStateManager();
    m_selectionManager->setBounds(QRect(0, 0, 1920, 1080));

    m_state = RegionInputState();
    m_handler->setSelectionManager(m_selectionManager);
    m_handler->setSharedState(&m_state);
}

void tst_RegionInputHandler::cleanup()
{
    delete m_selectionManager;
    m_selectionManager = nullptr;

    delete m_handler;
    m_handler = nullptr;
}

void tst_RegionInputHandler::testTinyMoveKeepsDetectedWindowSelection()
{
    const QRect detectedWindow(120, 140, 220, 160);
    m_state.hasDetectedWindow = true;
    m_state.highlightedWindowRect = detectedWindow;

    QSignalSpy fullScreenSpy(m_handler, &RegionInputHandler::fullScreenSelectionRequested);
    QSignalSpy selectionFinishedSpy(m_handler, &RegionInputHandler::selectionFinished);

    auto pressEvent = makeMouseEvent(QEvent::MouseButtonPress, QPoint(160, 180), Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&pressEvent);

    auto moveEvent = makeMouseEvent(QEvent::MouseMove, QPoint(163, 182), Qt::NoButton, Qt::LeftButton);
    m_handler->handleMouseMove(&moveEvent);

    auto releaseEvent = makeMouseEvent(QEvent::MouseButtonRelease, QPoint(163, 182), Qt::LeftButton, Qt::NoButton);
    m_handler->handleMouseRelease(&releaseEvent);

    QCOMPARE(fullScreenSpy.count(), 0);
    QCOMPARE(selectionFinishedSpy.count(), 1);
    QVERIFY(m_selectionManager->isComplete());
    QCOMPARE(m_selectionManager->selectionRect(), detectedWindow);
}

void tst_RegionInputHandler::testNoMoveKeepsDetectedWindowSelection()
{
    const QRect detectedWindow(40, 60, 280, 190);
    m_state.hasDetectedWindow = true;
    m_state.highlightedWindowRect = detectedWindow;

    QSignalSpy fullScreenSpy(m_handler, &RegionInputHandler::fullScreenSelectionRequested);
    QSignalSpy selectionFinishedSpy(m_handler, &RegionInputHandler::selectionFinished);

    auto pressEvent = makeMouseEvent(QEvent::MouseButtonPress, QPoint(90, 90), Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&pressEvent);

    auto releaseEvent = makeMouseEvent(QEvent::MouseButtonRelease, QPoint(90, 90), Qt::LeftButton, Qt::NoButton);
    m_handler->handleMouseRelease(&releaseEvent);

    QCOMPARE(fullScreenSpy.count(), 0);
    QCOMPARE(selectionFinishedSpy.count(), 1);
    QVERIFY(m_selectionManager->isComplete());
    QCOMPARE(m_selectionManager->selectionRect(), detectedWindow);
}

void tst_RegionInputHandler::testTinyMoveWithoutDetectionFallsBackToFullScreen()
{
    m_state.hasDetectedWindow = false;
    m_state.highlightedWindowRect = QRect();

    QSignalSpy fullScreenSpy(m_handler, &RegionInputHandler::fullScreenSelectionRequested);
    QSignalSpy selectionFinishedSpy(m_handler, &RegionInputHandler::selectionFinished);

    auto pressEvent = makeMouseEvent(QEvent::MouseButtonPress, QPoint(200, 220), Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&pressEvent);

    auto moveEvent = makeMouseEvent(QEvent::MouseMove, QPoint(203, 223), Qt::NoButton, Qt::LeftButton);
    m_handler->handleMouseMove(&moveEvent);

    auto releaseEvent = makeMouseEvent(QEvent::MouseButtonRelease, QPoint(203, 223), Qt::LeftButton, Qt::NoButton);
    m_handler->handleMouseRelease(&releaseEvent);

    QCOMPARE(fullScreenSpy.count(), 1);
    QCOMPARE(selectionFinishedSpy.count(), 1);
    QVERIFY(!m_selectionManager->isComplete());
}

void tst_RegionInputHandler::testLargeDragUsesDragSelectionInsteadOfPendingWindow()
{
    const QRect detectedWindow(500, 520, 180, 140);
    m_state.hasDetectedWindow = true;
    m_state.highlightedWindowRect = detectedWindow;

    QSignalSpy fullScreenSpy(m_handler, &RegionInputHandler::fullScreenSelectionRequested);
    QSignalSpy selectionFinishedSpy(m_handler, &RegionInputHandler::selectionFinished);

    auto pressEvent = makeMouseEvent(QEvent::MouseButtonPress, QPoint(100, 110), Qt::LeftButton, Qt::LeftButton);
    m_handler->handleMousePress(&pressEvent);

    auto moveEvent = makeMouseEvent(QEvent::MouseMove, QPoint(145, 155), Qt::NoButton, Qt::LeftButton);
    m_handler->handleMouseMove(&moveEvent);

    auto releaseEvent = makeMouseEvent(QEvent::MouseButtonRelease, QPoint(145, 155), Qt::LeftButton, Qt::NoButton);
    m_handler->handleMouseRelease(&releaseEvent);

    QCOMPARE(fullScreenSpy.count(), 0);
    QCOMPARE(selectionFinishedSpy.count(), 1);
    QVERIFY(m_selectionManager->isComplete());

    const QRect selectedRect = m_selectionManager->selectionRect();
    QVERIFY(selectedRect.width() > 5);
    QVERIFY(selectedRect.height() > 5);
    QVERIFY(selectedRect != detectedWindow);
}

QTEST_MAIN(tst_RegionInputHandler)
#include "tst_RegionInputHandler.moc"
