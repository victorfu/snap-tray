#include <QtTest/QtTest>

#include <QColorSpace>

#include "pinwindow/ImageStripTransform.h"
#include "tools/StripDraftModel.h"

using ImageStrip::Axis;
using ImageStrip::Error;
using ImageStrip::Operation;
using ImageStrip::Placement;
using ImageStrip::Request;

namespace {

// Every pixel encodes its own coordinates, so a moved pixel is recognisable.
QImage gridImage(int width, int height, QImage::Format format = QImage::Format_ARGB32_Premultiplied)
{
    QImage image(width, height, format);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            image.setPixelColor(x, y, QColor(x % 256, y % 256, (x / 256) * 16 + (y / 256), 255));
        }
    }
    return image;
}

Request request(Axis axis, Operation operation, int start, int length)
{
    Request r;
    r.axis = axis;
    r.operation = operation;
    r.start = start;
    r.length = length;
    return r;
}

} // namespace

class tst_ImageStripTransform : public QObject
{
    Q_OBJECT

private slots:
    void removeRowsJoinsBothSides();
    void removeColumnsJoinsBothSides();
    void insertRowsAddsTransparentBand();
    void insertColumnsAtEdges();
    void insertGivesOpaqueFormatsAnAlphaChannel();
    void keepsDevicePixelRatioAndColorSpace_data();
    void keepsDevicePixelRatioAndColorSpace();
    void repeatedOperationsDoNotDrift_data();
    void repeatedOperationsDoNotDrift();
    void onePixelStrips();
    void invalidRequestsAreRefusedAndLeaveSourceAlone_data();
    void invalidRequestsAreRefusedAndLeaveSourceAlone();
    void insertIsLimitedBySizeBudget();
    void placementOfItems();
    void shiftFollowsOperation();

    void lengthStepIsExactInPixelsAndViewUnits_data();
    void lengthStepIsExactInPixelsAndViewUnits();
    void draftFollowsViewScale();
    void draftLengthsAreWholeLogicalPixels_data();
    void draftLengthsAreWholeLogicalPixels();
    void draftNeverTakesTheWholeImage();
    void draftMoveResizeAndNudge();
    void draftInsertMayAppendAtTheEnd();
    void draftAxisAndSourceChangesDiscard();
};

void tst_ImageStripTransform::removeRowsJoinsBothSides()
{
    const QImage source = gridImage(40, 30);
    const QImage before = source.copy();
    const auto result = ImageStrip::apply(source, request(Axis::Rows, Operation::Remove, 10, 8));
    QVERIFY(result.ok());
    QCOMPARE(result.image.size(), QSize(40, 22));
    for (int x = 0; x < 40; x += 3) {
        QCOMPARE(result.image.pixelColor(x, 0), source.pixelColor(x, 0));
        QCOMPARE(result.image.pixelColor(x, 9), source.pixelColor(x, 9));     // last row before
        QCOMPARE(result.image.pixelColor(x, 10), source.pixelColor(x, 18));   // first row after
        QCOMPARE(result.image.pixelColor(x, 21), source.pixelColor(x, 29));
    }
    QCOMPARE(source, before);
}

void tst_ImageStripTransform::removeColumnsJoinsBothSides()
{
    const QImage source = gridImage(40, 30);
    const auto result = ImageStrip::apply(source, request(Axis::Columns, Operation::Remove, 0, 5));
    QVERIFY(result.ok());
    QCOMPARE(result.image.size(), QSize(35, 30));
    QCOMPARE(result.image, source.copy(5, 0, 35, 30));

    const auto tail = ImageStrip::apply(source, request(Axis::Columns, Operation::Remove, 35, 5));
    QVERIFY(tail.ok());
    QCOMPARE(tail.image, source.copy(0, 0, 35, 30));

    const auto middle = ImageStrip::apply(source, request(Axis::Columns, Operation::Remove, 12, 20));
    QVERIFY(middle.ok());
    QCOMPARE(middle.image.copy(0, 0, 12, 30), source.copy(0, 0, 12, 30));
    QCOMPARE(middle.image.copy(12, 0, 8, 30), source.copy(32, 0, 8, 30));
}

