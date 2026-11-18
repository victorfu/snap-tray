#include <QtTest/QtTest>
#include <QGuiApplication>
#include <QImage>
#include <QScreen>

#include "annotations/MosaicBlurType.h"
#include "PinWindow.h"

#include "annotations/AnnotationLayer.h"
#include "annotations/MosaicRectAnnotation.h"
#include "annotations/MosaicStroke.h"
#include "annotations/PolylineAnnotation.h"
#include "pinwindow/ImageStripTransform.h"
#include "pinwindow/RegionLayoutManager.h"
#include "qml/PinToolOptionsViewModel.h"
#include "qml/QmlFloatingSubToolbar.h"
#include "tools/ToolManager.h"
#include "tools/handlers/CutOutToolHandler.h"

using ImageStrip::Axis;
using ImageStrip::Operation;

namespace {

QImage patternImage(int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            image.setPixelColor(x, y, QColor(x % 256, y % 256, (x * 3 + y * 5) % 256, 255));
        }
    }
    return image;
}

QPixmap patternPixmap(int width, int height, qreal dpr = 1.0)
{
    QPixmap pixmap = QPixmap::fromImage(patternImage(width, height));
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

ImageStrip::Request strip(Axis axis, Operation operation, int start, int length)
{
    ImageStrip::Request request;
    request.axis = axis;
    request.operation = operation;
    request.start = start;
    request.length = length;
    return request;
}

QImage argb(const QImage& image)
{
    QImage copy = image.convertToFormat(QImage::Format_ARGB32);
    copy.setDevicePixelRatio(1.0);
    return copy;
}

void dragTool(ToolManager* manager, const QPointF& from, const QPointF& to)
{
    manager->handleMousePress(from);
    manager->handleMouseMove((from + to) / 2.0);
    manager->handleMouseMove(to);
    manager->handleMouseRelease(to);
}

void pressKey(PinWindow& window, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QKeyEvent event(QEvent::KeyPress, key, modifiers);
    QCoreApplication::sendEvent(&window, &event);
}

std::unique_ptr<PolylineAnnotation> line(const QPoint& a, const QPoint& b)
{
    return std::make_unique<PolylineAnnotation>(
        QVector<QPoint>{a, b}, QColor(Qt::red), 3, LineEndStyle::None, LineStyle::Solid);
}

QVector<QPoint> pointsOf(AnnotationLayer* layer, int index)
{
    auto* polyline = dynamic_cast<PolylineAnnotation*>(layer->itemAt(index));
    return polyline ? polyline->points() : QVector<QPoint>();
}

}  // namespace

class TestPinWindowStripCutOut : public QObject
{
    Q_OBJECT

private slots:
    void removeAndInsert_PixelsAndSize_data();
    void removeAndInsert_PixelsAndSize();
    void repeatedOperations_NoSizeDrift_data();
    void repeatedOperations_NoSizeDrift();
    void rotatedPin_StripFollowsSourcePixels();
    void annotationsBeforeStayAndAfterShift();
    void zoomedPin_ShiftsAnnotationsInViewUnits();
    void crossingAnnotationIsRefusedAndNothingChanges_data();
    void crossingAnnotationIsRefusedAndNothingChanges();
    void redactionKeepsCoveringTheSamePixels();
    void repeatedSmallStrips_RedactionNeverDrifts_data();
    void repeatedSmallStrips_RedactionNeverDrifts();
    void inexactLengthOrZoomIsRefused();
    void undoneRedactionAcrossInsertionLineIsRefused();
    void openGestureIsRefused();
    void viewScaleNeverChangesUnderAnnotations_data();
    void viewScaleNeverChangesUnderAnnotations();
    void undoneStrokeAcrossRemovedStripIsRefused();
    void halfPixelSampleOrigin_RemovesTheRowsShown();
    void undoRedoRestoresEverythingAtOnce();
    void undoOrderWithLaterAnnotations();
    void invalidStripsLeaveThePinUntouched();
    void liveUpdateIsRefused();
    void toolDraftAppliesWithEnterAndCancelsWithEscape();
    void toolDraftIsDiscardedOnLeavingOrReplacedImage();
    void toolArrowKeysMoveTheBandAlongItsAxisOnScreen();
    void subToolbarShowsOperationDirectionAndResult();
    void exportMatchesWhatIsShown();
};

void TestPinWindowStripCutOut::removeAndInsert_PixelsAndSize_data()
{
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<int>("axis");
    QTest::addColumn<int>("operation");
    for (const qreal dpr : {1.0, 1.25, 1.5, 2.0}) {
        for (const Axis axis : {Axis::Rows, Axis::Columns}) {
            for (const Operation operation : {Operation::Remove, Operation::Insert}) {
                const QString name = QStringLiteral("%1x %2 %3")
                    .arg(dpr)
                    .arg(axis == Axis::Rows ? QStringLiteral("rows") : QStringLiteral("columns"))
                    .arg(operation == Operation::Remove ? QStringLiteral("remove") : QStringLiteral("insert"));
                QTest::newRow(qPrintable(name)) << dpr << int(axis) << int(operation);
            }
        }
    }
}

void TestPinWindowStripCutOut::removeAndInsert_PixelsAndSize()
{
    QFETCH(qreal, dpr);
    QFETCH(int, axis);
    QFETCH(int, operation);

    const QPixmap source = patternPixmap(300, 240, dpr);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    QCOMPARE(window.cropSourcePixelSize(), QSize(300, 240));

    // 60 device pixels is a whole number of logical pixels at every ratio here.
    const ImageStrip::Request request = strip(Axis(axis), Operation(operation), 90, 60);
    const QImage expected = ImageStrip::apply(source.toImage(), request).image;
    QVERIFY(!expected.isNull());

    QVERIFY(window.applyStrip(request));
    QCOMPARE(argb(window.m_originalPixmap.toImage()), argb(expected));
    QCOMPARE(window.m_originalPixmap.devicePixelRatio(), dpr);
    QCOMPARE(window.cropSourcePixelSize(), expected.size());
    // The logical size is the pixel size divided by the ratio, nothing else.
    const QSize logical(qRound(expected.width() / dpr), qRound(expected.height() / dpr));
    QCOMPARE(window.m_contentLogicalSize, logical);
    QCOMPARE(window.size(), logical);
    QCOMPARE(window.m_cropUndoStack.size(), 1);
}

