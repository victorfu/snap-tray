#include <QtTest/QtTest>
#include <QGuiApplication>
#include <QPixmap>
#include <QPainter>
#include <QMouseEvent>

#include "PinWindow.h"

class TestPinWindowResize : public QObject
{
    Q_OBJECT

private:
    QPixmap createTestPixmap(int width, int height, const QColor& color = Qt::red) {
        QPixmap pixmap(width, height);
        pixmap.fill(color);
        return pixmap;
    }

    static constexpr int kResizeMargin = 6;
    static constexpr int kMinSize = 50;

private slots:
    void initTestCase() {
        if (QGuiApplication::screens().isEmpty()) {
            QSKIP("No screens available for PinWindow tests in this environment.");
        }
    }

    // =========================================================================
    // Initial Size Tests
    // =========================================================================

    void testInitialWindowSize() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        // Window size = pixmap size
        QCOMPARE(window.size(), QSize(100, 100));
    }

    void testInitialWindowSizeNonSquare() {
        QPixmap pixmap = createTestPixmap(200, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QCOMPARE(window.size(), QSize(200, 100));
    }

    void testInitialPosition() {
        QPixmap pixmap = createTestPixmap(100, 100);
        QPoint requestedPos(100, 200);
        PinWindow window(pixmap, requestedPos);

        QCOMPARE(window.pos(), requestedPos);
    }

    // =========================================================================
    // Zoom and Size Tests
    // =========================================================================

    void testZoomAffectsSize() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QSize originalSize = window.size();

        window.setZoomLevel(2.0);
        QSize zoomedSize = window.size();

        // At 2x zoom, the window should be twice the size
        QCOMPARE(zoomedSize.width(), originalSize.width() * 2);
    }

    void testRotationAffectsSize() {
        // Use non-square pixmap to see size change
        QPixmap pixmap = createTestPixmap(200, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        int originalWidth = window.size().width();
        int originalHeight = window.size().height();

        window.rotateRight();

        int rotatedWidth = window.size().width();
        int rotatedHeight = window.size().height();

        // After 90 degree rotation, width and height should swap
        QCOMPARE(rotatedWidth, originalHeight);
        QCOMPARE(rotatedHeight, originalWidth);
    }

    // =========================================================================
    // Combined Transform and Size Tests
    // =========================================================================

    void testZoomAfterRotation() {
        QPixmap pixmap = createTestPixmap(200, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        window.rotateRight();
        QSize afterRotation = window.size();

        window.setZoomLevel(2.0);
        QSize afterZoom = window.size();

        // Content should double
        QCOMPARE(afterZoom.width(), afterRotation.width() * 2);
    }

    // =========================================================================
    // Window Attribute Tests
    // =========================================================================

    void testWindowIsFrameless() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QVERIFY(window.windowFlags() & Qt::FramelessWindowHint);
    }

    void testWindowStaysOnTop() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QVERIFY(window.windowFlags() & Qt::WindowStaysOnTopHint);
    }

    void testWindowHasTranslucentBackground() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QVERIFY(window.testAttribute(Qt::WA_TranslucentBackground));
    }

    void testWindowDeletesOnClose() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QVERIFY(window.testAttribute(Qt::WA_DeleteOnClose));
    }

    // =========================================================================
    // Mouse Tracking Tests
    // =========================================================================

    void testMouseTrackingEnabled() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QVERIFY(window.hasMouseTracking());
    }

    // =========================================================================
    // Keyboard Shortcut Tests (via keyPressEvent)
    // =========================================================================

    void testEscapeKeyClosesWindow() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow *window = new PinWindow(pixmap, QPoint(0, 0));

        QSignalSpy closeSpy(window, &PinWindow::closed);
        QVERIFY(closeSpy.isValid());

        QKeyEvent escEvent(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &escEvent);

        QCOMPARE(closeSpy.count(), 1);
    }

    void testKey1RotatesRight() {
        QPixmap pixmap = createTestPixmap(200, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QSize originalSize = window.size();

        QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_1, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &keyEvent);

        // Size should change due to rotation
        QVERIFY(window.size() != originalSize);
    }

    void testKey2RotatesLeft() {
        QPixmap pixmap = createTestPixmap(200, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QSize originalSize = window.size();

        QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_2, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &keyEvent);

        // Size should change due to rotation
        QVERIFY(window.size() != originalSize);
    }

    void testKey3FlipsHorizontal() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QSize originalSize = window.size();

        QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_3, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &keyEvent);

        // Size should remain the same for flip
        QCOMPARE(window.size(), originalSize);
    }

    void testKey4FlipsVertical() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QSize originalSize = window.size();

        QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_4, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &keyEvent);

        // Size should remain the same for flip
        QCOMPARE(window.size(), originalSize);
    }

    // =========================================================================
    // Double Click Test
    // =========================================================================

    void testDoubleClickClosesWindow() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow *window = new PinWindow(pixmap, QPoint(0, 0));

        QSignalSpy closeSpy(window, &PinWindow::closed);
        QVERIFY(closeSpy.isValid());

        // Simulate double click
        QPoint center = window->rect().center();
        QMouseEvent doubleClickEvent(QEvent::MouseButtonDblClick,
                                     center, window->mapToGlobal(center),
                                     Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &doubleClickEvent);

        QCOMPARE(closeSpy.count(), 1);
    }
};

QTEST_MAIN(TestPinWindowResize)
#include "tst_Resize.moc"