void tst_ImageStripTransform::insertRowsAddsTransparentBand()
{
    const QImage source = gridImage(40, 30);
    const auto result = ImageStrip::apply(source, request(Axis::Rows, Operation::Insert, 10, 6));
    QVERIFY(result.ok());
    QCOMPARE(result.image.size(), QSize(40, 36));
    QCOMPARE(result.image.copy(0, 0, 40, 10), source.copy(0, 0, 40, 10));
    QCOMPARE(result.image.copy(0, 16, 40, 20), source.copy(0, 10, 40, 20));
    for (int y = 10; y < 16; ++y) {
        for (int x = 0; x < 40; ++x) {
            QCOMPARE(qAlpha(result.image.pixel(x, y)), 0);
        }
    }
}

void tst_ImageStripTransform::insertColumnsAtEdges()
{
    const QImage source = gridImage(20, 10);
    const auto front = ImageStrip::apply(source, request(Axis::Columns, Operation::Insert, 0, 4));
    QVERIFY(front.ok());
    QCOMPARE(front.image.copy(4, 0, 20, 10), source);
    QCOMPARE(qAlpha(front.image.pixel(3, 5)), 0);

    // start == width appends after the last column.
    const auto back = ImageStrip::apply(source, request(Axis::Columns, Operation::Insert, 20, 4));
    QVERIFY(back.ok());
    QCOMPARE(back.image.size(), QSize(24, 10));
    QCOMPARE(back.image.copy(0, 0, 20, 10), source);
    QCOMPARE(qAlpha(back.image.pixel(23, 5)), 0);
}

void tst_ImageStripTransform::insertGivesOpaqueFormatsAnAlphaChannel()
{
    const QImage source = gridImage(20, 10, QImage::Format_RGB32);
    QVERIFY(!source.hasAlphaChannel());
    const auto inserted = ImageStrip::apply(source, request(Axis::Rows, Operation::Insert, 5, 2));
    QVERIFY(inserted.ok());
    QVERIFY(inserted.image.hasAlphaChannel());
    QCOMPARE(qAlpha(inserted.image.pixel(3, 5)), 0);
    QCOMPARE(inserted.image.pixelColor(3, 4), source.pixelColor(3, 4));
    QCOMPARE(inserted.image.pixelColor(3, 7), source.pixelColor(3, 5));

    // Removing needs no alpha and keeps the format.
    const auto removed = ImageStrip::apply(source, request(Axis::Rows, Operation::Remove, 5, 2));
    QCOMPARE(removed.image.format(), QImage::Format_RGB32);
}

void tst_ImageStripTransform::keepsDevicePixelRatioAndColorSpace_data()
{
    QTest::addColumn<qreal>("dpr");
    QTest::newRow("1x") << 1.0;
    QTest::newRow("1.25x") << 1.25;
    QTest::newRow("1.5x") << 1.5;
    QTest::newRow("2x") << 2.0;
}

void tst_ImageStripTransform::keepsDevicePixelRatioAndColorSpace()
{
    QFETCH(qreal, dpr);
    QImage source = gridImage(60, 40);
    source.setDevicePixelRatio(dpr);
    source.setColorSpace(QColorSpace(QColorSpace::DisplayP3));

    for (const Operation operation : {Operation::Remove, Operation::Insert}) {
        for (const Axis axis : {Axis::Rows, Axis::Columns}) {
            const auto result = ImageStrip::apply(source, request(axis, operation, 7, 9));
            QVERIFY(result.ok());
            QCOMPARE(result.image.devicePixelRatio(), dpr);
            QCOMPARE(result.image.colorSpace(), QColorSpace(QColorSpace::DisplayP3));
            // Sizes are device pixels: the ratio never rescales the strip.
            const int change = operation == Operation::Remove ? -9 : 9;
            QCOMPARE(result.image.size(),
                     axis == Axis::Rows ? QSize(60, 40 + change) : QSize(60 + change, 40));
            // The pixel after the strip is the same source pixel at any ratio.
            const QPoint after = axis == Axis::Rows ? QPoint(3, 7) : QPoint(7, 3);
            const QPoint from = operation == Operation::Remove
                ? (axis == Axis::Rows ? QPoint(3, 16) : QPoint(16, 3))
                : QPoint();
            if (operation == Operation::Remove) {
                QCOMPARE(result.image.pixelColor(after), source.pixelColor(from));
            } else {
                QCOMPARE(qAlpha(result.image.pixel(after)), 0);
            }
        }
    }
}

