#include <QtTest>
#include <QSignalSpy>
#include <QPixmap>
#include <QPainter>
#include "pinwindow/ImageTransformer.h"

/**
 * @brief Unit tests for ImageTransformer class.
 *
 * Tests image transformation operations including:
 * - Rotation (90 degree increments)
 * - Flip (horizontal and vertical)
 * - Zoom level (with clamping)
 * - Smoothing and fast scaling modes
 * - Cache behavior
 * - Signal emission
 */
class tst_ImageTransformer : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Basic state tests
    void testInitialState();
    void testSetOriginalPixmap();

    // Rotation tests
    void testRotateRight_CyclesThrough360();
    void testRotateLeft_CyclesThrough360();
    void testRotateRight_EmitsSignal();
    void testRotateLeft_EmitsSignal();
    void testRotationAngle_AfterMultipleRotations();

    // Flip tests
    void testFlipHorizontal_Toggles();
    void testFlipVertical_Toggles();
    void testFlipHorizontal_EmitsSignal();
    void testFlipVertical_EmitsSignal();
    void testFlipCombinations();

    // Zoom tests
    void testSetZoomLevel_ValidValues();
    void testSetZoomLevel_ClampsToMinimum();
    void testSetZoomLevel_ClampsToMaximum();
    void testSetZoomLevel_EmitsSignalOnChange();
    void testSetZoomLevel_NoSignalIfSameValue();
    void testSetZoomLevel_BoundaryValues();

    // Smoothing tests
    void testSetSmoothing_Toggles();
    void testSetSmoothing_EmitsSignalOnChange();
    void testSetSmoothing_NoSignalIfSameValue();

    // Fast scaling tests
    void testSetFastScaling_Toggles();
    void testSetFastScaling_NoSignal();

    // Cache tests
    void testInvalidateCache();
    void testCacheRebuildsOnTransformChange();

    // Output tests
    void testGetTransformedPixmap_NoTransform();
    void testGetTransformedPixmap_WithRotation();
    void testGetTransformedPixmap_WithFlip();
    void testTransformedLogicalSize_NoRotation();
    void testTransformedLogicalSize_90DegreeRotation();
    void testDisplaySize_WithZoom();
    void testGetDisplayPixmap();

    // Combined operations tests
    void testCombinedRotateAndFlip();
    void testMultipleTransformOperations();

private:
    ImageTransformer* m_transformer = nullptr;
    QPixmap createTestPixmap(int width, int height);
};

void tst_ImageTransformer::init()
{
    m_transformer = new ImageTransformer();
}

void tst_ImageTransformer::cleanup()
{
    delete m_transformer;
    m_transformer = nullptr;
}

QPixmap tst_ImageTransformer::createTestPixmap(int width, int height)
{
    QPixmap pixmap(width, height);
    pixmap.fill(Qt::blue);
    QPainter painter(&pixmap);
    painter.setPen(Qt::white);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, "Test");
    return pixmap;
}

// ============================================================================
// Basic state tests
// ============================================================================

void tst_ImageTransformer::testInitialState()
{
    QCOMPARE(m_transformer->rotationAngle(), 0);
    QCOMPARE(m_transformer->isFlippedHorizontal(), false);
    QCOMPARE(m_transformer->isFlippedVertical(), false);
    QCOMPARE(m_transformer->zoomLevel(), 1.0);
    QCOMPARE(m_transformer->smoothing(), true);
    QVERIFY(m_transformer->originalPixmap().isNull());
}

void tst_ImageTransformer::testSetOriginalPixmap()
{
    QPixmap testPixmap = createTestPixmap(100, 50);
    m_transformer->setOriginalPixmap(testPixmap);

    QVERIFY(!m_transformer->originalPixmap().isNull());
    QCOMPARE(m_transformer->originalPixmap().size(), QSize(100, 50));
}

// ============================================================================
// Rotation tests
// ============================================================================

void tst_ImageTransformer::testRotateRight_CyclesThrough360()
{
    QCOMPARE(m_transformer->rotationAngle(), 0);

    m_transformer->rotateRight();
    QCOMPARE(m_transformer->rotationAngle(), 90);

    m_transformer->rotateRight();
    QCOMPARE(m_transformer->rotationAngle(), 180);

    m_transformer->rotateRight();
    QCOMPARE(m_transformer->rotationAngle(), 270);

    m_transformer->rotateRight();
    QCOMPARE(m_transformer->rotationAngle(), 0);  // Cycles back to 0
}

