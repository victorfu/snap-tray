#include <QtTest/QtTest>

#include <QApplication>
#include <QPixmap>
#include <QWidget>

#include "region/CaptureChromeWindow.h"
#include "region/SelectionStateManager.h"
#include "region/StaticCaptureBackgroundWindow.h"

class tst_CaptureChromeWindow : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testStaticBackgroundWindowTracksHost();
    void testSelectionModeTracksDimensionRect();
    void testHighlightModeTracksDimensionRect();
    void testHideClearsDimensionRect();
    void testPaintCompletedSignalEmitted();
};

void tst_CaptureChromeWindow::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for capture chrome window tests.");
    }
}

void tst_CaptureChromeWindow::testStaticBackgroundWindowTracksHost()
{
    QWidget host;
    host.resize(320, 240);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    QPixmap background(host.size());
    background.fill(Qt::red);

    StaticCaptureBackgroundWindow backgroundWindow;
    backgroundWindow.syncToHost(&host, background, true);

    QVERIFY(backgroundWindow.isVisible());
    QCOMPARE(backgroundWindow.geometry(),
             QRect(host.mapToGlobal(QPoint(0, 0)), host.size()));

    backgroundWindow.hideOverlay();
    QVERIFY(!backgroundWindow.isVisible());
}

void tst_CaptureChromeWindow::testSelectionModeTracksDimensionRect()
{
    QWidget host;
    host.resize(320, 240);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    SelectionStateManager selectionManager;
    selectionManager.setSelectionRect(QRect(20, 30, 120, 80));

    CaptureChromeWindow chrome;
    chrome.setSelectionManager(&selectionManager);
    chrome.syncToHost(&host,
                      QRect(20, 30, 120, 80),
                      true,
                      QRect(),
                      1.0,
                      0,
                      0,
                      false,
                      nullptr,
                      false,
                      QPoint(),
                      true);

    QVERIFY(chrome.isVisible());
    QTRY_VERIFY(chrome.lastDimensionInfoRect().isValid());
    QVERIFY(!chrome.lastDimensionInfoRect().isEmpty());
}

void tst_CaptureChromeWindow::testHighlightModeTracksDimensionRect()
{
    QWidget host;
    host.resize(320, 240);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    SelectionStateManager selectionManager;

    CaptureChromeWindow chrome;
    chrome.setSelectionManager(&selectionManager);
    chrome.syncToHost(&host,
                      QRect(),
                      false,
                      QRect(40, 50, 100, 60),
                      1.0,
                      0,
                      0,
                      false,
                      nullptr,
                      false,
                      QPoint(),
                      true);

    QVERIFY(chrome.isVisible());
    QTRY_VERIFY(chrome.lastDimensionInfoRect().isValid());
    QVERIFY(!chrome.lastDimensionInfoRect().isEmpty());
}

void tst_CaptureChromeWindow::testHideClearsDimensionRect()
{
    QWidget host;
    host.resize(320, 240);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    SelectionStateManager selectionManager;
    selectionManager.setSelectionRect(QRect(20, 30, 120, 80));

    CaptureChromeWindow chrome;
    chrome.setSelectionManager(&selectionManager);
    chrome.syncToHost(&host,
                      QRect(20, 30, 120, 80),
                      true,
                      QRect(),
                      1.0,
                      0,
                      0,
                      false,
                      nullptr,
                      false,
                      QPoint(),
                      true);
    QTRY_VERIFY(chrome.lastDimensionInfoRect().isValid());

    chrome.hideOverlay();
    QVERIFY(!chrome.isVisible());
    QVERIFY(!chrome.lastDimensionInfoRect().isValid());
}

void tst_CaptureChromeWindow::testPaintCompletedSignalEmitted()
{
    QWidget host;
    host.resize(320, 240);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));

    SelectionStateManager selectionManager;
    selectionManager.setSelectionRect(QRect(24, 36, 100, 70));

    CaptureChromeWindow chrome;
    chrome.setSelectionManager(&selectionManager);

    QSignalSpy paintSpy(&chrome, SIGNAL(framePainted()));

    chrome.syncToHost(&host,
                      QRect(24, 36, 100, 70),
                      true,
                      QRect(),
                      1.0,
                      0,
                      0,
                      false,
                      nullptr,
                      false,
                      QPoint(),
                      true);

    QTRY_VERIFY(!paintSpy.isEmpty());
}

QTEST_MAIN(tst_CaptureChromeWindow)
#include "tst_CaptureChromeWindow.moc"
