#include <QtTest/QtTest>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QScreen>

#include "annotations/MosaicBlurType.h"
#include "PinWindow.h"

#include "annotations/AnnotationLayer.h"
#include "annotations/MarkerStroke.h"
#include "annotations/MosaicRectAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "pinwindow/RegionLayoutManager.h"
#include "pinwindow/RegionLayoutRenderer.h"
#include "utils/CoordinateHelper.h"

namespace {

QPixmap createPatternPixmap(int width, int height, qreal dpr = 1.0)
{
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            image.setPixelColor(x, y, QColor(x % 256, y % 256, (x + y) % 256, 255));
        }
    }
    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
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
    void testApplyCrop_RefreshesMosaicSourceInsideRemoveCommand();
    void testApplyCrop_RefreshesMosaicSourceInsideUndoneAddCommand();
    void testRegionLayoutApply_MovesAnnotationAfterSuccessfulRecompose();
    void testRegionLayoutApply_RefreshesMosaicSourceInsideUndoneAddCommand();
    void testRegionLayoutApply_MaterializesResizedRegionForNextEdit();
    void testRegionLayoutApply_RecomposeFailureKeepsModeActive();
    void testRegionLayoutZoomMapsRenderingInputAndApply_data();
    void testRegionLayoutZoomMapsRenderingInputAndApply();
    void testRegionLayoutFreezesZoomDuringSession();
    void testRegionLayoutZoomedAnnotationFollowsRegion();
    void testRegionLayoutZoomAndRotationShareOneTransform();
    void testRegionLayoutChromeStaysFixedInView_data();
    void testRegionLayoutChromeStaysFixedInView();
    void testRegionLayoutOverlappingHandleTargetsChooseClosest();
    void testRegionLayoutKeepsControlsOnScreenAndRestoresPosition();
    void testRegionLayoutDefersScreenClampUntilGestureEnds();
    void testApplyCrop_EdgeEndpointCoordinate_ClampsToLastPixelColumn();
    void testPreciseSourceSampleRectForRegion_UsesScreenLocalCoordinates();
    void testDisplaySourceRectForTarget_PrefersTranslationOverScaling();
    void testSetSourceRegion_FractionalDpr_PreservesExactLogicalSize();
    void testCropUndoRedo_FractionalDpr_RestoresExactLogicalSize();
    void testHandleToolbarUndo_PrioritizesCropWhenNoPostCropAnnotations();
    void testHandleToolbarUndo_PrioritizesPostCropAnnotationsFirst();
    void testHandleToolbarUndo_DoesNothingDuringHistoryLock();
    void testHandleToolbarUndo_UsesStableCropBoundaryBeyondFiftyAnnotations();
    void testHandleToolbarRedo_ReappliesCropAfterUndo();
    void testHandleToolbarRedo_DoesNothingDuringHistoryLock();
    void testTransformChange_ClearsCropHistory();
    void testHandleToolbarRedo_PrioritizesCropBeforePostCropAnnotationRedo();
    void testHandleToolbarRedo_UsesStableCropBoundaryBeyondFiftyAnnotations();
    void testHandleToolbarUndo_InPlaceCacheInvalidationDoesNotPreemptCropUndo();
    void testHandleToolbarUndo_NonTopRemovalPrecedesCropUndo();
    void testHandleToolbarRedo_NewAnnotationBranchInvalidatesCropRedo();
    void testHistoryFloorInvalidatesUnreachableCropBoundary();
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
    window.showToolbar();
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
    window.showToolbar();
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
    window.showToolbar();

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
    window.showToolbar();
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
    window.showToolbar();
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

void TestPinWindowCropUndo::testApplyCrop_RefreshesMosaicSourceInsideRemoveCommand()
{
    QPixmap source = createPatternPixmap(240, 160);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);
    QVERIFY(window.m_sharedSourcePixmap != nullptr);

    const QRect originalRect(90, 70, 50, 30);
    window.m_annotationLayer->addItem(std::make_unique<MosaicRectAnnotation>(
        originalRect,
        window.m_sharedSourcePixmap,
        12,
        MosaicBlurType::Pixelate));

    window.m_annotationLayer->setSelectedIndex(0);
    QVERIFY(window.m_annotationLayer->removeSelectedItem());
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(0));
    QVERIFY(window.m_annotationLayer->canUndo());

    const QRect cropRect(60, 40, 120, 90);
    window.applyCrop(cropRect);

    // Undo deletion to restore the Mosaic item from command-owned storage.
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
        MosaicBlurType::Pixelate);

    QImage expected(120, 90, QImage::Format_ARGB32_Premultiplied);
    expected.fill(Qt::transparent);
    {
        QPainter painter(&expected);
        expectedAnnotation.draw(painter);
    }

    QCOMPARE(actual, expected);
}

void TestPinWindowCropUndo::testApplyCrop_RefreshesMosaicSourceInsideUndoneAddCommand()
{
    QPixmap source = createPatternPixmap(240, 160);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);

    const QRect originalRect(90, 70, 50, 30);
    window.m_annotationLayer->addItem(std::make_unique<MosaicRectAnnotation>(
        originalRect,
        window.m_sharedSourcePixmap,
        12,
        MosaicBlurType::Pixelate));
    window.m_annotationLayer->undo();
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(0));
    QVERIFY(window.m_annotationLayer->canRedo());

    const QRect cropRect(60, 40, 120, 90);
    window.applyCrop(cropRect);
    window.m_annotationLayer->redo();

    auto* restored = dynamic_cast<MosaicRectAnnotation*>(window.m_annotationLayer->itemAt(0));
    QVERIFY(restored != nullptr);

    QImage actual(120, 90, QImage::Format_ARGB32_Premultiplied);
    actual.fill(Qt::transparent);
    {
        QPainter painter(&actual);
        restored->draw(painter);
    }

    auto expectedSource = std::make_shared<const QPixmap>(source.copy(cropRect));
    MosaicRectAnnotation expectedAnnotation(
        originalRect.translated(-cropRect.topLeft()),
        expectedSource,
        12,
        MosaicBlurType::Pixelate);
    QImage expected(120, 90, QImage::Format_ARGB32_Premultiplied);
    expected.fill(Qt::transparent);
    {
        QPainter painter(&expected);
        expectedAnnotation.draw(painter);
    }

    QCOMPARE(actual, expected);
}

