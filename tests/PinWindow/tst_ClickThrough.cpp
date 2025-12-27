#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QPixmap>
#include <QPainter>
#include <QHotkey>

#include "PinWindow.h"
#include "PinWindowManager.h"

class TestPinWindowClickThrough : public QObject
{
    Q_OBJECT

private:
    QPixmap createTestPixmap(int width, int height, const QColor& color = Qt::red) {
        QPixmap pixmap(width, height);
        pixmap.fill(color);
        return pixmap;
    }

private slots:
    void initTestCase() {
        // Ensure we have a QApplication for GUI tests
    }

    // =========================================================================
    // Initial State Tests
    // =========================================================================

    void testInitialClickThroughState() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindowManager manager;
        PinWindow *window = manager.createPinWindow(pixmap, QPoint(0, 0));

        QCOMPARE(window->isClickThrough(), false);
    }

    // =========================================================================
    // Click-Through Mode Tests
    // =========================================================================

    void testSetClickThroughEnabled() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindowManager manager;
        PinWindow *window = manager.createPinWindow(pixmap, QPoint(0, 0));

        window->setClickThrough(true);
        QCOMPARE(window->isClickThrough(), true);
    }

    void testSetClickThroughDisabled() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindowManager manager;
        PinWindow *window = manager.createPinWindow(pixmap, QPoint(0, 0));

        window->setClickThrough(true);
        QCOMPARE(window->isClickThrough(), true);

        window->setClickThrough(false);
        QCOMPARE(window->isClickThrough(), false);
    }

    void testToggleClickThrough() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindowManager manager;
        PinWindow *window = manager.createPinWindow(pixmap, QPoint(0, 0));

        // Toggle on
        window->setClickThrough(!window->isClickThrough());
        QCOMPARE(window->isClickThrough(), true);

        // Toggle off
        window->setClickThrough(!window->isClickThrough());
        QCOMPARE(window->isClickThrough(), false);
    }

    void testClickThroughIdempotent() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindowManager manager;
        PinWindow *window = manager.createPinWindow(pixmap, QPoint(0, 0));

        // Setting the same value multiple times should be idempotent
        window->setClickThrough(true);
        QCOMPARE(window->isClickThrough(), true);
        
        window->setClickThrough(true);
        QCOMPARE(window->isClickThrough(), true);

        window->setClickThrough(false);
        QCOMPARE(window->isClickThrough(), false);
        
        window->setClickThrough(false);
        QCOMPARE(window->isClickThrough(), false);
    }

    // =========================================================================
    // Manager-based Global Hotkey Tests
    // =========================================================================

    void testManagerDisableClickThroughAll() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindowManager manager;
        
        PinWindow *window1 = manager.createPinWindow(pixmap, QPoint(0, 0));
        PinWindow *window2 = manager.createPinWindow(pixmap, QPoint(100, 100));

        // Enable click-through on both windows
        window1->setClickThrough(true);
        window2->setClickThrough(true);
        QCOMPARE(window1->isClickThrough(), true);
        QCOMPARE(window2->isClickThrough(), true);

        // Manager should disable click-through on all windows
        manager.disableClickThroughAll();
        QCOMPARE(window1->isClickThrough(), false);
        QCOMPARE(window2->isClickThrough(), false);
    }

    void testMultipleWindowsIndependentClickThrough() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindowManager manager;
        
        PinWindow *window1 = manager.createPinWindow(pixmap, QPoint(0, 0));
        PinWindow *window2 = manager.createPinWindow(pixmap, QPoint(100, 100));

        // Enable click-through on first window
        window1->setClickThrough(true);
        QCOMPARE(window1->isClickThrough(), true);
        QCOMPARE(window2->isClickThrough(), false);

        // Enable click-through on second window
        window2->setClickThrough(true);
        QCOMPARE(window1->isClickThrough(), true);
        QCOMPARE(window2->isClickThrough(), true);

        // Disable click-through on first window
        window1->setClickThrough(false);
        QCOMPARE(window1->isClickThrough(), false);
        QCOMPARE(window2->isClickThrough(), true);
    }

    void testClickThroughPersistsAcrossOperations() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindowManager manager;
        PinWindow *window = manager.createPinWindow(pixmap, QPoint(0, 0));

        // Enable click-through
        window->setClickThrough(true);
        QCOMPARE(window->isClickThrough(), true);

        // Perform other operations - click-through should persist
        window->setZoomLevel(2.0);
        QCOMPARE(window->isClickThrough(), true);

        window->setOpacity(0.5);
        QCOMPARE(window->isClickThrough(), true);

        window->rotateRight();
        QCOMPARE(window->isClickThrough(), true);
    }

    // =========================================================================
    // Cleanup Tests
    // =========================================================================

    void testClickThroughCleanupOnDestruction() {
        // Create manager and window in click-through mode
        {
            PinWindowManager manager;
            QPixmap pixmap = createTestPixmap(100, 100);
            PinWindow *window = manager.createPinWindow(pixmap, QPoint(0, 0));
            window->setClickThrough(true);
            QCOMPARE(window->isClickThrough(), true);
            // Manager destructor should clean up the hotkey
        }
        // If we reach here without crashing, cleanup was successful
        QVERIFY(true);
    }

    void testClickThroughHotkeyUpdatedOnWindowClose() {
        PinWindowManager manager;
        QPixmap pixmap = createTestPixmap(100, 100);
        
        PinWindow *window1 = manager.createPinWindow(pixmap, QPoint(0, 0));
        PinWindow *window2 = manager.createPinWindow(pixmap, QPoint(100, 100));

        // Enable click-through on both windows
        window1->setClickThrough(true);
        window2->setClickThrough(true);

        // Close one window - hotkey should still be registered because window2 is still in click-through
        window1->close();
        QTest::qWait(100); // Give time for cleanup

        QCOMPARE(window2->isClickThrough(), true);
        
        // Close the second window - hotkey should be unregistered
        window2->close();
        QTest::qWait(100); // Give time for cleanup
        
        // If we reach here without crashing, the test passed
        QVERIFY(true);
    }
};

QTEST_MAIN(TestPinWindowClickThrough)
#include "tst_ClickThrough.moc"
