#include <QtTest/QtTest>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>

#define private public
#include "PinWindow.h"
#undef private

#include "annotations/AnnotationLayer.h"
#include "annotations/ErasedItemsGroup.h"
#include "annotations/MarkerStroke.h"
#include "annotations/MosaicRectAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "pinwindow/RegionLayoutManager.h"

namespace {

QPixmap createPatternPixmap(int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            image.setPixelColor(x, y, QColor(x % 256, y % 256, (x + y) % 256, 255));
        }
    }
    return QPixmap::fromImage(image);
}

bool pixmapsEqual(const QPixmap& lhs, const QPixmap& rhs)
{
    if (lhs.isNull() != rhs.isNull()) {
        return false;
    }
    if (lhs.size() != rhs.size()) {
        return false;
    }
    if (!qFuzzyCompare(lhs.devicePixelRatio(), rhs.devicePixelRatio())) {
        return false;
    }
    return lhs.toImage() == rhs.toImage();
}

bool regionHasNonWhitePixel(const QImage& image, const QRect& region)
{
    const QRect clipped = region.intersected(image.rect());
    if (clipped.isEmpty()) {
        return false;
    }

    for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
        for (int x = clipped.left(); x <= clipped.right(); ++x) {
            const QColor color = image.pixelColor(x, y);
            if (color.red() != 255 || color.green() != 255 || color.blue() != 255 || color.alpha() != 255) {
                return true;
            }
        }
    }

    return false;
}

}  // namespace

class TestPinWindowCropUndo : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testApplyCrop_RotatedState_CropsExpectedOriginalRegion();
    void testApplyCrop_ZoomedState_CropsExpectedSourceRegion();
    void testApplyCrop_ClearsMultiRegionMetadataAndUndoRestoresIt();
    void testUndoCrop_ZoomedState_RestoresAnnotationPosition();
    void testUndoCrop_RestoresPixmapAndTransformState();
    void testUndoCrop_RestoresAnnotationPosition();
    void testApplyCrop_MarkerStroke_RemainsAlignedAfterTranslation();
    void testUndoCrop_MarkerStroke_RestoresOriginalPosition();
    void testApplyCrop_RefreshesMosaicSourceInsideErasedGroup();
    void testApplyCrop_EdgeEndpointCoordinate_ClampsToLastPixelColumn();
    void testHandleToolbarUndo_PrioritizesCropWhenNoPostCropAnnotations();
    void testHandleToolbarUndo_PrioritizesPostCropAnnotationsFirst();
    void testHandleToolbarUndo_UsesStableCropBoundaryAfterAnnotationTrim();
    void testHandleToolbarRedo_ReappliesCropAfterUndo();
    void testTransformChange_ClearsCropHistory();
    void testHandleToolbarRedo_PrioritizesCropBeforePostCropAnnotationRedo();
    void testHandleToolbarRedo_UsesStableCropBoundaryAfterAnnotationTrim();
    void testHandleToolbarUndo_InPlaceCacheInvalidationDoesNotPreemptCropUndo();
};

void TestPinWindowCropUndo::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for PinWindow tests in this environment.");
    }
}

void TestPinWindowCropUndo::testApplyCrop_RotatedState_CropsExpectedOriginalRegion()
{
    QPixmap source = createPatternPixmap(140, 90);
    PinWindow window(source, QPoint(0, 0), nullptr, false);

    window.rotateRight();
    QCOMPARE(window.m_rotationAngle, 90);

    const QRect cropRect(15, 18, 50, 30);
    const QPixmap expected = source.copy(cropRect);

    window.applyCrop(cropRect);

    QVERIFY(pixmapsEqual(window.m_originalPixmap, expected));
    QCOMPARE(window.m_rotationAngle, 90);
    QCOMPARE(window.m_flipHorizontal, false);
    QCOMPARE(window.m_flipVertical, false);
}