void TestPinWindowStripCutOut::repeatedOperations_NoSizeDrift_data()
{
    QTest::addColumn<qreal>("dpr");
    QTest::newRow("1x") << 1.0;
    QTest::newRow("1.25x") << 1.25;
    QTest::newRow("1.5x") << 1.5;
    QTest::newRow("2x") << 2.0;
}

void TestPinWindowStripCutOut::repeatedOperations_NoSizeDrift()
{
    QFETCH(qreal, dpr);
    const QPixmap source = patternPixmap(300, 240, dpr);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    const QSize logicalBefore = window.size();

    for (int round = 0; round < 8; ++round) {
        QVERIFY(window.applyStrip(strip(Axis::Rows, Operation::Insert, 30, 60)));
        QVERIFY(window.applyStrip(strip(Axis::Columns, Operation::Insert, 150, 30)));
        QCOMPARE(window.cropSourcePixelSize(), QSize(330, 300));
        QVERIFY(window.applyStrip(strip(Axis::Columns, Operation::Remove, 150, 30)));
        QVERIFY(window.applyStrip(strip(Axis::Rows, Operation::Remove, 30, 60)));
    }
    QCOMPARE(window.cropSourcePixelSize(), QSize(300, 240));
    QCOMPARE(window.size(), logicalBefore);
    QCOMPARE(window.m_sourceSampleRect, QRectF(0, 0, 300, 240));
    QCOMPARE(argb(window.m_originalPixmap.toImage()), argb(source.toImage()));
}

void TestPinWindowStripCutOut::rotatedPin_StripFollowsSourcePixels()
{
    const QPixmap source = patternPixmap(200, 120);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.rotateRight();
    window.showToolbar();
    window.handleToolbarToolSelected(static_cast<int>(ToolId::CutOut));
    CutOutToolHandler* tool = window.cutOutToolHandler();
    QVERIFY(tool);

    // A horizontal band on screen is a band of source columns after a quarter turn.
    tool->setViewAxis(Axis::Rows);
    QCOMPARE(tool->viewAxis(), Axis::Rows);
    QCOMPARE(tool->request().axis, Axis::Columns);

    const QImage shownBefore = window.getExportPixmapCore(false, true).toImage();
    QCOMPARE(shownBefore.size(), QSize(120, 200));
    // Mark the on-screen rows 50..90.
    dragTool(window.m_toolManager,
             window.mapToOriginalCoords(QPointF(60, 50)),
             window.mapToOriginalCoords(QPointF(60, 90)));
    QVERIFY(tool->hasDraft());
    QCOMPARE(tool->request().length, 40);
    QVERIFY(tool->commitDraft());

    const QImage shownAfter = window.getExportPixmapCore(false, true).toImage();
    QCOMPARE(shownAfter.size(), QSize(120, 160));
    QCOMPARE(window.m_rotationAngle, 90);
    // What is shown is the old view without its rows 50..90.
    QCOMPARE(argb(shownAfter.copy(0, 0, 120, 50)), argb(shownBefore.copy(0, 0, 120, 50)));
    QCOMPARE(argb(shownAfter.copy(0, 50, 120, 110)), argb(shownBefore.copy(0, 90, 120, 110)));
}

void TestPinWindowStripCutOut::annotationsBeforeStayAndAfterShift()
{
    PinWindow window(patternPixmap(300, 240), QPoint(0, 0), nullptr, false);
    window.showToolbar();
    AnnotationLayer* layer = window.m_annotationLayer;
    layer->addItem(line(QPoint(20, 20), QPoint(120, 60)));     // above the strip
    layer->addItem(line(QPoint(30, 170), QPoint(200, 220)));   // below it

    QVERIFY(window.applyStrip(strip(Axis::Rows, Operation::Remove, 80, 70)));
    QCOMPARE(pointsOf(layer, 0), (QVector<QPoint>{QPoint(20, 20), QPoint(120, 60)}));
    QCOMPARE(pointsOf(layer, 1), (QVector<QPoint>{QPoint(30, 100), QPoint(200, 150)}));

    // Inserting space pushes what lies after the line the other way.
    QVERIFY(window.applyStrip(strip(Axis::Rows, Operation::Insert, 80, 25)));
    QCOMPARE(pointsOf(layer, 0), (QVector<QPoint>{QPoint(20, 20), QPoint(120, 60)}));
    QCOMPARE(pointsOf(layer, 1), (QVector<QPoint>{QPoint(30, 125), QPoint(200, 175)}));

    // Columns move along x.
    QVERIFY(window.applyStrip(strip(Axis::Columns, Operation::Remove, 210, 40)));
    QCOMPARE(pointsOf(layer, 1), (QVector<QPoint>{QPoint(30, 125), QPoint(200, 175)}));
    QVERIFY(window.applyStrip(strip(Axis::Columns, Operation::Insert, 0, 10)));
    QCOMPARE(pointsOf(layer, 0), (QVector<QPoint>{QPoint(30, 20), QPoint(130, 60)}));
}

void TestPinWindowStripCutOut::zoomedPin_ShiftsAnnotationsInViewUnits()
{
    // A 2x image shown at 150%: one source pixel is 0.75 view units.
    PinWindow window(patternPixmap(400, 320, 2.0), QPoint(0, 0), nullptr, false);
    window.setZoomLevel(1.5);
    window.showToolbar();
    QCOMPARE(window.size(), QSize(300, 240));
    AnnotationLayer* layer = window.m_annotationLayer;
    layer->addItem(line(QPoint(20, 200), QPoint(100, 220)));

    // Rows 80..160 of the source are view rows 60..120.
    QVERIFY(window.applyStrip(strip(Axis::Rows, Operation::Remove, 80, 80)));
    QCOMPARE(window.size(), QSize(300, 180));
    QCOMPARE(pointsOf(layer, 0), (QVector<QPoint>{QPoint(20, 140), QPoint(100, 160)}));
}

