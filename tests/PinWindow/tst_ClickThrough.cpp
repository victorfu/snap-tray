#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QPixmap>
#include <QPainter>
#include <QHotkey>

#include "PinWindow.h"

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
        PinWindow window(pixmap, QPoint(0, 0));

        QCOMPARE(window.isClickThrough(), false);
    }

    // =========================================================================
    // Click-Through Mode Tests
    // =========================================================================

    void testSetClickThroughEnabled() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        window.setClickThrough(true);
        QCOMPARE(window.isClickThrough(), true);
    }

    void testSetClickThroughDisabled() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        window.setClickThrough(true);
        QCOMPARE(window.isClickThrough(), true);

        window.setClickThrough(false);
        QCOMPARE(window.isClickThrough(), false);
    }

    void testToggleClickThrough() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        // Toggle on
        window.setClickThrough(!window.isClickThrough());
        QCOMPARE(window.isClickThrough(), true);

        // Toggle off
        window.setClickThrough(!window.isClickThrough());
        QCOMPARE(window.isClickThrough(), false);
    }

    void testClickThroughIdempotent() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        // Setting the same value multiple times should be idempotent
        window.setClickThrough(true);
        QCOMPARE(window.isClickThrough(), true);
        
        window.setClickThrough(true);
        QCOMPARE(window.isClickThrough(), true);

        window.setClickThrough(false);
        QCOMPARE(window.isClickThrough(), false);
        
        window.setClickThrough(false);
        QCOMPARE(window.isClickThrough(), false);
    }

    // =========================================================================
    // Global Hotkey Tests
    // =========================================================================

    void testClickThroughHotkeyLifecycle() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        // Initially, no hotkey should be registered
        QCOMPARE(window.isClickThrough(), false);

        // Enable click-through mode - hotkey should be registered
        window.setClickThrough(true);
        QCOMPARE(window.isClickThrough(), true);
        // Note: We can't directly verify the hotkey registration in tests
        // because QHotkey might fail to register in a test environment

        // Disable click-through mode - hotkey should be unregistered
        window.setClickThrough(false);
        QCOMPARE(window.isClickThrough(), false);
    }

    void testMultipleWindowsIndependentClickThrough() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window1(pixmap, QPoint(0, 0));
        PinWindow window2(pixmap, QPoint(100, 100));

        // Enable click-through on first window
        window1.setClickThrough(true);
        QCOMPARE(window1.isClickThrough(), true);
        QCOMPARE(window2.isClickThrough(), false);

        // Enable click-through on second window
        window2.setClickThrough(true);
        QCOMPARE(window1.isClickThrough(), true);
        QCOMPARE(window2.isClickThrough(), true);

        // Disable click-through on first window
        window1.setClickThrough(false);
        QCOMPARE(window1.isClickThrough(), false);
        QCOMPARE(window2.isClickThrough(), true);
    }

    void testClickThroughPersistsAcrossOperations() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        // Enable click-through
        window.setClickThrough(true);
        QCOMPARE(window.isClickThrough(), true);

        // Perform other operations - click-through should persist
        window.setZoomLevel(2.0);
        QCOMPARE(window.isClickThrough(), true);

        window.setOpacity(0.5);
        QCOMPARE(window.isClickThrough(), true);

        window.rotateRight();
        QCOMPARE(window.isClickThrough(), true);
    }

    // =========================================================================
    // Cleanup Tests
    // =========================================================================

    void testClickThroughCleanupOnDestruction() {
        // Create window in click-through mode
        {
            QPixmap pixmap = createTestPixmap(100, 100);
            PinWindow window(pixmap, QPoint(0, 0));
            window.setClickThrough(true);
            QCOMPARE(window.isClickThrough(), true);
            // Window destructor should clean up the hotkey
        }
        // If we reach here without crashing, cleanup was successful
        QVERIFY(true);
    }
};

QTEST_MAIN(TestPinWindowClickThrough)
#include "tst_ClickThrough.moc"