void TestPinWindowCropUndo::testApplyCrop_ZoomedState_CropsExpectedSourceRegion()
{
    QPixmap source = createPatternPixmap(180, 120);
    PinWindow window(source, QPoint(0, 0), nullptr, false);

    window.setZoomLevel(2.0);
    window.toggleToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);

    QVector<QPoint> points = {QPoint(100, 70), QPoint(130, 90)};
    window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
        points, QColor(Qt::red), 3, LineEndStyle::None, LineStyle::Solid));

    const QRect toolCropRect(41, 21, 80, 60);  // tool-space (zoomed) coordinates
    const QPixmap expected = source.copy(QRect(20, 10, 41, 31));

    window.applyCrop(toolCropRect);

    QVERIFY(pixmapsEqual(window.m_originalPixmap, expected));

    auto* polyline = dynamic_cast<PolylineAnnotation*>(window.m_annotationLayer->itemAt(0));
    QVERIFY(polyline != nullptr);
    const QVector<QPoint> shiftedPoints = polyline->points();
    QCOMPARE(shiftedPoints.size(), points.size());
    QCOMPARE(shiftedPoints[0], points[0] - QPoint(40, 20));
    QCOMPARE(shiftedPoints[1], points[1] - QPoint(40, 20));
}

void TestPinWindowCropUndo::testApplyCrop_ClearsMultiRegionMetadataAndUndoRestoresIt()
{
    QPixmap source = createPatternPixmap(200, 120);
    PinWindow window(source, QPoint(0, 0), nullptr, false);

    QVector<LayoutRegion> regions;
    LayoutRegion leftRegion;
    leftRegion.rect = QRect(0, 0, 100, 120);
    leftRegion.originalRect = leftRegion.rect;
    leftRegion.image = source.toImage().copy(leftRegion.rect);
    leftRegion.color = QColor("#3daee9");
    leftRegion.index = 0;
    regions.append(leftRegion);

    LayoutRegion rightRegion;
    rightRegion.rect = QRect(100, 0, 100, 120);
    rightRegion.originalRect = rightRegion.rect;
    rightRegion.image = source.toImage().copy(rightRegion.rect);
    rightRegion.color = QColor("#2ecc71");
    rightRegion.index = 1;
    regions.append(rightRegion);

    window.setMultiRegionData(regions);
    QVERIFY(window.m_hasMultiRegionData);
    QCOMPARE(window.m_storedRegions.size(), regions.size());

    window.applyCrop(QRect(30, 20, 100, 70));

    QVERIFY(!window.m_hasMultiRegionData);
    QVERIFY(window.m_storedRegions.isEmpty());

    window.undoCrop();

    QVERIFY(window.m_hasMultiRegionData);
    QCOMPARE(window.m_storedRegions.size(), regions.size());
    QCOMPARE(window.m_storedRegions[0].rect, regions[0].rect);
    QCOMPARE(window.m_storedRegions[1].rect, regions[1].rect);
    QCOMPARE(window.m_storedRegions[0].originalRect, regions[0].originalRect);
    QCOMPARE(window.m_storedRegions[1].originalRect, regions[1].originalRect);
    QCOMPARE(window.m_storedRegions[0].index, regions[0].index);
    QCOMPARE(window.m_storedRegions[1].index, regions[1].index);
}

void TestPinWindowCropUndo::testUndoCrop_ZoomedState_RestoresAnnotationPosition()
{
    QPixmap source = createPatternPixmap(180, 120);
    PinWindow window(source, QPoint(0, 0), nullptr, false);

    window.setZoomLevel(2.0);
    window.toggleToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);

    QVector<QPoint> points = {QPoint(100, 70), QPoint(130, 90)};
    window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
        points, QColor(Qt::red), 3, LineEndStyle::None, LineStyle::Solid));

    const QRect toolCropRect(41, 21, 80, 60);
    window.applyCrop(toolCropRect);

    auto* polyline = dynamic_cast<PolylineAnnotation*>(window.m_annotationLayer->itemAt(0));
    QVERIFY(polyline != nullptr);
    const QVector<QPoint> croppedPoints = polyline->points();
    QCOMPARE(croppedPoints[0], points[0] - QPoint(40, 20));
    QCOMPARE(croppedPoints[1], points[1] - QPoint(40, 20));

    window.undoCrop();

    polyline = dynamic_cast<PolylineAnnotation*>(window.m_annotationLayer->itemAt(0));
    QVERIFY(polyline != nullptr);
    QCOMPARE(polyline->points(), points);
}

void TestPinWindowCropUndo::testUndoCrop_RestoresPixmapAndTransformState()
{
    QPixmap source = createPatternPixmap(160, 120);
    PinWindow window(source, QPoint(0, 0), nullptr, false);

    window.rotateRight();
    window.flipHorizontal();
    window.flipVertical();

    const QPixmap originalPixmap = window.m_originalPixmap;
    const int originalRotation = window.m_rotationAngle;
    const bool originalFlipH = window.m_flipHorizontal;
    const bool originalFlipV = window.m_flipVertical;

    window.applyCrop(QRect(25, 20, 70, 40));
    QVERIFY(!window.m_cropUndoStack.isEmpty());

    window.undoCrop();

    QVERIFY(pixmapsEqual(window.m_originalPixmap, originalPixmap));
    QCOMPARE(window.m_rotationAngle, originalRotation);
    QCOMPARE(window.m_flipHorizontal, originalFlipH);
    QCOMPARE(window.m_flipVertical, originalFlipV);
    QVERIFY(window.m_cropUndoStack.isEmpty());
}