void tst_ImageStripTransform::repeatedOperationsDoNotDrift_data()
{
    keepsDevicePixelRatioAndColorSpace_data();
}

void tst_ImageStripTransform::repeatedOperationsDoNotDrift()
{
    QFETCH(qreal, dpr);
    QImage image = gridImage(200, 160);
    image.setDevicePixelRatio(dpr);
    const QImage original = image.copy();

    // Insert and remove the same strips over and over.
    for (int round = 0; round < 25; ++round) {
        auto inserted = ImageStrip::apply(image, request(Axis::Rows, Operation::Insert, 33, 7));
        QVERIFY(inserted.ok());
        auto widened = ImageStrip::apply(inserted.image, request(Axis::Columns, Operation::Insert, 101, 3));
        QVERIFY(widened.ok());
        QCOMPARE(widened.image.size(), QSize(203, 167));
        auto narrowed = ImageStrip::apply(widened.image, request(Axis::Columns, Operation::Remove, 101, 3));
        auto restored = ImageStrip::apply(narrowed.image, request(Axis::Rows, Operation::Remove, 33, 7));
        QVERIFY(restored.ok());
        image = restored.image;
    }
    QCOMPARE(image.size(), original.size());
    QCOMPARE(image.devicePixelRatio(), dpr);
    QCOMPARE(image.convertToFormat(QImage::Format_ARGB32), original.convertToFormat(QImage::Format_ARGB32));
}

void tst_ImageStripTransform::onePixelStrips()
{
    const QImage source = gridImage(5, 4);
    const auto removed = ImageStrip::apply(source, request(Axis::Rows, Operation::Remove, 3, 1));
    QVERIFY(removed.ok());
    QCOMPARE(removed.image, source.copy(0, 0, 5, 3));

    const auto inserted = ImageStrip::apply(source, request(Axis::Columns, Operation::Insert, 2, 1));
    QVERIFY(inserted.ok());
    QCOMPARE(inserted.image.size(), QSize(6, 4));
    QCOMPARE(inserted.image.pixelColor(1, 2), source.pixelColor(1, 2));
    QCOMPARE(inserted.image.pixelColor(3, 2), source.pixelColor(2, 2));

    // A two pixel image can lose one row but not both.
    const QImage tiny = gridImage(3, 2);
    QVERIFY(ImageStrip::apply(tiny, request(Axis::Rows, Operation::Remove, 0, 1)).ok());
    QCOMPARE(ImageStrip::apply(tiny, request(Axis::Rows, Operation::Remove, 0, 2)).error, Error::WholeImage);
}

void tst_ImageStripTransform::invalidRequestsAreRefusedAndLeaveSourceAlone_data()
{
    QTest::addColumn<int>("axis");
    QTest::addColumn<int>("operation");
    QTest::addColumn<int>("start");
    QTest::addColumn<int>("length");
    QTest::addColumn<int>("expected");

    const int rows = int(Axis::Rows), cols = int(Axis::Columns);
    const int remove = int(Operation::Remove), insert = int(Operation::Insert);
    QTest::newRow("zero length") << rows << remove << 5 << 0 << int(Error::InvalidStrip);
    QTest::newRow("negative length") << rows << insert << 5 << -3 << int(Error::InvalidStrip);
    QTest::newRow("negative start") << cols << remove << -1 << 4 << int(Error::InvalidStrip);
    QTest::newRow("remove past the end") << rows << remove << 25 << 10 << int(Error::InvalidStrip);
    QTest::newRow("remove starting at the end") << rows << remove << 30 << 1 << int(Error::InvalidStrip);
    QTest::newRow("remove everything") << rows << remove << 0 << 30 << int(Error::WholeImage);
    QTest::newRow("remove every column") << cols << remove << 0 << 40 << int(Error::WholeImage);
    QTest::newRow("insert beyond the end") << cols << insert << 41 << 4 << int(Error::InvalidStrip);
    QTest::newRow("insert too much") << rows << insert << 0 << 40000 << int(Error::TooLarge);
    QTest::newRow("length overflow") << rows << remove << 1 << INT_MAX << int(Error::InvalidStrip);
}