void TestPinWindowCropUndo::testRegionLayoutApply_MovesAnnotationAfterSuccessfulRecompose()
{
    const QPixmap source = createPatternPixmap(210, 100);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);

    QVector<LayoutRegion> regions;
    LayoutRegion leftRegion;
    leftRegion.rect = QRect(0, 0, 100, 100);
    leftRegion.originalRect = leftRegion.rect;
    leftRegion.image = source.toImage().copy(leftRegion.rect);
    leftRegion.index = 1;
    regions.append(leftRegion);

    LayoutRegion rightRegion;
    rightRegion.rect = QRect(110, 0, 100, 100);
    rightRegion.originalRect = rightRegion.rect;
    rightRegion.image = source.toImage().copy(rightRegion.rect);
    rightRegion.index = 2;
    regions.append(rightRegion);

    const QVector<QPointF> points = {QPointF(130, 30), QPointF(160, 60)};
    window.m_annotationLayer->addItem(
        std::make_unique<MarkerStroke>(points, QColor(Qt::red), 12));
    auto* marker = dynamic_cast<MarkerStroke*>(window.m_annotationLayer->itemAt(0));
    QVERIFY(marker != nullptr);
    const QRect originalAnnotationRect = marker->boundingRect();

    window.setMultiRegionData(regions);
    window.enterRegionLayoutMode();
    QVERIFY(window.m_regionLayoutManager != nullptr);
    window.m_regionLayoutManager->selectRegion(1);
    window.m_regionLayoutManager->startDrag(QPoint(160, 50));
    window.m_regionLayoutManager->updateDrag(QPoint(200, 50));
    window.m_regionLayoutManager->finishDrag();
    window.exitRegionLayoutMode(true);

    marker = dynamic_cast<MarkerStroke*>(window.m_annotationLayer->itemAt(0));
    QVERIFY(marker != nullptr);
    QCOMPARE(marker->boundingRect(), originalAnnotationRect.translated(40, 0));
    QCOMPARE(window.m_originalPixmap.deviceIndependentSize().toSize(), QSize(250, 100));
    QVERIFY(window.m_sharedSourcePixmap != nullptr);
    QVERIFY(pixmapsEqual(*window.m_sharedSourcePixmap, window.m_originalPixmap));
}

void TestPinWindowCropUndo::testRegionLayoutApply_RefreshesMosaicSourceInsideUndoneAddCommand()
{
    const QPixmap source = createPatternPixmap(210, 100);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);
    QVERIFY(window.m_sharedSourcePixmap != nullptr);

    QVector<LayoutRegion> regions;
    LayoutRegion leftRegion;
    leftRegion.rect = QRect(0, 0, 100, 100);
    leftRegion.originalRect = leftRegion.rect;
    leftRegion.image = source.toImage().copy(leftRegion.rect);
    leftRegion.index = 1;
    regions.append(leftRegion);

    LayoutRegion rightRegion;
    rightRegion.rect = QRect(110, 0, 100, 100);
    rightRegion.originalRect = rightRegion.rect;
    rightRegion.image = source.toImage().copy(rightRegion.rect);
    rightRegion.index = 2;
    regions.append(rightRegion);

    const QRect originalRect(130, 30, 30, 30);
    window.m_annotationLayer->addItem(std::make_unique<MosaicRectAnnotation>(
        originalRect,
        window.m_sharedSourcePixmap,
        12,
        MosaicBlurType::Pixelate));
    window.m_annotationLayer->undo();
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(0));

    window.setMultiRegionData(regions);
    window.enterRegionLayoutMode();
    QVERIFY(window.m_regionLayoutManager != nullptr);
    window.m_regionLayoutManager->selectRegion(1);
    window.m_regionLayoutManager->startDrag(QPoint(160, 50));
    window.m_regionLayoutManager->updateDrag(QPoint(200, 50));
    window.m_regionLayoutManager->finishDrag();
    window.exitRegionLayoutMode(true);

    window.m_annotationLayer->redo();
    auto* restored = dynamic_cast<MosaicRectAnnotation*>(window.m_annotationLayer->itemAt(0));
    QVERIFY(restored != nullptr);

    QImage actual(250, 100, QImage::Format_ARGB32_Premultiplied);
    actual.fill(Qt::transparent);
    {
        QPainter painter(&actual);
        restored->draw(painter);
    }

    auto expectedSource = std::make_shared<const QPixmap>(window.m_originalPixmap);
    MosaicRectAnnotation expectedAnnotation(
        originalRect.translated(40, 0),
        expectedSource,
        12,
        MosaicBlurType::Pixelate);
    QImage expected(250, 100, QImage::Format_ARGB32_Premultiplied);
    expected.fill(Qt::transparent);
    {
        QPainter painter(&expected);
        expectedAnnotation.draw(painter);
    }

    auto staleSource = std::make_shared<const QPixmap>(source);
    MosaicRectAnnotation staleAnnotation(
        originalRect.translated(40, 0),
        staleSource,
        12,
        MosaicBlurType::Pixelate);
    QImage stale(250, 100, QImage::Format_ARGB32_Premultiplied);
    stale.fill(Qt::transparent);
    {
        QPainter painter(&stale);
        staleAnnotation.draw(painter);
    }

    QVERIFY(stale != expected);
    QCOMPARE(actual, expected);
}