void TestPinWindowCropUndo::testUndoCrop_RestoresAnnotationPosition()
{
    QPixmap source(220, 140);
    source.fill(Qt::white);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.toggleToolbar();

    QVERIFY(window.m_annotationLayer != nullptr);

    QVector<QPoint> points = {QPoint(50, 40), QPoint(110, 60)};
    window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
        points, QColor(Qt::red), 3, LineEndStyle::None, LineStyle::Solid));

    auto* polyline = dynamic_cast<PolylineAnnotation*>(window.m_annotationLayer->itemAt(0));
    QVERIFY(polyline != nullptr);
    const QVector<QPoint> originalPoints = polyline->points();

    const QRect cropRect(20, 10, 150, 100);
    window.applyCrop(cropRect);

    polyline = dynamic_cast<PolylineAnnotation*>(window.m_annotationLayer->itemAt(0));
    QVERIFY(polyline != nullptr);
    const QVector<QPoint> croppedPoints = polyline->points();
    QCOMPARE(croppedPoints[0], originalPoints[0] - QPoint(20, 10));
    QCOMPARE(croppedPoints[1], originalPoints[1] - QPoint(20, 10));

    window.undoCrop();

    polyline = dynamic_cast<PolylineAnnotation*>(window.m_annotationLayer->itemAt(0));
    QVERIFY(polyline != nullptr);
    const QVector<QPoint> restoredPoints = polyline->points();
    QCOMPARE(restoredPoints, originalPoints);
}

void TestPinWindowCropUndo::testApplyCrop_MarkerStroke_RemainsAlignedAfterTranslation()
{
    QPixmap source = createPatternPixmap(260, 180);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.toggleToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);

    const QVector<QPointF> markerPoints = {
        QPointF(70, 55),
        QPointF(110, 75),
        QPointF(150, 85),
        QPointF(185, 95)
    };
    window.m_annotationLayer->addItem(std::make_unique<MarkerStroke>(markerPoints, QColor(Qt::red), 20));

    auto* marker = dynamic_cast<MarkerStroke*>(window.m_annotationLayer->itemAt(0));
    QVERIFY(marker != nullptr);
    const QRect originalRect = marker->boundingRect();

    // Warm marker cache before crop to reproduce stale-cache behavior.
    QImage warmup(260, 180, QImage::Format_ARGB32);
    warmup.fill(Qt::white);
    QPainter warmPainter(&warmup);
    window.m_annotationLayer->draw(warmPainter);

    const QRect cropRect(60, 40, 140, 100);
    window.applyCrop(cropRect);

    marker = dynamic_cast<MarkerStroke*>(window.m_annotationLayer->itemAt(0));
    QVERIFY(marker != nullptr);

    const QRect expectedRect = originalRect.translated(-cropRect.topLeft());
    QCOMPARE(marker->boundingRect(), expectedRect);

    QImage translated(140, 100, QImage::Format_ARGB32);
    translated.fill(Qt::white);
    QPainter translatedPainter(&translated);
    window.m_annotationLayer->draw(translatedPainter);

    QVERIFY(regionHasNonWhitePixel(translated, expectedRect.adjusted(-2, -2, 2, 2)));
}

void TestPinWindowCropUndo::testUndoCrop_MarkerStroke_RestoresOriginalPosition()
{
    QPixmap source = createPatternPixmap(260, 180);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.toggleToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);

    const QVector<QPointF> markerPoints = {
        QPointF(70, 55),
        QPointF(110, 75),
        QPointF(150, 85),
        QPointF(185, 95)
    };
    window.m_annotationLayer->addItem(std::make_unique<MarkerStroke>(markerPoints, QColor(Qt::red), 20));

    auto* marker = dynamic_cast<MarkerStroke*>(window.m_annotationLayer->itemAt(0));
    QVERIFY(marker != nullptr);
    const QRect originalRect = marker->boundingRect();

    QImage warmup(260, 180, QImage::Format_ARGB32);
    warmup.fill(Qt::white);
    QPainter warmPainter(&warmup);
    window.m_annotationLayer->draw(warmPainter);

    const QRect cropRect(60, 40, 140, 100);
    const QRect expectedRect = originalRect.translated(-cropRect.topLeft());

    window.applyCrop(cropRect);
    marker = dynamic_cast<MarkerStroke*>(window.m_annotationLayer->itemAt(0));
    QVERIFY(marker != nullptr);
    QCOMPARE(marker->boundingRect(), expectedRect);

    window.undoCrop();
    marker = dynamic_cast<MarkerStroke*>(window.m_annotationLayer->itemAt(0));
    QVERIFY(marker != nullptr);
    QCOMPARE(marker->boundingRect(), originalRect);
}