void TestPinWindowStripCutOut::crossingAnnotationIsRefusedAndNothingChanges_data()
{
    QTest::addColumn<int>("operation");
    QTest::addColumn<bool>("redaction");
    QTest::newRow("stroke across a removed strip") << int(Operation::Remove) << false;
    QTest::newRow("redaction across a removed strip") << int(Operation::Remove) << true;
    QTest::newRow("stroke across an insertion line") << int(Operation::Insert) << false;
    QTest::newRow("redaction across an insertion line") << int(Operation::Insert) << true;
}

void TestPinWindowStripCutOut::crossingAnnotationIsRefusedAndNothingChanges()
{
    QFETCH(int, operation);
    QFETCH(bool, redaction);

    const QPixmap source = patternPixmap(300, 240);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    window.handleToolbarToolSelected(static_cast<int>(ToolId::CutOut));
    AnnotationLayer* layer = window.m_annotationLayer;
    if (redaction) {
        layer->addItem(std::make_unique<MosaicRectAnnotation>(
            QRect(60, 90, 120, 60), window.m_sharedSourcePixmap, 12, MosaicBlurType::Pixelate));
    } else {
        layer->addItem(line(QPoint(40, 60), QPoint(200, 180)));
    }
    const QRect boundsBefore = layer->itemAt(0)->boundingRect();
    const QImage exportBefore = window.getExportPixmapWithAnnotations().toImage();
    const auto historyBefore = layer->historyStateToken();

    QVERIFY(!window.applyStrip(strip(Axis::Rows, Operation(operation), 100, 40)));

    // Refused as a whole: image, size, annotation, history and tool as before.
    QCOMPARE(argb(window.m_originalPixmap.toImage()), argb(source.toImage()));
    QCOMPARE(window.size(), QSize(300, 240));
    QCOMPARE(layer->itemAt(0)->boundingRect(), boundsBefore);
    QCOMPARE(layer->historyStateToken(), historyBefore);
    QVERIFY(!window.canUndoCrop());
    QCOMPARE(window.getExportPixmapWithAnnotations().toImage(), exportBefore);
    QVERIFY(window.isCutOutToolActive());

    // "Out of the way" includes Redo: undone, the annotation could still come
    // back whole on one side of the strip. A new edit discards that redo.
    layer->undo();
    QVERIFY(!window.applyStrip(strip(Axis::Rows, Operation(operation), 100, 40)));
    QCOMPARE(window.size(), QSize(300, 240));
    layer->addItem(line(QPoint(10, 10), QPoint(40, 20)));
    QVERIFY(window.applyStrip(strip(Axis::Rows, Operation(operation), 100, 40)));
}

void TestPinWindowStripCutOut::redactionKeepsCoveringTheSamePixels()
{
    // A block of fine detail stands in for something private.
    QImage image = patternImage(300, 240);
    const QRect secret(60, 168, 96, 48);
    for (int y = secret.top(); y <= secret.bottom(); ++y) {
        for (int x = secret.left(); x <= secret.right(); ++x) {
            image.setPixelColor(x, y, ((x + y) % 2) ? QColor(Qt::black) : QColor(Qt::white));
        }
    }
    const QPixmap source = QPixmap::fromImage(image);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    window.m_annotationLayer->addItem(std::make_unique<MosaicRectAnnotation>(
        secret, window.m_sharedSourcePixmap, 12, MosaicBlurType::Pixelate));

    const QImage exportBefore = argb(window.getExportPixmapWithAnnotations().toImage());
    QVERIFY(exportBefore.copy(secret) != argb(image).copy(secret));   // it is hidden

    // Remove 72 rows above it: the block and its redaction move up together.
    QVERIFY(window.applyStrip(strip(Axis::Rows, Operation::Remove, 48, 72)));
    const QRect moved = secret.translated(0, -72);
    QCOMPARE(window.m_annotationLayer->itemAt(0)->boundingRect().topLeft(), moved.topLeft());

    const QImage exportAfter = argb(window.getExportPixmapWithAnnotations().toImage());
    const QImage bare = argb(window.m_originalPixmap.toImage());
    QCOMPARE(bare.copy(moved), argb(image).copy(secret));              // the pixels did move there
    QVERIFY2(exportAfter.copy(moved) != bare.copy(moved), "The private pixels are exposed");
    QCOMPARE(exportAfter.copy(moved), exportBefore.copy(secret));      // hidden exactly as before

    // Undo puts image and redaction back together.
    window.handleToolbarUndo();
    QCOMPARE(argb(window.getExportPixmapWithAnnotations().toImage()), exportBefore);
}

// Review of #371: a shift that is a fraction of a view unit was rounded by the
// redaction, a little further off its pixels with every strip.
void TestPinWindowStripCutOut::repeatedSmallStrips_RedactionNeverDrifts_data()
{
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<qreal>("zoom");
    QTest::newRow("1x 100%") << 1.0 << 1.0;
    QTest::newRow("1.25x 100%") << 1.25 << 1.0;
    QTest::newRow("1.5x 100%") << 1.5 << 1.0;
    QTest::newRow("2x 100%") << 2.0 << 1.0;
    QTest::newRow("1x 150%") << 1.0 << 1.5;
    QTest::newRow("2x 150%") << 2.0 << 1.5;
    QTest::newRow("1x 200%") << 1.0 << 2.0;
}

