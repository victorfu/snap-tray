#include <QtTest/QtTest>

#include "tools/CropDraftModel.h"

using Aspect = CropDraftModel::Aspect;
using Handle = CropDraftModel::Handle;

class tst_CropDraftModel : public QObject
{
    Q_OBJECT

private slots:
    void dragMakesRectInSourcePixels();
    void dragIsClampedToImage();
    void dragWithoutAreaLeavesNoRect();
    void aspectPresetsConstrainDrag_data();
    void aspectPresetsConstrainDrag();
    void aspectDragStopsAtImageEdge();
    void originalAspectFollowsImage();
    void transposedViewSwapsPreset();
    void setAspectRefitsAroundCenter();
    void setAspectNearEdgeStaysInside();
    void tinyImageKeepsAspectLegal();
    void moveStopsAtEdges();
    void nudgeMovesOnePixel();
    void edgeResizeKeepsOppositeEdge();
    void edgeResizeWithAspectFollowsRatio();
    void cornerResizeAnchorsOppositeCorner();
    void resizeNeverCollapses();
    void extentsAreWholeLogicalPixels_data();
    void extentsAreWholeLogicalPixels();
    void fullImageExtentIsAlwaysLegal();
    void positionsMoveBySinglePixelsAtHighDpr();
    void newSourceSizeDiscardsDraft();
};

void tst_CropDraftModel::dragMakesRectInSourcePixels()
{
    CropDraftModel model;
    model.setSource(QSize(200, 100), 1.0);
    model.setRectFromDrag(QPoint(10, 20), QPoint(110, 70));
    QCOMPARE(model.rect(), QRect(10, 20, 100, 50));

    // Dragging up and to the left gives the same rect.
    model.setRectFromDrag(QPoint(110, 70), QPoint(10, 20));
    QCOMPARE(model.rect(), QRect(10, 20, 100, 50));
}

void tst_CropDraftModel::dragIsClampedToImage()
{
    CropDraftModel model;
    model.setSource(QSize(200, 100), 1.0);
    model.setRectFromDrag(QPoint(150, 60), QPoint(900, 900));
    QCOMPARE(model.rect(), QRect(150, 60, 50, 40));

    model.setRectFromDrag(QPoint(50, 40), QPoint(-30, -30));
    QCOMPARE(model.rect(), QRect(0, 0, 50, 40));
}

void tst_CropDraftModel::dragWithoutAreaLeavesNoRect()
{
    CropDraftModel model;
    model.setSource(QSize(200, 100), 1.0);
    model.setRectFromDrag(QPoint(10, 20), QPoint(60, 20));
    QVERIFY(!model.hasRect());
}

void tst_CropDraftModel::aspectPresetsConstrainDrag_data()
{
    QTest::addColumn<int>("aspect");
    QTest::addColumn<QSize>("expected");

    QTest::newRow("1:1") << int(Aspect::Square) << QSize(160, 160);
    QTest::newRow("16:9") << int(Aspect::Wide16x9) << QSize(160, 90);
    QTest::newRow("9:16") << int(Aspect::Tall9x16) << QSize(160, 284);
    QTest::newRow("4:3") << int(Aspect::Standard4x3) << QSize(160, 120);
    QTest::newRow("3:2") << int(Aspect::Classic3x2) << QSize(160, 107);
}

void tst_CropDraftModel::aspectPresetsConstrainDrag()
{
    QFETCH(int, aspect);
    QFETCH(QSize, expected);

    CropDraftModel model;
    model.setSource(QSize(1000, 1000), 1.0);
    model.setAspect(static_cast<Aspect>(aspect));
    // The pointer moved further horizontally, so the width drives the size.
    model.setRectFromDrag(QPoint(100, 100), QPoint(260, 110));
    QCOMPARE(model.rect().topLeft(), QPoint(100, 100));
    QCOMPARE(model.rect().size(), expected);
}

void tst_CropDraftModel::aspectDragStopsAtImageEdge()
{
    CropDraftModel model;
    model.setSource(QSize(300, 100), 1.0);
    model.setAspect(Aspect::Square);
    model.setRectFromDrag(QPoint(10, 20), QPoint(290, 30));
    // Only 80 px remain below the anchor, so the square cannot be wider.
    QCOMPARE(model.rect(), QRect(10, 20, 80, 80));
}

void tst_CropDraftModel::originalAspectFollowsImage()
{
    CropDraftModel model;
    model.setSource(QSize(400, 100), 1.0);
    model.setAspect(Aspect::Original);
    model.setRectFromDrag(QPoint(0, 0), QPoint(200, 10));
    QCOMPARE(model.rect(), QRect(0, 0, 200, 50));

    // Rotating the view does not change what "original" means in source pixels.
    model.setViewTransposed(true);
    model.setRectFromDrag(QPoint(0, 0), QPoint(200, 10));
    QCOMPARE(model.rect(), QRect(0, 0, 200, 50));
}