void TestPinWindowCropUndo::testApplyCrop_RefreshesMosaicSourceInsideErasedGroup()
{
    QPixmap source = createPatternPixmap(240, 160);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.toggleToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);
    QVERIFY(window.m_sharedSourcePixmap != nullptr);

    const QRect originalRect(90, 70, 50, 30);
    window.m_annotationLayer->addItem(std::make_unique<MosaicRectAnnotation>(
        originalRect,
        window.m_sharedSourcePixmap,
        12,
        MosaicRectAnnotation::BlurType::Pixelate));

    window.m_annotationLayer->setSelectedIndex(0);
    QVERIFY(window.m_annotationLayer->removeSelectedItem());
    QVERIFY(dynamic_cast<ErasedItemsGroup*>(window.m_annotationLayer->itemAt(0)) != nullptr);

    const QRect cropRect(60, 40, 120, 90);
    window.applyCrop(cropRect);

    // Undo deletion to restore the mosaic item from ErasedItemsGroup storage.
    window.m_annotationLayer->undo();
    auto* restored = dynamic_cast<MosaicRectAnnotation*>(window.m_annotationLayer->itemAt(0));
    QVERIFY(restored != nullptr);

    QImage actual(120, 90, QImage::Format_ARGB32_Premultiplied);
    actual.fill(Qt::transparent);
    {
        QPainter painter(&actual);
        restored->draw(painter);
    }

    const QRect translatedRect = originalRect.translated(-cropRect.topLeft());
    auto expectedSource = std::make_shared<const QPixmap>(source.copy(cropRect));
    MosaicRectAnnotation expectedAnnotation(
        translatedRect,
        expectedSource,
        12,
        MosaicRectAnnotation::BlurType::Pixelate);

    QImage expected(120, 90, QImage::Format_ARGB32_Premultiplied);
    expected.fill(Qt::transparent);
    {
        QPainter painter(&expected);
        expectedAnnotation.draw(painter);
    }

    QCOMPARE(actual, expected);
}

void TestPinWindowCropUndo::testApplyCrop_EdgeEndpointCoordinate_ClampsToLastPixelColumn()
{
    QPixmap source = createPatternPixmap(100, 60);
    PinWindow window(source, QPoint(0, 0), nullptr, false);

    const int endpointX = window.cropToolImageSize().width();
    const QRect endpointRect(QPoint(endpointX, 10), QPoint(endpointX, 40));
    const QPixmap expected = source.copy(QRect(99, 10, 1, 31));

    window.applyCrop(endpointRect);

    QVERIFY(pixmapsEqual(window.m_originalPixmap, expected));
}

void TestPinWindowCropUndo::testHandleToolbarUndo_PrioritizesCropWhenNoPostCropAnnotations()
{
    QPixmap source = createPatternPixmap(220, 140);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.toggleToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);

    QVector<QPoint> points = {QPoint(40, 35), QPoint(120, 55)};
    window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
        points, QColor(Qt::red), 3, LineEndStyle::None, LineStyle::Solid));
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(1));

    const QRect cropRect(30, 20, 120, 80);
    const QPixmap expectedCropped = source.copy(cropRect);
    window.applyCrop(cropRect);
    QVERIFY(pixmapsEqual(window.m_originalPixmap, expectedCropped));
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(1));

    window.handleToolbarUndo();

    QVERIFY(pixmapsEqual(window.m_originalPixmap, source));
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(1));
}