void TestPinWindowStripCutOut::repeatedSmallStrips_RedactionNeverDrifts()
{
    QFETCH(qreal, dpr);
    QFETCH(qreal, zoom);

    // Private detail in source rows 240..299 of a 300 x 360 px image.
    QImage image = patternImage(300, 360);
    const QRect secret(60, 240, 120, 60);
    for (int y = secret.top(); y <= secret.bottom(); ++y) {
        for (int x = secret.left(); x <= secret.right(); ++x) {
            image.setPixelColor(x, y, ((x + y) % 2) ? QColor(Qt::black) : QColor(Qt::white));
        }
    }
    QPixmap source = QPixmap::fromImage(image);
    source.setDevicePixelRatio(dpr);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.setZoomLevel(zoom);
    window.showToolbar();
    window.handleToolbarToolSelected(static_cast<int>(ToolId::CutOut));
    CutOutToolHandler* tool = window.cutOutToolHandler();
    QVERIFY(tool->canMarkStrip());

    const qreal viewPerPixel = static_cast<qreal>(window.height()) / 360;
    const auto viewRect = [&](const QRect& pixels) {
        return QRectF(pixels.x() * viewPerPixel, pixels.y() * viewPerPixel,
                      pixels.width() * viewPerPixel, pixels.height() * viewPerPixel).toRect();
    };
    const QRect mosaicRect = viewRect(secret);
    QCOMPARE(QRectF(mosaicRect), QRectF(secret.x() * viewPerPixel, secret.y() * viewPerPixel,
                                        secret.width() * viewPerPixel, secret.height() * viewPerPixel));
    window.m_annotationLayer->addItem(std::make_unique<MosaicRectAnnotation>(
        mosaicRect, window.m_sharedSourcePixmap, 12, MosaicBlurType::Pixelate));

    // The smallest strip the tool offers here, above the secret, ten times.
    const int step = ImageStrip::lengthStep(dpr, viewPerPixel);
    QVERIFY(step > 0);
    int removed = 0;
    for (int round = 0; round < 10; ++round) {
        QVERIFY(window.applyStrip(strip(Axis::Rows, Operation::Remove, 10, step)));
        removed += step;
        // The redaction sits exactly on the rows the secret moved to.
        const QRect expected = viewRect(secret.translated(0, -removed));
        QCOMPARE(window.m_annotationLayer->itemAt(0)->boundingRect(), expected);
        // And the view scale has not changed underneath it.
        QCOMPARE(static_cast<qreal>(window.height()) / (360 - removed), viewPerPixel);
    }
    QCOMPARE(argb(window.m_originalPixmap.toImage()).copy(secret.translated(0, -removed)),
             argb(image).copy(secret));

    if (qFuzzyCompare(zoom, 1.0)) {
        // Shown 1:1, a bare secret pixel is pure black or white; none may show.
        const QImage exported = argb(window.getExportPixmapCore(false, true).toImage());
        const QImage redacted = argb(window.getExportPixmapWithAnnotations().toImage());
        const QRect moved = secret.translated(0, -removed);
        int bare = 0;
        for (int y = moved.top(); y <= moved.bottom(); ++y) {
            for (int x = moved.left(); x <= moved.right(); ++x) {
                const QColor shown = redacted.pixelColor(x, y);
                const bool pure = (shown == QColor(Qt::black) || shown == QColor(Qt::white));
                if (pure && shown == exported.pixelColor(x, y)) ++bare;
            }
        }
        QVERIFY2(bare == 0, qPrintable(QStringLiteral("%1 private pixels are exposed").arg(bare)));
    }
}

void TestPinWindowStripCutOut::inexactLengthOrZoomIsRefused()
{
    // 1.25x: only multiples of 5 px are whole logical pixels.
    const QPixmap source = patternPixmap(300, 240, 1.25);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    for (const int length : {1, 3, 12}) {
        QVERIFY(!window.applyStrip(strip(Axis::Rows, Operation::Remove, 10, length)));
        QCOMPARE(argb(window.m_originalPixmap.toImage()), argb(source.toImage()));
        QVERIFY(!window.canUndoCrop());
    }
    QVERIFY(window.applyStrip(strip(Axis::Rows, Operation::Remove, 10, 5)));
    window.handleToolbarUndo();

    // An odd zoom has no exact length at all: the tool says so and marks nothing.
    window.setZoomLevel(1.37);
    window.handleToolbarToolSelected(static_cast<int>(ToolId::CutOut));
    CutOutToolHandler* tool = window.cutOutToolHandler();
    QVERIFY(!tool->canMarkStrip());
    QVERIFY(!window.m_subToolbar->viewModel()->cutOutHintText().isEmpty());
    const QSize toolSize = window.cropToolImageSize();
    dragTool(window.m_toolManager, QPointF(toolSize.width() / 2.0, 30), QPointF(toolSize.width() / 2.0, 150));
    QVERIFY(!tool->hasDraft());
    QVERIFY(!window.applyStrip(strip(Axis::Rows, Operation::Remove, 10, 5)));
    QCOMPARE(argb(window.m_originalPixmap.toImage()), argb(source.toImage()));

    // Back at 100% it works again.
    window.setZoomLevel(1.0);
    QVERIFY(tool->canMarkStrip());
    QVERIFY(window.m_subToolbar->viewModel()->cutOutHintText().isEmpty());
}

void TestPinWindowStripCutOut::undoneRedactionAcrossInsertionLineIsRefused()
{
    const QPixmap source = patternPixmap(300, 240);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    AnnotationLayer* layer = window.m_annotationLayer;
    layer->addItem(std::make_unique<MosaicRectAnnotation>(
        QRect(60, 100, 120, 60), window.m_sharedSourcePixmap, 12, MosaicBlurType::Pixelate));
    layer->undo();   // not visible, but Redo brings it back whole
    QCOMPARE(layer->itemCount(), size_t(0));

    QVERIFY(!window.applyStrip(strip(Axis::Rows, Operation::Insert, 120, 50)));
    QCOMPARE(argb(window.m_originalPixmap.toImage()), argb(source.toImage()));
    QVERIFY(!window.canUndoCrop());

    // An insertion clear of it is fine, and the redone redaction lands on its pixels.
    QVERIFY(window.applyStrip(strip(Axis::Rows, Operation::Insert, 40, 50)));
    layer->redo();
    QCOMPARE(layer->itemAt(0)->boundingRect(), QRect(60, 150, 120, 60));
}