void tst_ImageStripTransform::invalidRequestsAreRefusedAndLeaveSourceAlone()
{
    QFETCH(int, axis);
    QFETCH(int, operation);
    QFETCH(int, start);
    QFETCH(int, length);
    QFETCH(int, expected);

    const QImage source = gridImage(40, 30);
    const QImage before = source.copy();
    const Request r = request(Axis(axis), Operation(operation), start, length);
    QCOMPARE(int(ImageStrip::validate(source.size(), r)), expected);
    QVERIFY(!ImageStrip::resultSize(source.size(), r).isValid());

    const auto result = ImageStrip::apply(source, r);
    QVERIFY(!result.ok());
    QVERIFY(result.image.isNull());
    QCOMPARE(int(result.error), expected);
    QCOMPARE(source, before);

    QCOMPARE(ImageStrip::apply(QImage(), r).error, Error::EmptyImage);
}

void tst_ImageStripTransform::insertIsLimitedBySizeBudget()
{
    // Dimension limit.
    QCOMPARE(ImageStrip::maxInsertLength(QSize(100, 1000), Axis::Rows), ImageStrip::kMaxDimension - 1000);
    // Pixel budget: an 8000 px wide image may be at most 8000 rows tall.
    const QSize wide(8000, 6000);
    const int room = ImageStrip::maxInsertLength(wide, Axis::Rows);
    QCOMPARE(room, 2000);
    QCOMPARE(ImageStrip::validate(wide, request(Axis::Rows, Operation::Insert, 0, room)), Error::None);
    QCOMPARE(ImageStrip::validate(wide, request(Axis::Rows, Operation::Insert, 0, room + 1)), Error::TooLarge);
    // Already at the budget: nothing more fits.
    QCOMPARE(ImageStrip::maxInsertLength(QSize(8000, 8000), Axis::Columns), 0);
    QCOMPARE(ImageStrip::maxInsertLength(QSize(), Axis::Rows), 0);
}

void tst_ImageStripTransform::placementOfItems()
{
    // A strip of rows 100..150.
    const auto place = [](const QRectF& bounds, Operation operation) {
        return ImageStrip::placementOf(bounds, Axis::Rows, operation, 100.0, 50.0);
    };
    QCOMPARE(place(QRectF(10, 10, 50, 80), Operation::Remove), Placement::Before);
    QCOMPARE(place(QRectF(10, 50, 50, 50), Operation::Remove), Placement::Before);   // touches the edge
    QCOMPARE(place(QRectF(10, 150, 50, 20), Operation::Remove), Placement::After);
    QCOMPARE(place(QRectF(10, 90, 50, 20), Operation::Remove), Placement::Crossing);
    QCOMPARE(place(QRectF(10, 110, 50, 20), Operation::Remove), Placement::Crossing);  // inside it
    QCOMPARE(place(QRectF(10, 140, 50, 20), Operation::Remove), Placement::Crossing);
    QCOMPARE(place(QRectF(10, 20, 50, 400), Operation::Remove), Placement::Crossing);  // spans it

    // An insertion only splits what spans the line at 100.
    QCOMPARE(place(QRectF(10, 50, 50, 50), Operation::Insert), Placement::Before);
    QCOMPARE(place(QRectF(10, 100, 50, 20), Operation::Insert), Placement::After);
    QCOMPARE(place(QRectF(10, 110, 50, 20), Operation::Insert), Placement::After);
    QCOMPARE(place(QRectF(10, 90, 50, 20), Operation::Insert), Placement::Crossing);

    // Columns use x.
    QCOMPARE(ImageStrip::placementOf(QRectF(160, 0, 20, 500), Axis::Columns, Operation::Remove, 100, 50),
             Placement::After);
    QCOMPARE(ImageStrip::placementOf(QRectF(120, 0, 20, 500), Axis::Columns, Operation::Remove, 100, 50),
             Placement::Crossing);
}

void tst_ImageStripTransform::shiftFollowsOperation()
{
    QCOMPARE(ImageStrip::shiftAfter(Axis::Rows, Operation::Remove, 50), QPointF(0, -50));
    QCOMPARE(ImageStrip::shiftAfter(Axis::Rows, Operation::Insert, 50), QPointF(0, 50));
    QCOMPARE(ImageStrip::shiftAfter(Axis::Columns, Operation::Remove, 12.5), QPointF(-12.5, 0));
    QCOMPARE(ImageStrip::shiftAfter(Axis::Columns, Operation::Insert, 12.5), QPointF(12.5, 0));
}