void tst_CropDraftModel::transposedViewSwapsPreset()
{
    CropDraftModel model;
    model.setSource(QSize(1000, 1000), 1.0);
    model.setAspect(Aspect::Wide16x9);
    model.setViewTransposed(true);
    // On screen the image is turned a quarter, so 16:9 there is 9:16 here.
    model.setRectFromDrag(QPoint(0, 0), QPoint(10, 160));
    QCOMPARE(model.rect().size(), QSize(90, 160));
}

void tst_CropDraftModel::setAspectRefitsAroundCenter()
{
    CropDraftModel model;
    model.setSource(QSize(1000, 1000), 1.0);
    model.setRectFromDrag(QPoint(100, 100), QPoint(500, 700));
    QCOMPARE(model.rect(), QRect(100, 100, 400, 600));

    model.setAspect(Aspect::Square);
    QCOMPARE(model.rect(), QRect(100, 200, 400, 400));

    // Back to free keeps what is there.
    model.setAspect(Aspect::Free);
    QCOMPARE(model.rect(), QRect(100, 200, 400, 400));
}

void tst_CropDraftModel::setAspectNearEdgeStaysInside()
{
    CropDraftModel model;
    model.setSource(QSize(400, 100), 1.0);
    model.setRectFromDrag(QPoint(0, 0), QPoint(400, 20));
    QCOMPARE(model.rect(), QRect(0, 0, 400, 20));

    // A 400 px square does not fit a 100 px tall image: it shrinks to fit.
    model.setAspect(Aspect::Square);
    QCOMPARE(model.rect().size(), QSize(100, 100));
    QVERIFY(QRect(0, 0, 400, 100).contains(model.rect()));
    QCOMPARE(model.rect().center().x(), 199);
}

void tst_CropDraftModel::tinyImageKeepsAspectLegal()
{
    CropDraftModel model;
    model.setSource(QSize(3, 2), 1.0);
    model.setAspect(Aspect::Wide16x9);
    model.setRectFromDrag(QPoint(0, 0), QPoint(3, 2));
    QVERIFY(model.hasRect());
    QVERIFY(QRect(0, 0, 3, 2).contains(model.rect()));
    QVERIFY(model.rect().width() >= 1);
    QVERIFY(model.rect().height() >= 1);

    // A one pixel image can still be selected whole.
    CropDraftModel onePixel;
    onePixel.setSource(QSize(1, 1), 1.0);
    onePixel.setAspect(Aspect::Square);
    onePixel.setRectFromDrag(QPoint(0, 0), QPoint(1, 1));
    QCOMPARE(onePixel.rect(), QRect(0, 0, 1, 1));
}

void tst_CropDraftModel::moveStopsAtEdges()
{
    CropDraftModel model;
    model.setSource(QSize(200, 100), 1.0);
    const QRect start(50, 20, 60, 30);
    model.moveRect(start, QPoint(500, -500));
    QCOMPARE(model.rect(), QRect(140, 0, 60, 30));
    model.moveRect(start, QPoint(-500, 500));
    QCOMPARE(model.rect(), QRect(0, 70, 60, 30));
}

void tst_CropDraftModel::nudgeMovesOnePixel()
{
    CropDraftModel model;
    model.setSource(QSize(200, 100), 1.0);
    model.nudge(QPoint(1, 0));
    QVERIFY(!model.hasRect());

    model.setRectFromDrag(QPoint(50, 20), QPoint(110, 50));
    model.nudge(QPoint(1, 0));
    QCOMPARE(model.rect(), QRect(51, 20, 60, 30));
    model.nudge(QPoint(0, -1));
    QCOMPARE(model.rect(), QRect(51, 19, 60, 30));
    model.nudge(QPoint(0, -100));
    QCOMPARE(model.rect(), QRect(51, 0, 60, 30));
}

void tst_CropDraftModel::edgeResizeKeepsOppositeEdge()
{
    CropDraftModel model;
    model.setSource(QSize(200, 100), 1.0);
    const QRect start(50, 20, 60, 30);

    model.resizeRect(start, Handle::Right, QPoint(150, 0));
    QCOMPARE(model.rect(), QRect(50, 20, 100, 30));
    model.resizeRect(start, Handle::Left, QPoint(20, 0));
    QCOMPARE(model.rect(), QRect(20, 20, 90, 30));
    model.resizeRect(start, Handle::Top, QPoint(0, 5));
    QCOMPARE(model.rect(), QRect(50, 5, 60, 45));
    model.resizeRect(start, Handle::Bottom, QPoint(0, 999));
    QCOMPARE(model.rect(), QRect(50, 20, 60, 80));
}

void tst_CropDraftModel::edgeResizeWithAspectFollowsRatio()
{
    CropDraftModel model;
    model.setSource(QSize(1000, 1000), 1.0);
    model.setAspect(Aspect::Square);
    const QRect start(100, 400, 100, 100);

    model.resizeRect(start, Handle::Right, QPoint(400, 0));
    // Left edge stays; the height grows around the old vertical center (450).
    QCOMPARE(model.rect(), QRect(100, 300, 300, 300));

    model.resizeRect(QRect(100, 0, 100, 100), Handle::Right, QPoint(400, 0));
    // At the top edge there is no room above, so it grows downward only.
    QCOMPARE(model.rect(), QRect(100, 0, 300, 300));
}