void TestPinWindowCropUndo::testHandleToolbarUndo_PrioritizesPostCropAnnotationsFirst()
{
    QPixmap source = createPatternPixmap(220, 140);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.toggleToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);

    QVector<QPoint> preCropPoints = {QPoint(40, 35), QPoint(120, 55)};
    window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
        preCropPoints, QColor(Qt::red), 3, LineEndStyle::None, LineStyle::Solid));

    const QRect cropRect(30, 20, 120, 80);
    const QPixmap expectedCropped = source.copy(cropRect);
    window.applyCrop(cropRect);
    QVERIFY(pixmapsEqual(window.m_originalPixmap, expectedCropped));

    QVector<QPoint> postCropPoints = {QPoint(20, 20), QPoint(80, 45)};
    window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
        postCropPoints, QColor(Qt::blue), 3, LineEndStyle::None, LineStyle::Solid));
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(2));

    window.handleToolbarUndo();

    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(1));
    QVERIFY(pixmapsEqual(window.m_originalPixmap, expectedCropped));

    window.handleToolbarUndo();

    QVERIFY(pixmapsEqual(window.m_originalPixmap, source));
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(1));
}

void TestPinWindowCropUndo::testHandleToolbarUndo_UsesStableCropBoundaryAfterAnnotationTrim()
{
    QPixmap source = createPatternPixmap(260, 180);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.toggleToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);

    // Fill annotation history to capacity so the first post-crop add trims from the front.
    for (int i = 0; i < 50; ++i) {
        const QVector<QPoint> points = {
            QPoint(20 + i, 20 + i),
            QPoint(80 + i, 40 + i)
        };
        window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
            points, QColor(Qt::red), 2, LineEndStyle::None, LineStyle::Solid));
    }
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(50));

    const QRect cropRect(30, 20, 160, 110);
    const QPixmap expectedCropped = source.copy(cropRect);
    window.applyCrop(cropRect);
    QVERIFY(pixmapsEqual(window.m_originalPixmap, expectedCropped));

    const QVector<QPoint> postCropPoints = {QPoint(18, 16), QPoint(70, 42)};
    window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
        postCropPoints, QColor(Qt::blue), 2, LineEndStyle::None, LineStyle::Solid));
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(50));

    window.handleToolbarUndo(); // Undo post-crop annotation first.
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(49));
    QVERIFY(pixmapsEqual(window.m_originalPixmap, expectedCropped));

    window.handleToolbarUndo(); // Then undo crop, not pre-crop annotation.
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(49));
    QVERIFY(pixmapsEqual(window.m_originalPixmap, source));
}

void TestPinWindowCropUndo::testHandleToolbarRedo_ReappliesCropAfterUndo()
{
    QPixmap source = createPatternPixmap(200, 130);
    PinWindow window(source, QPoint(0, 0), nullptr, false);

    const QRect cropRect(25, 15, 120, 80);
    const QPixmap expectedCropped = source.copy(cropRect);

    window.applyCrop(cropRect);
    QVERIFY(pixmapsEqual(window.m_originalPixmap, expectedCropped));

    window.handleToolbarUndo();
    QVERIFY(pixmapsEqual(window.m_originalPixmap, source));

    window.handleToolbarRedo();
    QVERIFY(pixmapsEqual(window.m_originalPixmap, expectedCropped));
    QVERIFY(!window.m_cropUndoStack.isEmpty());
    QVERIFY(window.m_cropRedoStack.isEmpty());
}

void TestPinWindowCropUndo::testTransformChange_ClearsCropHistory()
{
    QPixmap source = createPatternPixmap(200, 130);
    PinWindow window(source, QPoint(0, 0), nullptr, false);

    const QRect cropRect(25, 15, 120, 80);
    const QPixmap expectedCropped = source.copy(cropRect);

    window.applyCrop(cropRect);
    QVERIFY(pixmapsEqual(window.m_originalPixmap, expectedCropped));
    QVERIFY(!window.m_cropUndoStack.isEmpty());

    window.handleToolbarUndo();
    QVERIFY(pixmapsEqual(window.m_originalPixmap, source));
    QVERIFY(window.m_cropUndoStack.isEmpty());
    QVERIFY(!window.m_cropRedoStack.isEmpty());

    window.rotateRight();  // Any transform change should invalidate crop history.
    QCOMPARE(window.m_rotationAngle, 90);
    QVERIFY(window.m_cropUndoStack.isEmpty());
    QVERIFY(window.m_cropRedoStack.isEmpty());

    const QPixmap afterRotate = window.m_originalPixmap;
    window.handleToolbarRedo();  // Should be no-op because crop history was cleared.
    QCOMPARE(window.m_rotationAngle, 90);
    QVERIFY(pixmapsEqual(window.m_originalPixmap, afterRotate));
}