void TestPinWindowCropUndo::testRegionLayoutApply_MaterializesResizedRegionForNextEdit()
{
    const QPixmap source = createPatternPixmap(100, 100);
    PinWindow window(source, QPoint(0, 0), nullptr, false);

    LayoutRegion region;
    region.rect = QRect(0, 0, 100, 100);
    region.originalRect = region.rect;
    region.image = source.toImage();
    region.index = 1;
    window.setMultiRegionData({region});

    window.enterRegionLayoutMode();
    QVERIFY(window.m_regionLayoutManager != nullptr);
    window.m_regionLayoutManager->selectRegion(0);
    window.m_regionLayoutManager->startResize(
        ResizeHandler::Edge::BottomRight, QPoint(99, 99));
    window.m_regionLayoutManager->updateResize(QPoint(199, 199), false);
    window.m_regionLayoutManager->finishResize();
    window.exitRegionLayoutMode(true);

    QVERIFY(!window.isRegionLayoutMode());
    QCOMPARE(window.m_originalPixmap.deviceIndependentSize().toSize(), QSize(200, 200));
    QCOMPARE(window.m_storedRegions.size(), 1);
    QCOMPARE(window.m_storedRegions[0].rect, QRect(0, 0, 200, 200));
    QCOMPARE(window.m_storedRegions[0].originalRect, QRect(0, 0, 200, 200));
    QCOMPARE(window.m_storedRegions[0].image.size(), QSize(200, 200));
    const QPixmap firstApply = window.m_originalPixmap;

    // Re-enter and apply without another resize. The committed per-region
    // image must already match the 200x200 rect, so no transparent quadrant
    // appears and the result remains byte-for-byte stable.
    window.enterRegionLayoutMode();
    window.exitRegionLayoutMode(true);

    QVERIFY(!window.isRegionLayoutMode());
    QVERIFY(pixmapsEqual(window.m_originalPixmap, firstApply));
    QVERIFY(window.m_originalPixmap.toImage().pixelColor(199, 199).alpha() > 0);
}

void TestPinWindowCropUndo::testRegionLayoutApply_RecomposeFailureKeepsModeActive()
{
    const QPixmap source = createPatternPixmap(100, 100);
    PinWindow window(source, QPoint(0, 0), nullptr, false);

    LayoutRegion invalidRegion;
    invalidRegion.rect = QRect(0, 0, 100, 100);
    invalidRegion.originalRect = invalidRegion.rect;
    invalidRegion.image = QImage();
    invalidRegion.index = 1;
    window.setMultiRegionData({invalidRegion});

    window.enterRegionLayoutMode();
    QVERIFY(window.m_regionLayoutManager != nullptr);
    window.m_regionLayoutManager->selectRegion(0);
    window.m_regionLayoutManager->startDrag(QPoint(50, 50));
    window.m_regionLayoutManager->updateDrag(QPoint(130, 50));
    window.m_regionLayoutManager->finishDrag();
    const QSize activeLayoutSize = window.size();
    QCOMPARE(activeLayoutSize, window.regionLayoutViewportSize(QSize(180, 100)));

    window.exitRegionLayoutMode(true);

    // A failed allocation/invalid source must not exit into a half-applied
    // state. The active layout is left intact so Cancel remains available.
    QVERIFY(window.isRegionLayoutMode());
    QCOMPARE(window.size(), activeLayoutSize);
    QVERIFY(pixmapsEqual(window.m_originalPixmap, source));
    QCOMPARE(window.m_storedRegions[0].rect, invalidRegion.rect);

    window.exitRegionLayoutMode(false);
    QVERIFY(!window.isRegionLayoutMode());
    QCOMPARE(window.size(), QSize(100, 100));
}

void TestPinWindowCropUndo::testRegionLayoutZoomMapsRenderingInputAndApply_data()
{
    QTest::addColumn<qreal>("zoom");
    QTest::addColumn<int>("expectedModelDelta");

    QTest::newRow("half") << 0.5 << 40;
    QTest::newRow("double") << 2.0 << 10;
}