void tst_ImageStripTransform::lengthStepIsExactInPixelsAndViewUnits_data()
{
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<qreal>("zoom");
    QTest::addColumn<int>("expected");

    QTest::newRow("1x 100%") << 1.0 << 1.0 << 1;
    QTest::newRow("2x 100%") << 2.0 << 1.0 << 2;
    QTest::newRow("1.25x 100%") << 1.25 << 1.0 << 5;
    QTest::newRow("1.5x 100%") << 1.5 << 1.0 << 3;
    QTest::newRow("1x 150%") << 1.0 << 1.5 << 2;
    QTest::newRow("1x 200%") << 1.0 << 2.0 << 1;
    QTest::newRow("1x 50%") << 1.0 << 0.5 << 2;
    QTest::newRow("2x 150%") << 2.0 << 1.5 << 4;
    QTest::newRow("2x 75%") << 2.0 << 0.75 << 8;
    QTest::newRow("1x 137%") << 1.0 << 1.37 << 0;   // no exact length within reach
}

void tst_ImageStripTransform::lengthStepIsExactInPixelsAndViewUnits()
{
    QFETCH(qreal, dpr);
    QFETCH(qreal, zoom);
    QFETCH(int, expected);

    const qreal viewPerPixel = zoom / dpr;
    const int step = ImageStrip::lengthStep(dpr, viewPerPixel);
    QCOMPARE(step, expected);
    if (step > 0) {
        for (int multiple = 1; multiple <= 5; ++multiple) {
            const qreal logical = multiple * step / dpr;
            const qreal view = multiple * step * viewPerPixel;
            QVERIFY(qAbs(logical - qRound(logical)) < 1e-6);
            QVERIFY(qAbs(view - qRound(view)) < 1e-6);
        }
    }
    QCOMPARE(ImageStrip::lengthStep(0.0, 1.0), 0);
    QCOMPARE(ImageStrip::lengthStep(1.0, 0.0), 0);
}

void tst_ImageStripTransform::draftFollowsViewScale()
{
    StripDraftModel model;
    // 1x image shown at 150%: only even lengths move annotations exactly.
    model.setSource(QSize(300, 200), 1.0, QSizeF(1.5, 1.5));
    QCOMPARE(model.lengthStep(), 2);
    model.setFromDrag(40, 47);
    QCOMPARE(model.length(), 8);
    model.setFromDrag(40, 41);
    QCOMPARE(model.length(), 2);

    // Another zoom starts over.
    model.setSource(QSize(300, 200), 1.0, QSizeF(1.0, 1.0));
    QVERIFY(!model.hasStrip());
    model.setFromDrag(40, 47);
    QCOMPARE(model.length(), 7);

    // At a zoom with no exact length nothing can be marked.
    model.setSource(QSize(300, 200), 1.0, QSizeF(1.37, 1.37));
    QCOMPARE(model.lengthStep(), 0);
    model.setFromDrag(40, 90);
    QVERIFY(!model.hasStrip());
}

// ---- StripDraftModel ----

void tst_ImageStripTransform::draftLengthsAreWholeLogicalPixels_data()
{
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<int>("dragged");
    QTest::addColumn<int>("expected");
    QTest::newRow("1x") << 1.0 << 37 << 37;
    QTest::newRow("2x odd") << 2.0 << 37 << 38;
    QTest::newRow("2x one pixel") << 2.0 << 1 << 2;
    QTest::newRow("1.5x") << 1.5 << 100 << 99;
    QTest::newRow("1.5x one pixel") << 1.5 << 1 << 3;
    QTest::newRow("1.25x one pixel") << 1.25 << 1 << 5;
    QTest::newRow("1.25x 12 px") << 1.25 << 12 << 10;
    QTest::newRow("1.25x") << 1.25 << 50 << 50;
}

