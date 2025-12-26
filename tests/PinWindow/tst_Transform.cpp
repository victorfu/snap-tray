#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QPixmap>
#include <QPainter>

#include "PinWindow.h"

class TestPinWindowTransform : public QObject
{
    Q_OBJECT

private:
    QPixmap createTestPixmap(int width, int height, const QColor& color = Qt::red) {
        QPixmap pixmap(width, height);
        pixmap.fill(color);
        // Draw a marker in top-left corner to verify transformations
        QPainter painter(&pixmap);
        painter.fillRect(0, 0, 10, 10, Qt::blue);
        return pixmap;
    }

private slots:
    void initTestCase() {
        // Ensure we have a QApplication for GUI tests
    }

    // =========================================================================
    // Initial State Tests
    // =========================================================================

    void testInitialZoomLevel() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QCOMPARE(window.zoomLevel(), 1.0);
    }

    void testInitialOpacity() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QCOMPARE(window.opacity(), 1.0);
    }

    // =========================================================================
    // Zoom Tests
    // =========================================================================

    void testSetZoomLevel() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        window.setZoomLevel(2.0);
        QCOMPARE(window.zoomLevel(), 2.0);

        window.setZoomLevel(0.5);
        QCOMPARE(window.zoomLevel(), 0.5);
    }

    void testZoomLevelClamping() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        // Test lower bound (0.1)
        window.setZoomLevel(0.05);
        QCOMPARE(window.zoomLevel(), 0.1);

        // Test upper bound (5.0)
        window.setZoomLevel(10.0);
        QCOMPARE(window.zoomLevel(), 5.0);
    }

    void testZoomLevelBoundaryValues() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        // Exactly at boundaries
        window.setZoomLevel(0.1);
        QCOMPARE(window.zoomLevel(), 0.1);

        window.setZoomLevel(5.0);
        QCOMPARE(window.zoomLevel(), 5.0);
    }

    // =========================================================================
    // Opacity Tests
    // =========================================================================

    void testSetOpacity() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        window.setOpacity(0.5);
        QCOMPARE(window.opacity(), 0.5);

        window.setOpacity(0.8);
        QCOMPARE(window.opacity(), 0.8);
    }

    void testOpacityClamping() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        // Test lower bound (0.1)
        window.setOpacity(0.0);
        QCOMPARE(window.opacity(), 0.1);

        // Test upper bound (1.0)
        window.setOpacity(1.5);
        QCOMPARE(window.opacity(), 1.0);
    }

    void testOpacityBoundaryValues() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        window.setOpacity(0.1);
        QCOMPARE(window.opacity(), 0.1);

        window.setOpacity(1.0);
        QCOMPARE(window.opacity(), 1.0);
    }

    // =========================================================================
    // Rotation Tests
    // =========================================================================

    void testRotateRight() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        // One rotation = 90 degrees
        window.rotateRight();
        // Window size should change for non-square images
        // For square pixmap, just verify it doesn't crash

        // Four rotations = back to original
        window.rotateRight();
        window.rotateRight();
        window.rotateRight();
        // Should be back at 0 degrees (360 % 360 = 0)
    }

    void testRotateLeft() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        window.rotateLeft();
        // One left rotation = 270 degrees (same as -90)

        // Four left rotations = back to original
        window.rotateLeft();
        window.rotateLeft();
        window.rotateLeft();
    }

    void testRotateRightThenLeft() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QSize originalSize = window.size();

        window.rotateRight();
        window.rotateLeft();

        // Should return to original size
        QCOMPARE(window.size(), originalSize);
    }

    void testRotateNonSquarePixmap() {
        // Non-square pixmap: width != height
        QPixmap pixmap = createTestPixmap(200, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QSize originalSize = window.size();

        window.rotateRight();
        QSize rotatedSize = window.size();

        // Width and height should swap (accounting for shadow margin)
        // Original: 200 + margins x 100 + margins
        // Rotated: 100 + margins x 200 + margins
        QVERIFY(rotatedSize.width() != originalSize.width() ||
                rotatedSize.height() != originalSize.height());
    }

    // =========================================================================
    // Flip Tests
    // =========================================================================

    void testFlipHorizontal() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QSize originalSize = window.size();

        window.flipHorizontal();
        QCOMPARE(window.size(), originalSize);

        // Double flip = original
        window.flipHorizontal();
        QCOMPARE(window.size(), originalSize);
    }

    void testFlipVertical() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QSize originalSize = window.size();

        window.flipVertical();
        QCOMPARE(window.size(), originalSize);

        // Double flip = original
        window.flipVertical();
        QCOMPARE(window.size(), originalSize);
    }

    void testCombinedFlips() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QSize originalSize = window.size();

        window.flipHorizontal();
        window.flipVertical();

        // Size should remain the same
        QCOMPARE(window.size(), originalSize);

        // Flip both back
        window.flipHorizontal();
        window.flipVertical();
        QCOMPARE(window.size(), originalSize);
    }

    // =========================================================================
    // Combined Transform Tests
    // =========================================================================

    void testRotateAndFlip() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        window.rotateRight();
        window.flipHorizontal();
        window.rotateLeft();
        window.flipVertical();

        // Should not crash, transforms should compose correctly
    }

    void testRotateAndZoom() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        window.setZoomLevel(2.0);
        window.rotateRight();

        QCOMPARE(window.zoomLevel(), 2.0);
    }

    void testTransformSequence() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        // Complex sequence of transforms
        window.setZoomLevel(1.5);
        window.rotateRight();
        window.flipHorizontal();
        window.setOpacity(0.7);
        window.rotateRight();
        window.flipVertical();
        window.setZoomLevel(0.8);

        // Verify state
        QCOMPARE(window.zoomLevel(), 0.8);
        QCOMPARE(window.opacity(), 0.7);
    }

    // =========================================================================
    // Watermark Tests
    // =========================================================================

    void testWatermarkSettings() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        WatermarkRenderer::Settings settings;
        settings.enabled = true;
        settings.imagePath = "/test/watermark.png";
        settings.position = WatermarkRenderer::TopRight;
        settings.opacity = 0.7;

        window.setWatermarkSettings(settings);

        WatermarkRenderer::Settings retrieved = window.watermarkSettings();
        QCOMPARE(retrieved.enabled, true);
        QCOMPARE(retrieved.imagePath, QString("/test/watermark.png"));
        QCOMPARE(retrieved.position, WatermarkRenderer::TopRight);
        QVERIFY(qFuzzyCompare(retrieved.opacity, 0.7));
    }

    // =========================================================================
    // Click-Through Tests
    // =========================================================================

    void testInitialClickThrough() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QCOMPARE(window.isClickThrough(), false);
    }

    void testSetClickThrough() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        window.setClickThrough(true);
        QCOMPARE(window.isClickThrough(), true);

        window.setClickThrough(false);
        QCOMPARE(window.isClickThrough(), false);
    }

    // =========================================================================
    // Signal Tests
    // =========================================================================

    void testClosedSignal() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow *window = new PinWindow(pixmap, QPoint(0, 0));

        QSignalSpy spy(window, &PinWindow::closed);
        QVERIFY(spy.isValid());

        window->close();

        QCOMPARE(spy.count(), 1);
        QList<QVariant> arguments = spy.takeFirst();
        QCOMPARE(arguments.at(0).value<PinWindow*>(), window);
    }
};

QTEST_MAIN(TestPinWindowTransform)
#include "tst_Transform.moc"