void TestPinWindowCropUndo::testRegionLayoutZoomMapsRenderingInputAndApply()
{
    QFETCH(qreal, zoom);
    QFETCH(int, expectedModelDelta);

    QImage sourceImage(210, 100, QImage::Format_ARGB32_Premultiplied);
    sourceImage.fill(Qt::transparent);
    {
        QPainter painter(&sourceImage);
        painter.fillRect(QRect(0, 0, 100, 100), Qt::red);
        painter.fillRect(QRect(110, 0, 100, 100), Qt::blue);
    }
    const QPixmap source = QPixmap::fromImage(sourceImage);
    PinWindow window(source, QPoint(0, 0), nullptr, false);

    LayoutRegion leftRegion;
    leftRegion.rect = QRect(0, 0, 100, 100);
    leftRegion.originalRect = leftRegion.rect;
    leftRegion.image = sourceImage.copy(leftRegion.rect);
    leftRegion.index = 1;

    LayoutRegion rightRegion;
    rightRegion.rect = QRect(110, 0, 100, 100);
    rightRegion.originalRect = rightRegion.rect;
    rightRegion.image = sourceImage.copy(rightRegion.rect);
    rightRegion.index = 2;

    window.setZoomLevel(zoom);
    window.setMultiRegionData({leftRegion, rightRegion});
    window.enterRegionLayoutMode();
    QVERIFY(window.isRegionLayoutMode());
    QCOMPARE(window.size(), window.regionLayoutViewportSize(QSize(210, 100)));

    const QPointF secondCenterView(190.0 * zoom, 20.0 * zoom);
    const QPointF secondSampleView(180.0 * zoom, 10.0 * zoom);
    QImage rendered(window.size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    window.render(&rendered);
    QCOMPARE(rendered.pixelColor(secondSampleView.toPoint()), QColor(Qt::blue));

    QMouseEvent pressEvent(
        QEvent::MouseButtonPress,
        secondCenterView,
        window.mapToGlobal(secondCenterView.toPoint()),
        Qt::LeftButton,
        Qt::LeftButton,
        Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &pressEvent);
    QCOMPARE(window.m_regionLayoutManager->selectedIndex(), 1);

    const QPointF movedView = secondCenterView + QPointF(20.0, 0.0);
    QMouseEvent moveEvent(
        QEvent::MouseMove,
        movedView,
        window.mapToGlobal(movedView.toPoint()),
        Qt::NoButton,
        Qt::LeftButton,
        Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &moveEvent);
    QCOMPARE(window.m_regionLayoutManager->regions()[1].rect.left(),
             110 + expectedModelDelta);
    QCOMPARE(window.size(), window.regionLayoutViewportSize(
        window.m_regionLayoutManager->canvasBounds().size()));

    QMouseEvent releaseEvent(
        QEvent::MouseButtonRelease,
        movedView,
        window.mapToGlobal(movedView.toPoint()),
        Qt::LeftButton,
        Qt::NoButton,
        Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &releaseEvent);

    const QPointF confirmView = RegionLayoutRenderer::confirmButtonRect(
        window.regionLayoutControlsRect(
            window.m_regionLayoutManager->canvasBounds().size())).center();
    QMouseEvent confirmEvent(
        QEvent::MouseButtonPress,
        confirmView,
        window.mapToGlobal(confirmView.toPoint()),
        Qt::LeftButton,
        Qt::LeftButton,
        Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &confirmEvent);

    QVERIFY(!window.isRegionLayoutMode());
    QCOMPARE(window.zoomLevel(), zoom);
    const QSize expectedBaseSize(210 + expectedModelDelta, 100);
    QCOMPARE(window.baseContentLogicalSize(), expectedBaseSize);
    QCOMPARE(window.size(), CoordinateHelper::toPhysical(expectedBaseSize, zoom));
    QCOMPARE(window.m_displayPixmap.deviceIndependentSize().toSize(), window.size());
    const QSize appliedSize = window.size();
    window.updateSize();
    QCOMPARE(window.size(), appliedSize);
}

void TestPinWindowCropUndo::testRegionLayoutFreezesZoomDuringSession()
{
    const QPixmap source = createPatternPixmap(210, 100);
    PinWindow window(source, QPoint(0, 0), nullptr, false);

    LayoutRegion leftRegion;
    leftRegion.rect = QRect(0, 0, 100, 100);
    leftRegion.originalRect = leftRegion.rect;
    leftRegion.image = source.toImage().copy(leftRegion.rect);

    LayoutRegion rightRegion;
    rightRegion.rect = QRect(110, 0, 100, 100);
    rightRegion.originalRect = rightRegion.rect;
    rightRegion.image = source.toImage().copy(rightRegion.rect);

    window.setMultiRegionData({leftRegion, rightRegion});
    window.enterRegionLayoutMode();
    window.m_regionLayoutManager->selectRegion(1);
    window.m_regionLayoutManager->startDrag(QPoint(160, 50));
    window.m_regionLayoutManager->updateDrag(QPoint(200, 50));
    window.m_regionLayoutManager->finishDrag();
    QCOMPARE(window.m_regionLayoutManager->canvasBounds().size(), QSize(250, 100));

    window.setZoomLevel(2.0);
    QCOMPARE(window.zoomLevel(), 1.0);
    QCOMPARE(window.size(), window.regionLayoutViewportSize(QSize(250, 100)));
    window.setZoomLevel(0.5);
    QCOMPARE(window.zoomLevel(), 1.0);
    QCOMPARE(window.size(), window.regionLayoutViewportSize(QSize(250, 100)));

    window.exitRegionLayoutMode(false);
    QVERIFY(!window.isRegionLayoutMode());
    QCOMPARE(window.zoomLevel(), 1.0);
    QCOMPARE(window.size(), QSize(210, 100));
    window.updateSize();
    QCOMPARE(window.size(), QSize(210, 100));
}

void TestPinWindowCropUndo::testRegionLayoutZoomedAnnotationFollowsRegion()
{
    const QPixmap source = createPatternPixmap(210, 100);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.setZoomLevel(2.0);
    window.showToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);

    LayoutRegion leftRegion;
    leftRegion.rect = QRect(0, 0, 100, 100);
    leftRegion.originalRect = leftRegion.rect;
    leftRegion.image = source.toImage().copy(leftRegion.rect);

    LayoutRegion rightRegion;
    rightRegion.rect = QRect(110, 0, 100, 100);
    rightRegion.originalRect = rightRegion.rect;
    rightRegion.image = source.toImage().copy(rightRegion.rect);

    const QVector<QPointF> points = {QPointF(260, 40), QPointF(320, 80)};
    window.m_annotationLayer->addItem(
        std::make_unique<MarkerStroke>(points, QColor(Qt::red), 12));
    auto* marker = dynamic_cast<MarkerStroke*>(window.m_annotationLayer->itemAt(0));
    QVERIFY(marker != nullptr);
    const QRect originalRect = marker->boundingRect();

    window.setMultiRegionData({leftRegion, rightRegion});
    window.enterRegionLayoutMode();
    window.setZoomLevel(0.5);
    QCOMPARE(window.zoomLevel(), 2.0);
    window.m_regionLayoutManager->selectRegion(1);
    window.m_regionLayoutManager->startDrag(QPoint(160, 50));
    window.m_regionLayoutManager->updateDrag(QPoint(200, 50));
    window.m_regionLayoutManager->finishDrag();
    window.exitRegionLayoutMode(true);

    marker = dynamic_cast<MarkerStroke*>(window.m_annotationLayer->itemAt(0));
    QVERIFY(marker != nullptr);
    QCOMPARE(marker->boundingRect(), originalRect.translated(80, 0));
    QCOMPARE(window.zoomLevel(), 2.0);
    QCOMPARE(window.size(), QSize(500, 200));
}