void TestPinWindowCropUndo::testHandleToolbarRedo_PrioritizesCropBeforePostCropAnnotationRedo()
{
    QPixmap source = createPatternPixmap(220, 140);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.toggleToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);

    const QVector<QPoint> preCropPoints = {QPoint(40, 35), QPoint(120, 55)};
    window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
        preCropPoints, QColor(Qt::red), 3, LineEndStyle::None, LineStyle::Solid));

    const QRect cropRect(30, 20, 120, 80);
    const QPixmap expectedCropped = source.copy(cropRect);
    window.applyCrop(cropRect);

    const QVector<QPoint> postCropPoints = {QPoint(20, 20), QPoint(80, 45)};
    window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
        postCropPoints, QColor(Qt::blue), 3, LineEndStyle::None, LineStyle::Solid));
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(2));

    window.handleToolbarUndo(); // Undo post-crop annotation
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(1));

    window.handleToolbarUndo(); // Undo crop
    QVERIFY(pixmapsEqual(window.m_originalPixmap, source));
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(1));

    window.handleToolbarRedo(); // Redo crop first
    QVERIFY(pixmapsEqual(window.m_originalPixmap, expectedCropped));
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(1));

    window.handleToolbarRedo(); // Then redo post-crop annotation
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(2));
}

void TestPinWindowCropUndo::testHandleToolbarRedo_UsesStableCropBoundaryAfterAnnotationTrim()
{
    QPixmap source = createPatternPixmap(260, 180);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.toggleToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);

    // Fill annotation history to capacity so post-crop add triggers trimHistory().
    for (int i = 0; i < 50; ++i) {
        const QVector<QPoint> points = {
            QPoint(20 + i, 20 + i),
            QPoint(80 + i, 40 + i)
        };
        window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
            points, QColor(Qt::red), 2, LineEndStyle::None, LineStyle::Solid));
    }
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(50));

    const QRect cropRect(30, 20, 160, 110);
    const QPixmap expectedCropped = source.copy(cropRect);
    window.applyCrop(cropRect);

    const QVector<QPoint> postCropPoints = {QPoint(18, 16), QPoint(70, 42)};
    window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
        postCropPoints, QColor(Qt::blue), 2, LineEndStyle::None, LineStyle::Solid));
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(50));

    window.handleToolbarUndo(); // Undo post-crop annotation
    window.handleToolbarUndo(); // Undo crop
    QVERIFY(pixmapsEqual(window.m_originalPixmap, source));
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(49));

    window.handleToolbarRedo(); // Redo crop first when boundary matches via top item.
    QVERIFY(pixmapsEqual(window.m_originalPixmap, expectedCropped));
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(49));

    window.handleToolbarRedo(); // Then redo post-crop annotation.
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(50));
}

void TestPinWindowCropUndo::testHandleToolbarUndo_InPlaceCacheInvalidationDoesNotPreemptCropUndo()
{
    QPixmap source = createPatternPixmap(220, 140);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.toggleToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);

    const QVector<QPoint> preCropPoints = {QPoint(40, 35), QPoint(120, 55)};
    window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
        preCropPoints, QColor(Qt::red), 3, LineEndStyle::None, LineStyle::Solid));

    const QRect cropRect(30, 20, 120, 80);
    window.applyCrop(cropRect);
    QVERIFY(!pixmapsEqual(window.m_originalPixmap, source));

    auto* polyline = dynamic_cast<PolylineAnnotation*>(window.m_annotationLayer->itemAt(0));
    QVERIFY(polyline != nullptr);
    polyline->moveBy(QPoint(7, 0));
    window.m_annotationLayer->invalidateCache();  // Mimic in-place edit flow in PinWindow handlers.

    window.handleToolbarUndo();

    // Cache invalidation after in-place edits does not create annotation undo entries.
    // Undo should restore crop first instead of deleting pre-crop annotations.
    QVERIFY(pixmapsEqual(window.m_originalPixmap, source));
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(1));
    QVERIFY(window.m_cropUndoStack.isEmpty());

    polyline = dynamic_cast<PolylineAnnotation*>(window.m_annotationLayer->itemAt(0));
    QVERIFY(polyline != nullptr);
    QCOMPARE(polyline->points(), QVector<QPoint>({QPoint(47, 35), QPoint(127, 55)}));

    window.handleToolbarUndo();
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(0));
}

QTEST_MAIN(TestPinWindowCropUndo)
#include "tst_CropUndo.moc"
