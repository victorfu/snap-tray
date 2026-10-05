#include <QtTest/QtTest>
#include <QGuiApplication>
#include <QPixmap>
#include <QPainter>
#include <QMouseEvent>

#include "PinWindow.h"
#include "PinWindowManager.h"
#include "pinwindow/PinWindowPlacement.h"
#include "platform/WindowDragPolicy.h"

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

    static void sendDragEvent(PinWindow& window, QEvent::Type type, const QPoint& globalPos)
    {
        const auto button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
        const auto buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
        QMouseEvent event(type, window.mapFromGlobal(globalPos), globalPos,
                          button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &event);
    }

private slots:
    void testLongScreenshotFitsBeforeFirstShow() {
        class ShowObserver : public QObject {
        public:
            QSize firstSize;
            bool eventFilter(QObject* object, QEvent* event) override {
                if (event->type() == QEvent::Show && firstSize.isEmpty()) {
                    if (auto* pin = qobject_cast<PinWindow*>(object)) firstSize = pin->size();
                }
                return false;
            }
        } observer;
        qApp->installEventFilter(&observer);
        const QPixmap pixmap = createTestPixmap(800, 30000);
        const QRect screen(0, 0, 1920, 1080);
        const auto placement = computeInitialPinWindowPlacement(pixmap, screen);
        QVERIFY(placement.zoomLevel < 0.1);
        QVERIFY(screen.contains(QRect(placement.position, placement.displaySize)));

        PinWindowManager manager;
        auto* pin = manager.createPinWindow(pixmap, placement.position, true, placement.zoomLevel);
        QCOMPARE(observer.firstSize, placement.displaySize);
        QCOMPARE(pin->size(), placement.displaySize);
        QVERIFY(pin->isVisible());
        // Keep the original pixels available when the user zooms in again.
        pin->setZoomLevel(placement.zoomLevel * 2);
        QCOMPARE(pin->height(), qRound(30000 * placement.zoomLevel * 2));
    }

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

    void testDragBurstUsesLatestPosition() {
        PinWindow window(createTestPixmap(240, 160), QPoint(100, 100));
        QCoreApplication::processEvents();
        const QPoint initial = window.pos();
        const QPoint press = window.mapToGlobal(QPoint(100, 80));
        sendDragEvent(window, QEvent::MouseButtonPress, press);

        for (const QPoint& delta : {QPoint(10, 20), QPoint(40, 10), QPoint(60, 30)}) {
            sendDragEvent(window, QEvent::MouseMove, press + delta);
            if (SnapTray::windowDragUpdateIntervalMs() > 0) {
                // No event-loop tick yet: a burst must not issue geometry changes.
                QCOMPARE(window.pos(), initial);
            } else {
                QCOMPARE(window.pos(), initial + delta);
            }
        }
        QTRY_COMPARE(window.pos(), initial + QPoint(60, 30));
        sendDragEvent(window, QEvent::MouseButtonRelease, press + QPoint(60, 30));
    }

    void testPacedDragReleaseFlushesFinalPosition() {
        const int interval = SnapTray::windowDragUpdateIntervalMs();
        if (interval == 0) QSKIP("This platform uses immediate window dragging.");
        PinWindow window(createTestPixmap(240, 160), QPoint(100, 100));
        QCoreApplication::processEvents();
        const QPoint initial = window.pos();
        const QPoint press = window.mapToGlobal(QPoint(100, 80));
        sendDragEvent(window, QEvent::MouseButtonPress, press);
        sendDragEvent(window, QEvent::MouseMove, press + QPoint(30, 20));
        QCOMPARE(window.pos(), initial);

        // Release before the tick, at a newer position than the queued move.
        sendDragEvent(window, QEvent::MouseButtonRelease, press + QPoint(50, 40));
        const QPoint finalPosition = initial + QPoint(50, 40);
        QCOMPARE(window.pos(), finalPosition);
        QTest::qWait(interval * 3);
        QCOMPARE(window.pos(), finalPosition);

        // A new press must not replay the previous drag's queued position.
        const QPoint nextPress = window.mapToGlobal(QPoint(100, 80));
        sendDragEvent(window, QEvent::MouseButtonPress, nextPress);
        QTest::qWait(interval * 3);
        QCOMPARE(window.pos(), finalPosition);
        sendDragEvent(window, QEvent::MouseButtonRelease, nextPress);
    }

    void testHiddenPinDiscardsPendingDrag() {
        const int interval = SnapTray::windowDragUpdateIntervalMs();
        if (interval == 0) QSKIP("This platform uses immediate window dragging.");
        PinWindow window(createTestPixmap(240, 160), QPoint(100, 100));
        QCoreApplication::processEvents();
        const QPoint initial = window.pos();
        const QPoint press = window.mapToGlobal(QPoint(100, 80));
        sendDragEvent(window, QEvent::MouseButtonPress, press);
        sendDragEvent(window, QEvent::MouseMove, press + QPoint(30, 20));
        window.hide();
        QTest::qWait(interval * 3);
        QCOMPARE(window.pos(), initial);
        window.show();
        QTest::qWait(interval * 3);
        QCOMPARE(window.pos(), initial);
        sendDragEvent(window, QEvent::MouseMove, press + QPoint(50, 40));
        QTest::qWait(interval * 3);
        QCOMPARE(window.pos(), initial);
    }

    void testResizeRemainsImmediate() {
        PinWindow window(createTestPixmap(240, 160), QPoint(100, 100));
        QCoreApplication::processEvents();
        const QPoint press = window.mapToGlobal(QPoint(window.width() - 1, 80));
        sendDragEvent(window, QEvent::MouseButtonPress, press);
        sendDragEvent(window, QEvent::MouseMove, press + QPoint(40, 0));
        QVERIFY(window.width() > 240);
        sendDragEvent(window, QEvent::MouseButtonRelease, press + QPoint(40, 0));
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

    void testPinWindowAnimationPolicyIsPlatformScoped() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0), nullptr, false, false);

#ifdef Q_OS_LINUX
        QVERIFY(window.windowFlags().testFlag(Qt::X11BypassWindowManagerHint));
#else
        QVERIFY(!window.windowFlags().testFlag(Qt::X11BypassWindowManagerHint));
#endif
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

    void testToolbarStartsHidden() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QCoreApplication::processEvents();

        QVERIFY(!window.isToolbarVisible());
    }

    void testDeferredShowWaitsUntilPrepared() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(12, 34), nullptr, false, false);

        QVERIFY(!window.isVisible());

        window.showPreparedWindow();
        QCoreApplication::processEvents();

        QVERIFY(window.isVisible());
        QCOMPARE(window.pos(), QPoint(12, 34));
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