void TestPinWindowStripCutOut::openGestureIsRefused()
{
    const QPixmap source = patternPixmap(300, 240);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    AnnotationLayer* layer = window.m_annotationLayer;
    layer->addItem(line(QPoint(20, 200), QPoint(90, 220)));
    QVERIFY(layer->beginGesture(layer->idAt(0)));
    QVERIFY(layer->isGestureActive());

    // The layer would not move its items now, so the image must not move either.
    QVERIFY(!window.applyStrip(strip(Axis::Rows, Operation::Remove, 60, 90)));
    QCOMPARE(argb(window.m_originalPixmap.toImage()), argb(source.toImage()));
    QCOMPARE(pointsOf(layer, 0), (QVector<QPoint>{QPoint(20, 200), QPoint(90, 220)}));
    layer->cancelGesture();
    QVERIFY(window.applyStrip(strip(Axis::Rows, Operation::Remove, 60, 90)));
}

// Second review of #371: with rounded sizes the pin could show the new image
// at a slightly different scale, while annotations were only shifted.
void TestPinWindowStripCutOut::viewScaleNeverChangesUnderAnnotations_data()
{
    QTest::addColumn<QSize>("pixels");
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<qreal>("zoom");
    QTest::addColumn<int>("operation");
    QTest::addColumn<int>("length");

    QTest::newRow("zoom a hair over 100%, insert") << QSize(800, 200) << 1.0 << 801.0 / 800.0 << int(Operation::Insert) << 600;
    QTest::newRow("odd height at 2x, insert") << QSize(400, 21) << 2.0 << 1.0 << int(Operation::Insert) << 42;
    QTest::newRow("tiny at 110%, insert") << QSize(400, 5) << 1.0 << 1.1 << int(Operation::Insert) << 100;
    QTest::newRow("odd height at 2x, remove") << QSize(400, 201) << 2.0 << 1.0 << int(Operation::Remove) << 42;
    QTest::newRow("plain, insert") << QSize(400, 200) << 1.0 << 1.0 << int(Operation::Insert) << 60;
    QTest::newRow("2x at 150%, remove") << QSize(400, 200) << 2.0 << 1.5 << int(Operation::Remove) << 40;
}

void TestPinWindowStripCutOut::viewScaleNeverChangesUnderAnnotations()
{
    QFETCH(QSize, pixels);
    QFETCH(qreal, dpr);
    QFETCH(qreal, zoom);
    QFETCH(int, operation);
    QFETCH(int, length);

    const QPixmap source = patternPixmap(pixels.width(), pixels.height(), dpr);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.setZoomLevel(zoom);
    window.showToolbar();
    const int toolBefore = window.cropToolImageSize().height();
    const int gridBefore = window.cropSourcePixelSize().height();
    const QSize sizeBefore = window.size();

    const bool applied = window.applyStrip(strip(Axis::Rows, Operation(operation), 0, length));
    if (!applied) {
        // Refusing is fine; changing the image is not.
        QCOMPARE(argb(window.m_originalPixmap.toImage()), argb(source.toImage()));
        QCOMPARE(window.size(), sizeBefore);
        QVERIFY(!window.canUndoCrop());
        return;
    }
    // Applied: the view grew or shrank by exactly the shift given to annotations.
    const int signedLength = Operation(operation) == Operation::Remove ? -length : length;
    const qreal shift = signedLength * static_cast<qreal>(toolBefore) / gridBefore;
    QCOMPARE(shift, static_cast<qreal>(qRound(shift)));
    QCOMPARE(window.cropToolImageSize().height(), toolBefore + qRound(shift));
}

void TestPinWindowStripCutOut::undoneStrokeAcrossRemovedStripIsRefused()
{
    // A stroke is not convex: no single shift keeps both of its ends covered.
    const QPixmap source = patternPixmap(300, 400);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    AnnotationLayer* layer = window.m_annotationLayer;
    layer->addItem(std::make_unique<MosaicStroke>(
        QVector<QPoint>{QPoint(40, 10), QPoint(40, 200), QPoint(200, 200), QPoint(200, 380)},
        window.m_sharedSourcePixmap, 24, 12, MosaicBlurType::Pixelate));
    const ImageStrip::Request removal = strip(Axis::Rows, Operation::Remove, 150, 100);

    QVERIFY(!window.applyStrip(removal));          // visible and crossing
    layer->undo();
    QCOMPARE(layer->itemCount(), size_t(0));
    QVERIFY2(!window.applyStrip(removal), "Redo would bring the stroke back off its pixels");
    QCOMPARE(argb(window.m_originalPixmap.toImage()), argb(source.toImage()));
    QVERIFY(!window.canUndoCrop());

    // A new edit discards the redo; then nothing can come back and the strip applies.
    layer->addItem(line(QPoint(10, 10), QPoint(30, 20)));
    QVERIFY(window.applyStrip(removal));
}

void TestPinWindowStripCutOut::halfPixelSampleOrigin_RemovesTheRowsShown()
{
    // A 1.5x region capture whose first shown row is device row 1 (origin 0.5).
    const QPixmap source = patternPixmap(150, 151, 1.5);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen != nullptr);
    window.setSourceRegion(QRect(0, 1, 100, 100).translated(screen->geometry().topLeft()), screen);
    window.showToolbar();
    QCOMPARE(window.cropSourcePixelSize(), QSize(150, 150));
    const QImage shownBefore = argb(window.getExportPixmapCore(false, true).toImage());
    QCOMPARE(shownBefore.size(), QSize(150, 150));

    QVERIFY(window.applyStrip(strip(Axis::Rows, Operation::Remove, 30, 30)));
    const QImage shownAfter = argb(window.getExportPixmapCore(false, true).toImage());
    QCOMPARE(shownAfter.size(), QSize(150, 120));
    // Exactly the shown rows 30..59 are gone: 29 stays, 60 follows it.
    QCOMPARE(shownAfter.copy(0, 0, 150, 30), shownBefore.copy(0, 0, 150, 30));
    QCOMPARE(shownAfter.copy(0, 30, 150, 90), shownBefore.copy(0, 60, 150, 90));
}