void tst_ImageTransformer::testRotateLeft_CyclesThrough360()
{
    QCOMPARE(m_transformer->rotationAngle(), 0);

    m_transformer->rotateLeft();
    QCOMPARE(m_transformer->rotationAngle(), 270);

    m_transformer->rotateLeft();
    QCOMPARE(m_transformer->rotationAngle(), 180);

    m_transformer->rotateLeft();
    QCOMPARE(m_transformer->rotationAngle(), 90);

    m_transformer->rotateLeft();
    QCOMPARE(m_transformer->rotationAngle(), 0);  // Cycles back to 0
}

void tst_ImageTransformer::testRotateRight_EmitsSignal()
{
    QSignalSpy spy(m_transformer, &ImageTransformer::transformChanged);
    QVERIFY(spy.isValid());

    m_transformer->rotateRight();

    QCOMPARE(spy.count(), 1);
}

void tst_ImageTransformer::testRotateLeft_EmitsSignal()
{
    QSignalSpy spy(m_transformer, &ImageTransformer::transformChanged);
    QVERIFY(spy.isValid());

    m_transformer->rotateLeft();

    QCOMPARE(spy.count(), 1);
}

void tst_ImageTransformer::testRotationAngle_AfterMultipleRotations()
{
    // Rotate right 3 times, left 2 times = net 90 degrees right
    m_transformer->rotateRight();
    m_transformer->rotateRight();
    m_transformer->rotateRight();
    m_transformer->rotateLeft();
    m_transformer->rotateLeft();

    QCOMPARE(m_transformer->rotationAngle(), 90);
}

// ============================================================================
// Flip tests
// ============================================================================

void tst_ImageTransformer::testFlipHorizontal_Toggles()
{
    QCOMPARE(m_transformer->isFlippedHorizontal(), false);

    m_transformer->flipHorizontal();
    QCOMPARE(m_transformer->isFlippedHorizontal(), true);

    m_transformer->flipHorizontal();
    QCOMPARE(m_transformer->isFlippedHorizontal(), false);
}

void tst_ImageTransformer::testFlipVertical_Toggles()
{
    QCOMPARE(m_transformer->isFlippedVertical(), false);

    m_transformer->flipVertical();
    QCOMPARE(m_transformer->isFlippedVertical(), true);

    m_transformer->flipVertical();
    QCOMPARE(m_transformer->isFlippedVertical(), false);
}

void tst_ImageTransformer::testFlipHorizontal_EmitsSignal()
{
    QSignalSpy spy(m_transformer, &ImageTransformer::transformChanged);
    QVERIFY(spy.isValid());

    m_transformer->flipHorizontal();

    QCOMPARE(spy.count(), 1);
}

void tst_ImageTransformer::testFlipVertical_EmitsSignal()
{
    QSignalSpy spy(m_transformer, &ImageTransformer::transformChanged);
    QVERIFY(spy.isValid());

    m_transformer->flipVertical();

    QCOMPARE(spy.count(), 1);
}

void tst_ImageTransformer::testFlipCombinations()
{
    // Both flips can be true at the same time
    m_transformer->flipHorizontal();
    m_transformer->flipVertical();

    QCOMPARE(m_transformer->isFlippedHorizontal(), true);
    QCOMPARE(m_transformer->isFlippedVertical(), true);

    // Toggle one off
    m_transformer->flipHorizontal();
    QCOMPARE(m_transformer->isFlippedHorizontal(), false);
    QCOMPARE(m_transformer->isFlippedVertical(), true);
}

// ============================================================================
// Zoom tests
// ============================================================================

void tst_ImageTransformer::testSetZoomLevel_ValidValues()
{
    m_transformer->setZoomLevel(2.0);
    QCOMPARE(m_transformer->zoomLevel(), 2.0);

    m_transformer->setZoomLevel(0.5);
    QCOMPARE(m_transformer->zoomLevel(), 0.5);

    m_transformer->setZoomLevel(1.0);
    QCOMPARE(m_transformer->zoomLevel(), 1.0);
}

void tst_ImageTransformer::testSetZoomLevel_ClampsToMinimum()
{
    m_transformer->setZoomLevel(0.05);  // Below minimum 0.1
    QCOMPARE(m_transformer->zoomLevel(), 0.1);

    m_transformer->setZoomLevel(0.0);
    QCOMPARE(m_transformer->zoomLevel(), 0.1);

    m_transformer->setZoomLevel(-1.0);
    QCOMPARE(m_transformer->zoomLevel(), 0.1);
}

void tst_ImageTransformer::testSetZoomLevel_ClampsToMaximum()
{
    m_transformer->setZoomLevel(10.0);  // Above maximum 5.0
    QCOMPARE(m_transformer->zoomLevel(), 5.0);

    m_transformer->setZoomLevel(100.0);
    QCOMPARE(m_transformer->zoomLevel(), 5.0);
}