void TestPinWindowCropUndo::testRegionLayoutZoomAndRotationShareOneTransform()
{
    QImage sourceImage(210, 100, QImage::Format_ARGB32_Premultiplied);
    sourceImage.fill(Qt::transparent);
    {
        QPainter painter(&sourceImage);
        painter.fillRect(QRect(0, 0, 100, 100), Qt::red);
        painter.fillRect(QRect(110, 0, 100, 100), Qt::blue);
    }
    PinWindow window(QPixmap::fromImage(sourceImage), QPoint(0, 0), nullptr, false);

    LayoutRegion leftRegion;
    leftRegion.rect = QRect(0, 0, 100, 100);
    leftRegion.originalRect = leftRegion.rect;
    leftRegion.image = sourceImage.copy(leftRegion.rect);

    LayoutRegion rightRegion;
    rightRegion.rect = QRect(110, 0, 100, 100);
    rightRegion.originalRect = rightRegion.rect;
    rightRegion.image = sourceImage.copy(rightRegion.rect);

    window.setZoomLevel(2.0);
    window.rotateRight();
    window.setMultiRegionData({leftRegion, rightRegion});
    window.enterRegionLayoutMode();
    QCOMPARE(window.size(), window.regionLayoutViewportSize(QSize(210, 100)));

    const QSize activeSize = window.size();
    window.rotateRight();
    window.flipHorizontal();
    window.setZoomLevel(0.5);
    QCOMPARE(window.m_rotationAngle, 90);
    QCOMPARE(window.m_flipHorizontal, false);
    QCOMPARE(window.zoomLevel(), 2.0);
    QCOMPARE(window.size(), activeSize);

    const QPointF mappedCenter = window.regionLayoutViewTransform(QSize(210, 100)).map(
        QPointF(160, 50));
    QCOMPARE(mappedCenter.toPoint(), QPoint(100, 320));

    QImage rendered(window.size(), QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    window.render(&rendered);
    QCOMPARE(rendered.pixelColor(mappedCenter.toPoint()), QColor(Qt::blue));

    QMouseEvent pressEvent(
        QEvent::MouseButtonPress,
        mappedCenter,
        window.mapToGlobal(mappedCenter.toPoint()),
        Qt::LeftButton,
        Qt::LeftButton,
        Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &pressEvent);
    QCOMPARE(window.m_regionLayoutManager->selectedIndex(), 1);
}

void TestPinWindowCropUndo::testRegionLayoutChromeStaysFixedInView_data()
{
    QTest::addColumn<qreal>("zoom");
    QTest::newRow("minimum") << 0.1;
    QTest::newRow("half") << 0.5;
    QTest::newRow("double") << 2.0;
    QTest::newRow("maximum") << 5.0;
}

void TestPinWindowCropUndo::testRegionLayoutChromeStaysFixedInView()
{
    QFETCH(qreal, zoom);

    const QPixmap source = createPatternPixmap(500, 300);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    LayoutRegion region;
    region.rect = QRect(0, 0, 500, 300);
    region.originalRect = region.rect;
    region.image = source.toImage();

    window.setZoomLevel(zoom);
    window.setMultiRegionData({region});
    window.enterRegionLayoutMode();
    window.m_regionLayoutManager->selectRegion(0);

    const QRect controlsRect = window.regionLayoutControlsRect(QSize(500, 300));
    const QRect confirmRect = RegionLayoutRenderer::confirmButtonRect(controlsRect);
    const QRect cancelRect = RegionLayoutRenderer::cancelButtonRect(controlsRect);
    QCOMPARE(confirmRect.size(), QSize(
        LayoutModeConstants::kButtonWidth,
        LayoutModeConstants::kButtonHeight));
    QCOMPARE(cancelRect.size(), confirmRect.size());
    QVERIFY(window.rect().contains(confirmRect));
    QVERIFY(window.rect().contains(cancelRect));

    const QTransform modelToView = window.regionLayoutViewTransform(QSize(500, 300));
    const QPoint topLeft = modelToView.map(QPointF(0.0, 0.0)).toPoint();
    QCOMPARE(window.regionLayoutHandleAtWidget(topLeft),
             ResizeHandler::Edge::TopLeft);
    QCOMPARE(window.regionLayoutHandleAtWidget(topLeft + QPoint(7, 0)),
             ResizeHandler::Edge::TopLeft);
    QCOMPARE(window.regionLayoutHandleAtWidget(topLeft + QPoint(9, 0)),
             ResizeHandler::Edge::None);
}

void TestPinWindowCropUndo::testRegionLayoutOverlappingHandleTargetsChooseClosest()
{
    const QPixmap source = createPatternPixmap(100, 100);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    LayoutRegion region;
    region.rect = QRect(0, 0, 100, 100);
    region.originalRect = region.rect;
    region.image = source.toImage();

    window.setZoomLevel(0.1);
    window.setMultiRegionData({region});
    window.enterRegionLayoutMode();
    window.m_regionLayoutManager->selectRegion(0);

    const QTransform modelToView = window.regionLayoutViewTransform(QSize(100, 100));
    const QRect viewRect = modelToView.mapRect(QRectF(region.rect)).toAlignedRect();
    const QPair<ResizeHandler::Edge, QPoint> handles[] = {
        {ResizeHandler::Edge::TopLeft, viewRect.topLeft()},
        {ResizeHandler::Edge::Top, QPoint(viewRect.center().x(), viewRect.top())},
        {ResizeHandler::Edge::TopRight, viewRect.topRight()},
        {ResizeHandler::Edge::Right, QPoint(viewRect.right(), viewRect.center().y())},
        {ResizeHandler::Edge::BottomRight, viewRect.bottomRight()},
        {ResizeHandler::Edge::Bottom, QPoint(viewRect.center().x(), viewRect.bottom())},
        {ResizeHandler::Edge::BottomLeft, viewRect.bottomLeft()},
        {ResizeHandler::Edge::Left, QPoint(viewRect.left(), viewRect.center().y())}
    };
    for (const auto& handle : handles) {
        QCOMPARE(window.regionLayoutHandleAtWidget(handle.second), handle.first);
    }

    const QRect controlsRect = window.regionLayoutControlsRect(QSize(100, 100));
    QVERIFY(controlsRect.top() > viewRect.bottom());
    QVERIFY(!RegionLayoutRenderer::confirmButtonRect(controlsRect).intersects(viewRect));
    QVERIFY(!RegionLayoutRenderer::cancelButtonRect(controlsRect).intersects(viewRect));
}

void TestPinWindowCropUndo::testRegionLayoutKeepsControlsOnScreenAndRestoresPosition()
{
    QScreen* targetScreen = QGuiApplication::primaryScreen();
    QVERIFY(targetScreen != nullptr);
    const QRect available = targetScreen->availableGeometry();

    const QPixmap source = createPatternPixmap(100, 100);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    LayoutRegion region;
    region.rect = QRect(0, 0, 100, 100);
    region.originalRect = region.rect;
    region.image = source.toImage();

    const QPoint originalPos(
        available.left() + qMax(0, (available.width() - 100) / 2),
        available.bottom() - 99);
    window.move(originalPos);
    window.setMultiRegionData({region});
    window.enterRegionLayoutMode();

    const QRect controlsBand = window.regionLayoutControlsRect(QSize(100, 100));
    const QRect controlsGlobal = RegionLayoutRenderer::confirmButtonRect(controlsBand)
                                     .united(RegionLayoutRenderer::cancelButtonRect(controlsBand))
                                     .translated(window.pos());
    QVERIFY(available.contains(controlsGlobal));

    window.exitRegionLayoutMode(false);
    QCOMPARE(window.pos(), originalPos);

    // A layout wider than the screen cannot fit as a whole. The centered
    // button pair still must be clamped into the available geometry.
    if (available.width() >= LayoutModeConstants::kMaxCanvasSize) {
        QSKIP("Available screen is wider than the maximum legal layout canvas.");
    }
    const int wideLayoutWidth = qMin(
        LayoutModeConstants::kMaxCanvasSize,
        available.width() + 500);
    QVERIFY(wideLayoutWidth > available.width());
    PinWindow wideWindow(source, QPoint(0, 0), nullptr, false);
    LayoutRegion wideRegion = region;
    wideRegion.rect = QRect(0, 0, wideLayoutWidth, 100);
    wideRegion.originalRect = wideRegion.rect;
    wideWindow.move(originalPos);
    wideWindow.setMultiRegionData({wideRegion});
    wideWindow.enterRegionLayoutMode();

    const QRect wideControls = wideWindow.regionLayoutControlsRect(
        QSize(wideLayoutWidth, 100));
    const QRect buttonPairGlobal = RegionLayoutRenderer::confirmButtonRect(wideControls)
                                       .united(RegionLayoutRenderer::cancelButtonRect(wideControls))
                                       .translated(wideWindow.pos());
    QVERIFY(available.contains(buttonPairGlobal));

    wideWindow.exitRegionLayoutMode(false);
    QCOMPARE(wideWindow.pos(), originalPos);
}

void TestPinWindowCropUndo::testRegionLayoutDefersScreenClampUntilGestureEnds()
{
    QScreen* targetScreen = QGuiApplication::primaryScreen();
    QVERIFY(targetScreen != nullptr);
    const QRect available = targetScreen->availableGeometry();

    const QPixmap source = createPatternPixmap(210, 100);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    LayoutRegion leftRegion;
    leftRegion.rect = QRect(0, 0, 100, 100);
    leftRegion.originalRect = leftRegion.rect;
    leftRegion.image = source.toImage().copy(leftRegion.rect);
    LayoutRegion rightRegion;
    rightRegion.rect = QRect(110, 0, 100, 100);
    rightRegion.originalRect = rightRegion.rect;
    rightRegion.image = source.toImage().copy(rightRegion.rect);

    const QPoint originalPos(available.right() - 209, available.top() + 40);
    window.move(originalPos);
    window.setMultiRegionData({leftRegion, rightRegion});
    window.enterRegionLayoutMode();
    const QPoint entryPos = window.pos();

    const QPointF pressPos(190.0, 20.0);
    QMouseEvent pressEvent(
        QEvent::MouseButtonPress,
        pressPos,
        window.mapToGlobal(pressPos.toPoint()),
        Qt::LeftButton,
        Qt::LeftButton,
        Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &pressEvent);
    QVERIFY(window.m_regionLayoutManager->isDragging());

    const int dragDistance = qMin(
        available.width(),
        LayoutModeConstants::kMaxCanvasSize - 300);
    const QPointF movedPos = pressPos + QPointF(dragDistance, 0.0);
    QMouseEvent moveEvent(
        QEvent::MouseMove,
        movedPos,
        window.mapToGlobal(movedPos.toPoint()),
        Qt::NoButton,
        Qt::LeftButton,
        Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &moveEvent);
    QCOMPARE(window.pos(), entryPos);
    const QRect firstMovedRect = window.m_regionLayoutManager->regions()[1].rect;

    QMouseEvent samePositionMove(
        QEvent::MouseMove,
        movedPos,
        window.mapToGlobal(movedPos.toPoint()),
        Qt::NoButton,
        Qt::LeftButton,
        Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &samePositionMove);
    QCOMPARE(window.m_regionLayoutManager->regions()[1].rect, firstMovedRect);
    QCOMPARE(window.pos(), entryPos);

    QMouseEvent releaseEvent(
        QEvent::MouseButtonRelease,
        movedPos,
        window.mapToGlobal(movedPos.toPoint()),
        Qt::LeftButton,
        Qt::NoButton,
        Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &releaseEvent);
    QVERIFY(!window.m_regionLayoutManager->isDragging());

    const QSize expandedLayoutSize = window.m_regionLayoutManager->canvasBounds().size();
    const QRect controls = window.regionLayoutControlsRect(expandedLayoutSize);
    const QRect buttonsGlobal = RegionLayoutRenderer::confirmButtonRect(controls)
                                    .united(RegionLayoutRenderer::cancelButtonRect(controls))
                                    .translated(window.pos());
    QVERIFY(available.contains(buttonsGlobal));

    window.exitRegionLayoutMode(false);
    QCOMPARE(window.pos(), originalPos);
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

void TestPinWindowCropUndo::testPreciseSourceSampleRectForRegion_UsesScreenLocalCoordinates()
{
    const QRect globalRegion(2025, 40, 100, 64);
    const QRect screenGeometry(1920, 0, 1536, 864);

    const QRectF sampleRect =
        PinWindow::preciseSourceSampleRectForRegion(globalRegion, screenGeometry, 1.25);

    QCOMPARE(sampleRect, QRectF(0.25, 0.0, 125.0, 80.0));
}

void TestPinWindowCropUndo::testDisplaySourceRectForTarget_PrefersTranslationOverScaling()
{
    const QRectF sampleRect(0.5, 0.5, 958.75, 421.25);
    const QRectF resolved = PinWindow::displaySourceRectForTarget(
        sampleRect,
        QSize(960, 422),
        QSize(959, 421));

    QCOMPARE(resolved, QRectF(1.0, 1.0, 959.0, 421.0));
}

void TestPinWindowCropUndo::testSetSourceRegion_FractionalDpr_PreservesExactLogicalSize()
{
    QPixmap source = createPatternPixmap(126, 80, 1.25);
    PinWindow window(source, QPoint(0, 0), nullptr, false);

    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen != nullptr);

    const QRect sourceRegion(25, 40, 100, 64);
    window.setSourceRegion(sourceRegion, screen);

    QCOMPARE(window.m_contentLogicalSize, sourceRegion.size());
    QCOMPARE(window.size(), sourceRegion.size());
    QCOMPARE(window.cropToolImageSize(), sourceRegion.size());
    QCOMPARE(window.m_sourceSampleRect, QRectF(0.25, 0.0, 125.0, 80.0));
    QCOMPARE(window.m_displayPixmap.size(), QSize(125, 80));
    QVERIFY(qFuzzyCompare(window.m_displayPixmap.devicePixelRatio(), 1.25));
}

void TestPinWindowCropUndo::testCropUndoRedo_FractionalDpr_RestoresExactLogicalSize()
{
    QPixmap source = createPatternPixmap(126, 80, 1.25);
    PinWindow window(source, QPoint(0, 0), nullptr, false);

    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen != nullptr);

    const QRect sourceRegion(25, 40, 100, 64);
    window.setSourceRegion(sourceRegion, screen);

    const QRect cropRect(20, 10, 40, 30);
    window.applyCrop(cropRect);

    QCOMPARE(window.m_contentLogicalSize, cropRect.size());
    QCOMPARE(window.size(), cropRect.size());
    QCOMPARE(window.m_sourceSampleRect, QRectF(0.25, 0.5, 50.0, 37.5));

    window.undoCrop();
    QCOMPARE(window.m_contentLogicalSize, sourceRegion.size());
    QCOMPARE(window.size(), sourceRegion.size());
    QCOMPARE(window.m_sourceSampleRect, QRectF(0.25, 0.0, 125.0, 80.0));

    window.redoCrop();
    QCOMPARE(window.m_contentLogicalSize, cropRect.size());
    QCOMPARE(window.size(), cropRect.size());
    QCOMPARE(window.m_sourceSampleRect, QRectF(0.25, 0.5, 50.0, 37.5));
}