void tst_ImageStripTransform::draftLengthsAreWholeLogicalPixels()
{
    QFETCH(qreal, dpr);
    QFETCH(int, dragged);
    QFETCH(int, expected);

    for (const Operation operation : {Operation::Remove, Operation::Insert}) {
        StripDraftModel model;
        model.setSource(QSize(600, 400), dpr);
        model.setOperation(operation);
        model.setFromDrag(50, 50 + dragged);
        QVERIFY(model.hasStrip());
        QCOMPARE(model.start(), 50);
        QCOMPARE(model.length(), expected);
        // Exactly a whole number of logical pixels, with nothing rounded away.
        QVERIFY(qAbs(expected / dpr - qRound(expected / dpr)) < 1e-9);
        // Dragging the other way keeps the far edge.
        model.setFromDrag(50 + expected, 50);
        QCOMPARE(model.start(), 50);
        QCOMPARE(model.length(), expected);
        QCOMPARE(ImageStrip::validate(model.sourceSize(), model.request()), Error::None);
    }
}

void tst_ImageStripTransform::draftNeverTakesTheWholeImage()
{
    StripDraftModel model;
    model.setSource(QSize(300, 200), 2.0);
    model.setFromDrag(0, 200);
    QVERIFY(model.hasStrip());
    QCOMPARE(model.length(), 198);   // one logical pixel of the image stays
    QCOMPARE(ImageStrip::validate(model.sourceSize(), model.request()), Error::None);

    // Nothing to mark without movement.
    model.setFromDrag(70, 70);
    QVERIFY(!model.hasStrip());

    // An image of one logical pixel cannot lose anything.
    StripDraftModel tiny;
    tiny.setSource(QSize(10, 2), 2.0);
    tiny.setFromDrag(0, 2);
    QVERIFY(!tiny.hasStrip());
}

void tst_ImageStripTransform::draftMoveResizeAndNudge()
{
    StripDraftModel model;
    model.setSource(QSize(300, 200), 1.0);
    model.setFromDrag(40, 90);
    QCOMPARE(model.start(), 40);
    QCOMPARE(model.length(), 50);

    model.move(40, 50, 500);
    QCOMPARE(model.start(), 150);   // stops at the end of the image
    QCOMPARE(model.length(), 50);
    model.move(40, 50, -500);
    QCOMPARE(model.start(), 0);

    model.restore(40, 50);
    model.nudge(1);
    QCOMPARE(model.start(), 41);
    model.nudge(-10);
    QCOMPARE(model.start(), 31);

    model.resize(40, 50, StripDraftModel::Part::EndEdge, 120);
    QCOMPARE(model.start(), 40);
    QCOMPARE(model.length(), 80);
    model.resize(40, 50, StripDraftModel::Part::StartEdge, 10);
    QCOMPARE(model.start(), 10);
    QCOMPARE(model.length(), 80);
    // An edge dragged past the other stops at one pixel.
    model.resize(40, 50, StripDraftModel::Part::EndEdge, 0);
    QCOMPARE(model.start(), 40);
    QCOMPARE(model.length(), 1);
    // The end edge cannot leave the image when removing.
    model.resize(40, 50, StripDraftModel::Part::EndEdge, 5000);
    QCOMPARE(model.start() + model.length(), 200);
}

void tst_ImageStripTransform::draftInsertMayAppendAtTheEnd()
{
    StripDraftModel model;
    model.setSource(QSize(300, 200), 1.0);
    model.setOperation(Operation::Insert);
    model.setFromDrag(150, 200);
    model.move(150, 50, 500);
    QCOMPARE(model.start(), 200);   // after the last row
    QCOMPARE(model.length(), 50);
    QCOMPARE(model.resultSize(), QSize(300, 250));

    // Switching to Remove pulls it back inside the image.
    model.setOperation(Operation::Remove);
    QCOMPARE(model.start(), 150);
    QCOMPARE(model.resultSize(), QSize(300, 150));
    QCOMPARE(ImageStrip::validate(model.sourceSize(), model.request()), Error::None);
}

void tst_ImageStripTransform::draftAxisAndSourceChangesDiscard()
{
    StripDraftModel model;
    model.setSource(QSize(300, 200), 1.0);
    model.setFromDrag(40, 90);
    model.setAxis(Axis::Columns);
    QVERIFY(!model.hasStrip());
    QCOMPARE(model.extent(), 300);

    model.setFromDrag(40, 90);
    model.setSource(QSize(300, 200), 1.0);
    QVERIFY(model.hasStrip());
    model.setSource(QSize(301, 200), 1.0);
    QVERIFY(!model.hasStrip());
}

QTEST_APPLESS_MAIN(tst_ImageStripTransform)
#include "tst_ImageStripTransform.moc"