void tst_CropDraftModel::cornerResizeAnchorsOppositeCorner()
{
    CropDraftModel model;
    model.setSource(QSize(200, 100), 1.0);
    const QRect start(50, 20, 60, 30);

    model.resizeRect(start, Handle::BottomRight, QPoint(150, 90));
    QCOMPARE(model.rect(), QRect(50, 20, 100, 70));
    model.resizeRect(start, Handle::TopLeft, QPoint(10, 5));
    QCOMPARE(model.rect(), QRect(10, 5, 100, 45));
}

void tst_CropDraftModel::resizeNeverCollapses()
{
    CropDraftModel model;
    model.setSource(QSize(200, 100), 1.0);
    const QRect start(50, 20, 60, 30);
    model.restoreRect(start);

    // Dragging the right edge past the left one stops at one pixel.
    model.resizeRect(start, Handle::Right, QPoint(0, 0));
    QCOMPARE(model.rect(), QRect(50, 20, 1, 30));

    // A corner dragged onto its anchor keeps the previous draft.
    model.restoreRect(start);
    model.resizeRect(start, Handle::BottomRight, QPoint(50, 20));
    QCOMPARE(model.rect(), start);
}

void tst_CropDraftModel::extentsAreWholeLogicalPixels_data()
{
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<int>("draggedPixels");
    QTest::addColumn<int>("expectedPixels");

    QTest::newRow("1x keeps any size") << 1.0 << 101 << 101;
    QTest::newRow("2x rounds to even") << 2.0 << 101 << 102;
    QTest::newRow("2x even stays") << 2.0 << 100 << 100;
    QTest::newRow("2x minimum is one logical pixel") << 2.0 << 1 << 2;
    QTest::newRow("1.5x 100 px is 67 logical") << 1.5 << 100 << 101;
    QTest::newRow("1.25x 50 px is 40 logical") << 1.25 << 50 << 50;
    QTest::newRow("1.25x 51 px is 41 logical") << 1.25 << 51 << 51;
}

void tst_CropDraftModel::extentsAreWholeLogicalPixels()
{
    QFETCH(qreal, dpr);
    QFETCH(int, draggedPixels);
    QFETCH(int, expectedPixels);

    CropDraftModel model;
    model.setSource(QSize(1000, 1000), dpr);
    model.setRectFromDrag(QPoint(10, 10), QPoint(10 + draggedPixels, 10 + draggedPixels));
    QCOMPARE(model.rect().width(), expectedPixels);
    QCOMPARE(model.rect().height(), expectedPixels);

    // The size is one the pin shows 1:1: a whole number of logical pixels.
    const int logical = qRound(expectedPixels / dpr);
    QCOMPARE(qRound(logical * dpr), expectedPixels);
}

void tst_CropDraftModel::fullImageExtentIsAlwaysLegal()
{
    CropDraftModel model;
    // 101 px is not a whole number of logical pixels at 2x, but it is the image.
    model.setSource(QSize(101, 51), 2.0);
    model.setRectFromDrag(QPoint(0, 0), QPoint(101, 51));
    QCOMPARE(model.rect(), QRect(0, 0, 101, 51));

    // Short of the full extent it snaps to an even size that still fits.
    model.setRectFromDrag(QPoint(0, 0), QPoint(99, 49));
    QCOMPARE(model.rect(), QRect(0, 0, 100, 50));
    model.setRectFromDrag(QPoint(1, 1), QPoint(101, 51));
    QCOMPARE(model.rect(), QRect(1, 1, 100, 50));
}

void tst_CropDraftModel::positionsMoveBySinglePixelsAtHighDpr()
{
    CropDraftModel model;
    model.setSource(QSize(400, 200), 2.0);
    model.setRectFromDrag(QPoint(10, 10), QPoint(110, 60));
    QCOMPARE(model.rect(), QRect(10, 10, 100, 50));
    model.nudge(QPoint(1, 0));
    QCOMPARE(model.rect(), QRect(11, 10, 100, 50));
    model.nudge(QPoint(0, 1));
    QCOMPARE(model.rect(), QRect(11, 11, 100, 50));
}

void tst_CropDraftModel::newSourceSizeDiscardsDraft()
{
    CropDraftModel model;
    model.setSource(QSize(200, 100), 1.0);
    model.setRectFromDrag(QPoint(10, 10), QPoint(60, 60));
    QVERIFY(model.hasRect());

    model.setSource(QSize(200, 100), 1.0);
    QVERIFY(model.hasRect());

    model.setSource(QSize(100, 100), 1.0);
    QVERIFY(!model.hasRect());
}

QTEST_APPLESS_MAIN(tst_CropDraftModel)
#include "tst_CropDraftModel.moc"