void TestPinWindowCropUndo::testHandleToolbarUndo_PrioritizesCropWhenNoPostCropAnnotations()
{
    QPixmap source = createPatternPixmap(220, 140);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
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
    window.showToolbar();
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

void TestPinWindowCropUndo::testHandleToolbarUndo_DoesNothingDuringHistoryLock()
{
    QPixmap source = createPatternPixmap(220, 140);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);

    window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
        QVector<QPoint>{QPoint(40, 35), QPoint(120, 55)},
        QColor(Qt::red), 3, LineEndStyle::None, LineStyle::Solid));

    const QRect cropRect(30, 20, 120, 80);
    const QPixmap expectedCropped = source.copy(cropRect);
    window.applyCrop(cropRect);
    QVERIFY(pixmapsEqual(window.m_originalPixmap, expectedCropped));

    window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
        QVector<QPoint>{QPoint(20, 20), QPoint(80, 45)},
        QColor(Qt::blue), 3, LineEndStyle::None, LineStyle::Solid));
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(2));

    const qsizetype cropUndoCount = window.m_cropUndoStack.size();
    window.m_annotationLayer->beginEraseTransaction();
    QVERIFY(window.m_annotationLayer->isHistoryLocked());

    window.handleToolbarUndo();

    QVERIFY(pixmapsEqual(window.m_originalPixmap, expectedCropped));
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(2));
    QCOMPARE(window.m_cropUndoStack.size(), cropUndoCount);

    QVERIFY(window.m_annotationLayer->endEraseTransaction());
    window.handleToolbarUndo();
    QVERIFY(pixmapsEqual(window.m_originalPixmap, expectedCropped));
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(1));
}