void TestPinWindowStripCutOut::undoRedoRestoresEverythingAtOnce()
{
    const QPixmap source = patternPixmap(300, 240, 1.5);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    QVector<LayoutRegion> regions(1);
    regions[0].rect = QRect(0, 0, 200, 160);
    regions[0].originalRect = regions[0].rect;
    regions[0].image = source.toImage();
    regions[0].index = 1;
    window.m_storedRegions = regions;
    window.m_hasMultiRegionData = true;
    window.showToolbar();
    AnnotationLayer* layer = window.m_annotationLayer;
    layer->addItem(line(QPoint(10, 10), QPoint(60, 30)));
    layer->addItem(line(QPoint(20, 120), QPoint(150, 150)));
    const QVector<QPoint> lowerBefore = pointsOf(layer, 1);
    const QSize sizeBefore = window.size();
    const QRectF sampleBefore = window.sourceSampleRect();
    const QImage exportBefore = window.getExportPixmapWithAnnotations().toImage();

    QVERIFY(window.applyStrip(strip(Axis::Rows, Operation::Remove, 60, 90)));
    const QImage exportAfter = window.getExportPixmapWithAnnotations().toImage();
    const QSize sizeAfter = window.size();
    QVERIFY(sizeAfter != sizeBefore);
    QVERIFY(!window.m_hasMultiRegionData);
    QCOMPARE(window.m_cropUndoStack.size(), 1);
    QVERIFY(pointsOf(layer, 1) != lowerBefore);

    // One undo step restores image, size, ratio, sample rect, layout data and annotations.
    window.handleToolbarUndo();
    QCOMPARE(argb(window.m_originalPixmap.toImage()), argb(source.toImage()));
    QCOMPARE(window.m_originalPixmap.devicePixelRatio(), 1.5);
    QCOMPARE(window.size(), sizeBefore);
    QCOMPARE(window.sourceSampleRect(), sampleBefore);
    QVERIFY(window.m_hasMultiRegionData);
    QCOMPARE(window.m_storedRegions.size(), 1);
    QCOMPARE(pointsOf(layer, 1), lowerBefore);
    QCOMPARE(layer->itemCount(), size_t(2));
    QCOMPARE(window.getExportPixmapWithAnnotations().toImage(), exportBefore);
    QVERIFY(!window.canUndoCrop());

    // And one redo step brings all of it back.
    window.handleToolbarRedo();
    QCOMPARE(window.size(), sizeAfter);
    QVERIFY(!window.m_hasMultiRegionData);
    QCOMPARE(window.getExportPixmapWithAnnotations().toImage(), exportAfter);

    window.handleToolbarUndo();
    window.handleToolbarRedo();
    QCOMPARE(window.getExportPixmapWithAnnotations().toImage(), exportAfter);
}

void TestPinWindowStripCutOut::undoOrderWithLaterAnnotations()
{
    PinWindow window(patternPixmap(300, 240), QPoint(0, 0), nullptr, false);
    window.showToolbar();
    AnnotationLayer* layer = window.m_annotationLayer;
    layer->addItem(line(QPoint(20, 200), QPoint(90, 220)));
    QVERIFY(window.applyStrip(strip(Axis::Rows, Operation::Remove, 60, 90)));
    layer->addItem(line(QPoint(10, 10), QPoint(40, 40)));   // drawn on the new image

    // The later annotation is undone first, then the strip.
    window.handleToolbarUndo();
    QCOMPARE(layer->itemCount(), size_t(1));
    QCOMPARE(window.size(), QSize(300, 150));
    window.handleToolbarUndo();
    QCOMPARE(window.size(), QSize(300, 240));
    QCOMPARE(pointsOf(layer, 0), (QVector<QPoint>{QPoint(20, 200), QPoint(90, 220)}));

    // Redo in the same order; an annotation redone after the strip sits on the new image.
    window.handleToolbarRedo();
    QCOMPARE(window.size(), QSize(300, 150));
    QCOMPARE(pointsOf(layer, 0), (QVector<QPoint>{QPoint(20, 110), QPoint(90, 130)}));
    window.handleToolbarRedo();
    QCOMPARE(layer->itemCount(), size_t(2));
    QCOMPARE(pointsOf(layer, 1), (QVector<QPoint>{QPoint(10, 10), QPoint(40, 40)}));
}

void TestPinWindowStripCutOut::invalidStripsLeaveThePinUntouched()
{
    const QPixmap source = patternPixmap(300, 240);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    window.m_annotationLayer->addItem(line(QPoint(20, 200), QPoint(90, 220)));
    const QImage exportBefore = window.getExportPixmapWithAnnotations().toImage();

    const ImageStrip::Request invalid[] = {
        strip(Axis::Rows, Operation::Remove, 0, 240),       // the whole image
        strip(Axis::Columns, Operation::Remove, 0, 300),
        strip(Axis::Rows, Operation::Remove, 200, 100),     // beyond the end
        strip(Axis::Rows, Operation::Remove, 50, 0),        // empty
        strip(Axis::Rows, Operation::Insert, 500, 10),      // outside
        strip(Axis::Rows, Operation::Insert, 10, 40000),    // far too large
        strip(Axis::Columns, Operation::Insert, 10, INT_MAX),
    };
    for (const ImageStrip::Request& request : invalid) {
        QVERIFY(!window.applyStrip(request));
        QCOMPARE(argb(window.m_originalPixmap.toImage()), argb(source.toImage()));
        QCOMPARE(window.size(), QSize(300, 240));
        QVERIFY(!window.canUndoCrop());
        QCOMPARE(window.getExportPixmapWithAnnotations().toImage(), exportBefore);
    }

    // The smallest strip still works.
    QVERIFY(window.applyStrip(strip(Axis::Rows, Operation::Remove, 239, 1)));
    QCOMPARE(window.size(), QSize(300, 239));
}