void tst_ImageTransformer::testSetZoomLevel_EmitsSignalOnChange()
{
    QSignalSpy spy(m_transformer, &ImageTransformer::transformChanged);
    QVERIFY(spy.isValid());

    m_transformer->setZoomLevel(2.0);

    QCOMPARE(spy.count(), 1);
}

void tst_ImageTransformer::testSetZoomLevel_NoSignalIfSameValue()
{
    m_transformer->setZoomLevel(2.0);

    QSignalSpy spy(m_transformer, &ImageTransformer::transformChanged);
    QVERIFY(spy.isValid());

    m_transformer->setZoomLevel(2.0);  // Same value

    QCOMPARE(spy.count(), 0);
}

void tst_ImageTransformer::testSetZoomLevel_BoundaryValues()
{
    // Exactly at minimum
    m_transformer->setZoomLevel(0.1);
    QCOMPARE(m_transformer->zoomLevel(), 0.1);

    // Exactly at maximum
    m_transformer->setZoomLevel(5.0);
    QCOMPARE(m_transformer->zoomLevel(), 5.0);
}

// ============================================================================
// Smoothing tests
// ============================================================================

void tst_ImageTransformer::testSetSmoothing_Toggles()
{
    QCOMPARE(m_transformer->smoothing(), true);

    m_transformer->setSmoothing(false);
    QCOMPARE(m_transformer->smoothing(), false);

    m_transformer->setSmoothing(true);
    QCOMPARE(m_transformer->smoothing(), true);
}

void tst_ImageTransformer::testSetSmoothing_EmitsSignalOnChange()
{
    QSignalSpy spy(m_transformer, &ImageTransformer::transformChanged);
    QVERIFY(spy.isValid());

    m_transformer->setSmoothing(false);

    QCOMPARE(spy.count(), 1);
}

void tst_ImageTransformer::testSetSmoothing_NoSignalIfSameValue()
{
    QCOMPARE(m_transformer->smoothing(), true);

    QSignalSpy spy(m_transformer, &ImageTransformer::transformChanged);
    QVERIFY(spy.isValid());

    m_transformer->setSmoothing(true);  // Same value

    QCOMPARE(spy.count(), 0);
}

// ============================================================================
// Fast scaling tests
// ============================================================================

void tst_ImageTransformer::testSetFastScaling_Toggles()
{
    // Fast scaling is off by default (not directly accessible, but setFastScaling works)
    m_transformer->setFastScaling(true);
    m_transformer->setFastScaling(false);
    // No crash = success (fast scaling is internal state)
}

void tst_ImageTransformer::testSetFastScaling_NoSignal()
{
    QSignalSpy spy(m_transformer, &ImageTransformer::transformChanged);
    QVERIFY(spy.isValid());

    m_transformer->setFastScaling(true);

    // setFastScaling does NOT emit transformChanged (it's for performance)
    QCOMPARE(spy.count(), 0);
}

// ============================================================================
// Cache tests
// ============================================================================

void tst_ImageTransformer::testInvalidateCache()
{
    QPixmap testPixmap = createTestPixmap(100, 50);
    m_transformer->setOriginalPixmap(testPixmap);

    // Get display to build cache
    m_transformer->getDisplayPixmap();

    // Invalidate should not crash
    m_transformer->invalidateCache();

    // After invalidate, getDisplayPixmap should still work
    QPixmap result = m_transformer->getDisplayPixmap();
    QVERIFY(!result.isNull());
}

void tst_ImageTransformer::testCacheRebuildsOnTransformChange()
{
    QPixmap testPixmap = createTestPixmap(100, 50);
    m_transformer->setOriginalPixmap(testPixmap);

    // Get display pixmap (builds cache)
    QPixmap display1 = m_transformer->getDisplayPixmap();
    QVERIFY(!display1.isNull());

    // Change transform
    m_transformer->rotateRight();

    // Get display pixmap again (should rebuild cache)
    QPixmap display2 = m_transformer->getDisplayPixmap();
    QVERIFY(!display2.isNull());

    // After 90 degree rotation, width and height should swap
    // (accounting for device pixel ratio)
    QSize size1 = display1.size() / display1.devicePixelRatio();
    QSize size2 = display2.size() / display2.devicePixelRatio();
    QCOMPARE(size2.width(), size1.height());
    QCOMPARE(size2.height(), size1.width());
}

// ============================================================================
// Output tests
// ============================================================================

void tst_ImageTransformer::testGetTransformedPixmap_NoTransform()
{
    QPixmap testPixmap = createTestPixmap(100, 50);
    m_transformer->setOriginalPixmap(testPixmap);

    QPixmap result = m_transformer->getTransformedPixmap();

    // Should return original when no transform applied
    QCOMPARE(result.size(), testPixmap.size());
}