void TestPinWindowCropUndo::testHandleToolbarUndo_UsesStableCropBoundaryBeyondFiftyAnnotations()
{
    QPixmap source = createPatternPixmap(260, 180);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);

    // Keep enough annotations to cover the former 50-item content limit.
    for (int i = 0; i < 50; ++i) {
        const QVector<QPoint> points = {
            QPoint(20 + i, 20 + i),
            QPoint(80 + i, 40 + i)
        };
        window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
            points, QColor(Qt::red), 2, LineEndStyle::None, LineStyle::Solid));
    }
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(50));
    AnnotationItem* firstAnnotation = window.m_annotationLayer->itemAt(0);
    QVERIFY(firstAnnotation != nullptr);

    const QRect cropRect(30, 20, 160, 110);
    const QPixmap expectedCropped = source.copy(cropRect);
    window.applyCrop(cropRect);
    QVERIFY(pixmapsEqual(window.m_originalPixmap, expectedCropped));

    const QVector<QPoint> postCropPoints = {QPoint(18, 16), QPoint(70, 42)};
    window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
        postCropPoints, QColor(Qt::blue), 2, LineEndStyle::None, LineStyle::Solid));
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(51));
    QCOMPARE(window.m_annotationLayer->itemAt(0), firstAnnotation);

    window.handleToolbarUndo(); // Undo post-crop annotation first.
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(50));
    QCOMPARE(window.m_annotationLayer->itemAt(0), firstAnnotation);
    QVERIFY(pixmapsEqual(window.m_originalPixmap, expectedCropped));

    window.handleToolbarUndo(); // Then undo crop, not pre-crop annotation.
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(50));
    QCOMPARE(window.m_annotationLayer->itemAt(0), firstAnnotation);
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

void TestPinWindowCropUndo::testHandleToolbarRedo_DoesNothingDuringHistoryLock()
{
    QPixmap source = createPatternPixmap(220, 140);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);

    window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
        QVector<QPoint>{QPoint(40, 35), QPoint(120, 55)},
        QColor(Qt::red), 3, LineEndStyle::None, LineStyle::Solid));

    const QRect cropRect(30, 20, 120, 80);
    const QPixmap expectedCropped = source.copy(cropRect);
    window.applyCrop(cropRect);
    window.handleToolbarUndo();
    QVERIFY(pixmapsEqual(window.m_originalPixmap, source));
    QVERIFY(!window.m_cropRedoStack.isEmpty());

    const qsizetype cropRedoCount = window.m_cropRedoStack.size();
    window.m_annotationLayer->beginEraseTransaction();
    QVERIFY(window.m_annotationLayer->isHistoryLocked());

    window.handleToolbarRedo();

    QVERIFY(pixmapsEqual(window.m_originalPixmap, source));
    QCOMPARE(window.m_cropRedoStack.size(), cropRedoCount);
    QVERIFY(window.m_cropUndoStack.isEmpty());

    QVERIFY(window.m_annotationLayer->endEraseTransaction());
    window.handleToolbarRedo();
    QVERIFY(pixmapsEqual(window.m_originalPixmap, expectedCropped));
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
    window.showToolbar();
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