void TestPinWindowStripCutOut::liveUpdateIsRefused()
{
    const QPixmap source = patternPixmap(300, 240);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    window.m_isLiveMode = true;
    QVERIFY(!window.applyStrip(strip(Axis::Rows, Operation::Remove, 60, 90)));
    QCOMPARE(argb(window.m_originalPixmap.toImage()), argb(source.toImage()));
    QVERIFY(!window.canUndoCrop());
    window.m_isLiveMode = false;

    // A redaction pass still running would place its results on the old image.
    window.m_autoBlurInProgress = true;
    QVERIFY(!window.applyStrip(strip(Axis::Rows, Operation::Remove, 60, 90)));
    QCOMPARE(window.size(), QSize(300, 240));
    window.m_autoBlurInProgress = false;
    QVERIFY(window.applyStrip(strip(Axis::Rows, Operation::Remove, 60, 90)));
}

void TestPinWindowStripCutOut::toolDraftAppliesWithEnterAndCancelsWithEscape()
{
    const QPixmap source = patternPixmap(300, 240);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    window.handleToolbarToolSelected(static_cast<int>(ToolId::CutOut));
    QVERIFY(window.isCutOutToolActive());
    CutOutToolHandler* tool = window.cutOutToolHandler();
    QSignalSpy closedSpy(&window, &PinWindow::closed);

    // Marking a band changes nothing yet.
    dragTool(window.m_toolManager, QPointF(150, 60), QPointF(150, 150));
    QVERIFY(tool->hasDraft());
    QCOMPARE(tool->request().start, 60);
    QCOMPARE(tool->request().length, 90);
    QCOMPARE(argb(window.m_originalPixmap.toImage()), argb(source.toImage()));
    QVERIFY(!window.canUndoCrop());

    // First Escape drops the band and stays in the tool; the pin stays open.
    pressKey(window, Qt::Key_Escape);
    QVERIFY(!tool->hasDraft());
    QVERIFY(window.isCutOutToolActive());
    QCOMPARE(closedSpy.count(), 0);
    pressKey(window, Qt::Key_Return);   // nothing to apply
    QCOMPARE(window.size(), QSize(300, 240));

    // Escape in the middle of a drag only abandons the drag.
    dragTool(window.m_toolManager, QPointF(150, 60), QPointF(150, 150));
    window.m_toolManager->handleMousePress(QPointF(150, 100));
    window.m_toolManager->handleMouseMove(QPointF(150, 140));
    QVERIFY(tool->request().start != 60);
    pressKey(window, Qt::Key_Escape);
    QCOMPARE(tool->request().start, 60);
    window.m_toolManager->handleMouseRelease(QPointF(150, 140));
    QCOMPARE(tool->request().start, 60);

    // Enter applies once and leaves the tool.
    pressKey(window, Qt::Key_Return);
    QCOMPARE(window.size(), QSize(300, 150));
    QCOMPARE(window.m_cropUndoStack.size(), 1);
    QVERIFY(!window.isCutOutToolActive());
    QVERIFY(!tool->hasDraft());
    pressKey(window, Qt::Key_Return);
    QCOMPARE(window.m_cropUndoStack.size(), 1);

    // A double-click inside the band applies as well; outside it does nothing.
    window.handleToolbarToolSelected(static_cast<int>(ToolId::CutOut));
    dragTool(window.m_toolManager, QPointF(150, 20), QPointF(150, 60));
    auto doubleClick = [&window](const QPoint& pos) {
        QMouseEvent event(QEvent::MouseButtonDblClick, QPointF(pos), QPointF(window.mapToGlobal(pos)),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        window.mouseDoubleClickEvent(&event);
    };
    doubleClick(QPoint(150, 120));
    QCOMPARE(window.size(), QSize(300, 150));
    QCOMPARE(closedSpy.count(), 0);
    doubleClick(QPoint(150, 40));
    QCOMPARE(window.size(), QSize(300, 110));
}

void TestPinWindowStripCutOut::toolDraftIsDiscardedOnLeavingOrReplacedImage()
{
    PinWindow window(patternPixmap(300, 240), QPoint(0, 0), nullptr, false);
    window.showToolbar();
    window.handleToolbarToolSelected(static_cast<int>(ToolId::CutOut));
    CutOutToolHandler* tool = window.cutOutToolHandler();

    dragTool(window.m_toolManager, QPointF(150, 60), QPointF(150, 150));
    QVERIFY(tool->hasDraft());
    window.handleToolbarToolSelected(static_cast<int>(ToolId::Pencil));
    QVERIFY(!tool->hasDraft());
    pressKey(window, Qt::Key_Return);
    QCOMPARE(window.size(), QSize(300, 240));

    window.handleToolbarToolSelected(static_cast<int>(ToolId::CutOut));
    dragTool(window.m_toolManager, QPointF(150, 60), QPointF(150, 150));
    window.hideToolbarPreservingToolState();   // Space
    QVERIFY(!tool->hasDraft());

    // A band marked on one image never applies to another.
    window.showToolbar();
    window.handleToolbarToolSelected(static_cast<int>(ToolId::CutOut));
    QVERIFY(window.applyStrip(strip(Axis::Rows, Operation::Remove, 0, 40)));
    window.handleToolbarToolSelected(static_cast<int>(ToolId::CutOut));
    dragTool(window.m_toolManager, QPointF(150, 60), QPointF(150, 150));
    QVERIFY(tool->hasDraft());
    window.handleToolbarUndo();
    QCOMPARE(window.size(), QSize(300, 240));
    QVERIFY2(!tool->hasDraft(), "A band must not carry over to another image");
    QVERIFY(!window.canUndoCrop());
}

void TestPinWindowStripCutOut::toolArrowKeysMoveTheBandAlongItsAxisOnScreen()
{
    PinWindow window(patternPixmap(400, 240, 2.0), QPoint(0, 0), nullptr, false);
    window.rotateRight();
    window.showToolbar();
    window.handleToolbarToolSelected(static_cast<int>(ToolId::CutOut));
    CutOutToolHandler* tool = window.cutOutToolHandler();
    tool->setViewAxis(Axis::Rows);
    dragTool(window.m_toolManager,
             window.mapToOriginalCoords(QPointF(60, 40)),
             window.mapToOriginalCoords(QPointF(60, 100)));
    QVERIFY(tool->hasDraft());

    const auto bandCenterOnScreen = [&]() {
        return window.mapFromOriginalCoords(tool->draftToolRect().center());
    };

    // Down moves the band down on screen by exactly one source pixel.
    const int startBefore = tool->request().start;
    const QPointF before = bandCenterOnScreen();
    pressKey(window, Qt::Key_Down);
    QCOMPARE(qAbs(tool->request().start - startBefore), 1);
    QVERIFY(bandCenterOnScreen().y() > before.y());
    pressKey(window, Qt::Key_Up);
    QCOMPARE(tool->request().start, startBefore);
    pressKey(window, Qt::Key_Up, Qt::ShiftModifier);
    QCOMPARE(qAbs(tool->request().start - startBefore), 10);

    // Across the band there is nowhere to move: the keys do nothing to it.
    const int start = tool->request().start;
    pressKey(window, Qt::Key_Left);
    pressKey(window, Qt::Key_Right);
    QCOMPARE(tool->request().start, start);
    QVERIFY(!window.canUndoCrop());
}

void TestPinWindowStripCutOut::subToolbarShowsOperationDirectionAndResult()
{
    PinWindow window(patternPixmap(600, 400, 2.0), QPoint(0, 0), nullptr, false);
    window.showToolbar();
    window.handleToolbarToolSelected(static_cast<int>(ToolId::CutOut));
    CutOutToolHandler* tool = window.cutOutToolHandler();
    auto* options = window.m_subToolbar->viewModel();

    QVERIFY(options->showCutOutSection());
    QVERIFY(!options->showCropSection());
    QVERIFY(!options->cutOutHasDraft());
    QCOMPARE(options->cutOutOperation(), int(Operation::Remove));
    QCOMPARE(options->cutOutAxis(), int(Axis::Rows));

    // The pin is 300x200 logical px; sizes are real source pixels.
    dragTool(window.m_toolManager, QPointF(150, 50), QPointF(150, 100));
    QVERIFY(options->cutOutHasDraft());
    QCOMPARE(options->cutOutSizeText(), QStringLiteral("\u2212100 px \u2192 600 \u00D7 300 px"));

    options->handleCutOutOperationSelected(int(Operation::Insert));
    QCOMPARE(tool->operation(), Operation::Insert);
    QCOMPARE(options->cutOutOperation(), int(Operation::Insert));
    QCOMPARE(options->cutOutSizeText(), QStringLiteral("+100 px \u2192 600 \u00D7 500 px"));

    // Another direction starts over: the old band meant rows.
    options->handleCutOutAxisSelected(int(Axis::Columns));
    QCOMPARE(tool->viewAxis(), Axis::Columns);
    QCOMPARE(options->cutOutAxis(), int(Axis::Columns));
    QVERIFY(!options->cutOutHasDraft());

    dragTool(window.m_toolManager, QPointF(100, 80), QPointF(140, 80));
    QCOMPARE(options->cutOutSizeText(), QStringLiteral("+80 px \u2192 680 \u00D7 400 px"));
    options->handleCutOutCancel();
    QVERIFY(!tool->hasDraft());
    options->handleCutOutApply();
    QCOMPARE(window.cropSourcePixelSize(), QSize(600, 400));

    dragTool(window.m_toolManager, QPointF(100, 80), QPointF(140, 80));
    options->handleCutOutApply();
    QCOMPARE(window.cropSourcePixelSize(), QSize(680, 400));
    QCOMPARE(window.size(), QSize(340, 200));
    QVERIFY(!window.isCutOutToolActive());
    // The inserted band is see-through.
    QCOMPARE(qAlpha(window.m_originalPixmap.toImage().pixel(240, 100)), 0);
}

void TestPinWindowStripCutOut::exportMatchesWhatIsShown()
{
    const QPixmap source = patternPixmap(300, 240);
    PinWindow window(source, QPoint(0, 0), nullptr, false);
    window.showToolbar();
    window.m_annotationLayer->addItem(line(QPoint(20, 200), QPoint(200, 220)));
    QVERIFY(window.applyStrip(strip(Axis::Rows, Operation::Remove, 60, 90)));

    // Copy and Save use this pixmap; the pin paints the same display pixmap.
    const QImage exported = argb(window.getExportPixmapWithAnnotations().toImage());
    const QImage bare = argb(window.getExportPixmapCore(false, true).toImage());
    QCOMPARE(exported.size(), QSize(300, 150));
    QCOMPARE(bare, argb(ImageStrip::apply(source.toImage(), strip(Axis::Rows, Operation::Remove, 60, 90)).image));
    QCOMPARE(argb(window.m_displayPixmap.toImage()), bare);
    // The stroke is drawn where it moved to, not where it was.
    QVERIFY(exported.copy(0, 100, 300, 50) != bare.copy(0, 100, 300, 50));
    QCOMPARE(exported.copy(0, 0, 300, 90), bare.copy(0, 0, 300, 90));
}

QTEST_MAIN(TestPinWindowStripCutOut)
#include "tst_StripCutOut.moc"