void tst_ImageTransformer::testGetTransformedPixmap_WithRotation()
{
    QPixmap testPixmap = createTestPixmap(100, 50);
    m_transformer->setOriginalPixmap(testPixmap);
    m_transformer->rotateRight();

    QPixmap result = m_transformer->getTransformedPixmap();

    // After 90 degree rotation, dimensions should swap
    QCOMPARE(result.width(), testPixmap.height());
    QCOMPARE(result.height(), testPixmap.width());
}

void tst_ImageTransformer::testGetTransformedPixmap_WithFlip()
{
    QPixmap testPixmap = createTestPixmap(100, 50);
    m_transformer->setOriginalPixmap(testPixmap);
    m_transformer->flipHorizontal();

    QPixmap result = m_transformer->getTransformedPixmap();

    // Flip doesn't change dimensions
    QCOMPARE(result.size(), testPixmap.size());
}

void tst_ImageTransformer::testTransformedLogicalSize_NoRotation()
{
    QPixmap testPixmap = createTestPixmap(100, 50);
    m_transformer->setOriginalPixmap(testPixmap);

    QSize logicalSize = m_transformer->transformedLogicalSize();

    QCOMPARE(logicalSize.width(), 100);
    QCOMPARE(logicalSize.height(), 50);
}

void tst_ImageTransformer::testTransformedLogicalSize_90DegreeRotation()
{
    QPixmap testPixmap = createTestPixmap(100, 50);
    m_transformer->setOriginalPixmap(testPixmap);
    m_transformer->rotateRight();

    QSize logicalSize = m_transformer->transformedLogicalSize();

    // Width and height should swap after 90 degree rotation
    QCOMPARE(logicalSize.width(), 50);
    QCOMPARE(logicalSize.height(), 100);
}

void tst_ImageTransformer::testDisplaySize_WithZoom()
{
    QPixmap testPixmap = createTestPixmap(100, 50);
    m_transformer->setOriginalPixmap(testPixmap);
    m_transformer->setZoomLevel(2.0);

    QSize displaySize = m_transformer->displaySize();

    QCOMPARE(displaySize.width(), 200);
    QCOMPARE(displaySize.height(), 100);
}

void tst_ImageTransformer::testGetDisplayPixmap()
{
    QPixmap testPixmap = createTestPixmap(100, 50);
    m_transformer->setOriginalPixmap(testPixmap);
    m_transformer->setZoomLevel(2.0);

    QPixmap displayPixmap = m_transformer->getDisplayPixmap();

    QVERIFY(!displayPixmap.isNull());
    // The display pixmap should be scaled
    QSize logicalSize = displayPixmap.size() / displayPixmap.devicePixelRatio();
    QCOMPARE(logicalSize.width(), 200);
    QCOMPARE(logicalSize.height(), 100);
}

// ============================================================================
// Combined operations tests
// ============================================================================

void tst_ImageTransformer::testCombinedRotateAndFlip()
{
    QPixmap testPixmap = createTestPixmap(100, 50);
    m_transformer->setOriginalPixmap(testPixmap);

    m_transformer->rotateRight();
    m_transformer->flipHorizontal();

    QCOMPARE(m_transformer->rotationAngle(), 90);
    QCOMPARE(m_transformer->isFlippedHorizontal(), true);
    QCOMPARE(m_transformer->isFlippedVertical(), false);

    QPixmap result = m_transformer->getTransformedPixmap();
    QVERIFY(!result.isNull());

    // After 90 degree rotation, dimensions swap
    QCOMPARE(result.width(), testPixmap.height());
    QCOMPARE(result.height(), testPixmap.width());
}

void tst_ImageTransformer::testMultipleTransformOperations()
{
    QSignalSpy spy(m_transformer, &ImageTransformer::transformChanged);
    QVERIFY(spy.isValid());

    QPixmap testPixmap = createTestPixmap(100, 50);
    m_transformer->setOriginalPixmap(testPixmap);

    m_transformer->rotateRight();       // Signal 1
    m_transformer->rotateRight();       // Signal 2
    m_transformer->flipHorizontal();    // Signal 3
    m_transformer->flipVertical();      // Signal 4
    m_transformer->setZoomLevel(2.0);   // Signal 5
    m_transformer->setSmoothing(false); // Signal 6

    QCOMPARE(spy.count(), 6);
    QCOMPARE(m_transformer->rotationAngle(), 180);
    QCOMPARE(m_transformer->isFlippedHorizontal(), true);
    QCOMPARE(m_transformer->isFlippedVertical(), true);
    QCOMPARE(m_transformer->zoomLevel(), 2.0);
    QCOMPARE(m_transformer->smoothing(), false);
}

QTEST_MAIN(tst_ImageTransformer)
#include "tst_ImageTransformer.moc"