void TestPinWindowCropUndo::testHandleToolbarRedo_UsesStableCropBoundaryBeyondFiftyAnnotations()
{
    QPixmap source = createPatternPixmap(260, 180);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);

    // Keep enough annotations to cover the former 50-item content limit.
    for (int i = 0; i < 50; ++i) {
        const QVector<QPoint> points = {
            QPoint(20 + i, 20 + i),
            QPoint(80 + i, 40 + i)
        };
        window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
            points, QColor(Qt::red), 2, LineEndStyle::None, LineStyle::Solid));
    }
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(50));
    AnnotationItem* firstAnnotation = window.m_annotationLayer->itemAt(0);
    QVERIFY(firstAnnotation != nullptr);

    const QRect cropRect(30, 20, 160, 110);
    const QPixmap expectedCropped = source.copy(cropRect);
    window.applyCrop(cropRect);

    const QVector<QPoint> postCropPoints = {QPoint(18, 16), QPoint(70, 42)};
    window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
        postCropPoints, QColor(Qt::blue), 2, LineEndStyle::None, LineStyle::Solid));
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(51));
    QCOMPARE(window.m_annotationLayer->itemAt(0), firstAnnotation);

    window.handleToolbarUndo(); // Undo post-crop annotation
    window.handleToolbarUndo(); // Undo crop
    QVERIFY(pixmapsEqual(window.m_originalPixmap, source));
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(50));
    QCOMPARE(window.m_annotationLayer->itemAt(0), firstAnnotation);

    window.handleToolbarRedo(); // Redo crop first when boundary matches via top item.
    QVERIFY(pixmapsEqual(window.m_originalPixmap, expectedCropped));
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(50));
    QCOMPARE(window.m_annotationLayer->itemAt(0), firstAnnotation);

    window.handleToolbarRedo(); // Then redo post-crop annotation.
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(51));
    QCOMPARE(window.m_annotationLayer->itemAt(0), firstAnnotation);
}

void TestPinWindowCropUndo::testHandleToolbarUndo_InPlaceCacheInvalidationDoesNotPreemptCropUndo()
{
    QPixmap source = createPatternPixmap(220, 140);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
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

void TestPinWindowCropUndo::testHandleToolbarUndo_NonTopRemovalPrecedesCropUndo()
{
    QPixmap source = createPatternPixmap(220, 140);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);

    window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
        QVector<QPoint>{QPoint(40, 35), QPoint(100, 45)},
        QColor(Qt::red), 3, LineEndStyle::None, LineStyle::Solid));
    window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
        QVector<QPoint>{QPoint(50, 55), QPoint(120, 65)},
        QColor(Qt::blue), 3, LineEndStyle::None, LineStyle::Solid));
    AnnotationItem* topItem = window.m_annotationLayer->itemAt(1);

    const QRect cropRect(30, 20, 120, 80);
    const QPixmap expectedCropped = source.copy(cropRect);
    window.applyCrop(cropRect);

    // Removing a non-top item keeps the old top pointer unchanged. History
    // state, rather than scene shape, must still put this undo before crop.
    window.m_annotationLayer->setSelectedIndex(0);
    QVERIFY(window.m_annotationLayer->removeSelectedItem());
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(1));
    QCOMPARE(window.m_annotationLayer->itemAt(0), topItem);

    window.handleToolbarUndo();
    QVERIFY(pixmapsEqual(window.m_originalPixmap, expectedCropped));
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(2));
    QCOMPARE(window.m_annotationLayer->itemAt(1), topItem);

    window.handleToolbarUndo();
    QVERIFY(pixmapsEqual(window.m_originalPixmap, source));
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(2));
}

void TestPinWindowCropUndo::testHandleToolbarRedo_NewAnnotationBranchInvalidatesCropRedo()
{
    QPixmap source = createPatternPixmap(220, 140);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);

    window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
        QVector<QPoint>{QPoint(40, 35), QPoint(120, 55)},
        QColor(Qt::red), 3, LineEndStyle::None, LineStyle::Solid));

    const QRect cropRect(30, 20, 120, 80);
    window.applyCrop(cropRect);
    window.handleToolbarUndo();
    QVERIFY(pixmapsEqual(window.m_originalPixmap, source));
    QVERIFY(!window.m_cropRedoStack.isEmpty());

    // The old crop boundary remains annotation-undo-reachable, but the new
    // branch must invalidate crop redo rather than reapply it out of order.
    window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
        QVector<QPoint>{QPoint(20, 20), QPoint(80, 45)},
        QColor(Qt::blue), 3, LineEndStyle::None, LineStyle::Solid));
    window.handleToolbarRedo();

    QVERIFY(pixmapsEqual(window.m_originalPixmap, source));
    QVERIFY(window.m_cropRedoStack.isEmpty());
    QCOMPARE(window.m_annotationLayer->itemCount(), static_cast<size_t>(2));
}

void TestPinWindowCropUndo::testHistoryFloorInvalidatesUnreachableCropBoundary()
{
    QPixmap source = createPatternPixmap(260, 180);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);

    const QRect cropRect(30, 20, 160, 110);
    const QPixmap expectedCropped = source.copy(cropRect);
    window.applyCrop(cropRect);
    QVERIFY(!window.m_cropUndoStack.isEmpty());

    for (std::size_t i = 0; i <= AnnotationLayer::kMaxHistoryCommands; ++i) {
        const int offset = static_cast<int>(i % 20);
        window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
            QVector<QPoint>{QPoint(10 + offset, 10), QPoint(60 + offset, 30)},
            QColor(Qt::red), 2, LineEndStyle::None, LineStyle::Solid));
    }
    QCOMPARE(window.m_annotationLayer->historyCommandCount(),
             AnnotationLayer::kMaxHistoryCommands);
    QCOMPARE(window.m_annotationLayer->historyStateRelation(
                 window.m_cropUndoStack.constLast().annotationHistoryState),
             AnnotationLayer::HistoryStateRelation::Unreachable);

    window.handleToolbarUndo();

    // Committed post-crop annotations make the old crop unsafe to undo. The
    // crop entry is discarded while the latest annotation remains undoable.
    QVERIFY(pixmapsEqual(window.m_originalPixmap, expectedCropped));
    QVERIFY(window.m_cropUndoStack.isEmpty());
    QCOMPARE(window.m_annotationLayer->itemCount(),
             AnnotationLayer::kMaxHistoryCommands);
}

QTEST_MAIN(TestPinWindowCropUndo)
#include "tst_CropUndo.moc"
