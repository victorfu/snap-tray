#include <QtTest/QtTest>

#include <QGuiApplication>
#include <QPainter>
#include <QSignalSpy>

#include "annotation/ColorPickOverlay.h"
#include "region/MagnifierPanel.h"

// #271: the eyedropper picks one physical pixel of the host's own image.
class tst_ColorPickOverlay : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void pixelAtFloorsToThePhysicalPixel_data();
    void pixelAtFloorsToThePhysicalPixel();
    void nudgeMovesOnePhysicalPixelAndStaysInside();
    void colorAtIsTheStoredPixel();
    void hexNameIsUppercase();
    void invalidSourceStartsNothing();
    void clickPicksThePixelUnderThePointer_data();
    void clickPicksThePixelUnderThePointer();
    void arrowKeysMoveOnePhysicalPixelAndEnterPicks();
    void escapeRightClickAndOutsideClickCancel_data();
    void escapeRightClickAndOutsideClickCancel();
    void beginReportsOnlyAcceptedColors();
    void losingActivationCancelsWithoutReclaimingFocus();
    void magnifierSamplesTheDevicePixelItIsGiven();

private:
    // Every pixel has its own color, so a wrong coordinate shows.
    static QImage gradientImage(const QSize& size, qreal dpr);
    static QColor expectedAt(const QPoint& pixel);
};

void tst_ColorPickOverlay::initTestCase()
{
    if (QGuiApplication::screens().isEmpty())
        QSKIP("The overlay needs a screen");
}

QImage tst_ColorPickOverlay::gradientImage(const QSize& size, qreal dpr)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width(); ++x)
            image.setPixelColor(x, y, expectedAt(QPoint(x, y)));
    }
    image.setDevicePixelRatio(dpr);
    return image;
}

QColor tst_ColorPickOverlay::expectedAt(const QPoint& pixel)
{
    return QColor(pixel.x() % 256, pixel.y() % 256, (pixel.x() + pixel.y()) % 256);
}

void tst_ColorPickOverlay::pixelAtFloorsToThePhysicalPixel_data()
{
    QTest::addColumn<QPointF>("logical");
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<QPoint>("expected");
    QTest::newRow("1x") << QPointF(10, 20) << 1.0 << QPoint(10, 20);
    QTest::newRow("2x whole") << QPointF(10, 20) << 2.0 << QPoint(20, 40);
    // At 2x the second physical pixel of a logical pixel is only reachable
    // through a fractional position; floor keeps it, rounding would skip it.
    QTest::newRow("2x half") << QPointF(10.5, 20.5) << 2.0 << QPoint(21, 41);
    QTest::newRow("2x 0.99") << QPointF(10.99, 0.0) << 2.0 << QPoint(21, 0);
    QTest::newRow("1.5x") << QPointF(11, 3) << 1.5 << QPoint(16, 4);
    QTest::newRow("left of image") << QPointF(-0.1, 5) << 1.0 << QPoint(-1, -1);
    QTest::newRow("past the edge") << QPointF(50, 5) << 2.0 << QPoint(-1, -1);
}

void tst_ColorPickOverlay::pixelAtFloorsToThePhysicalPixel()
{
    QFETCH(QPointF, logical);
    QFETCH(qreal, dpr);
    QFETCH(QPoint, expected);
    QCOMPARE(ColorPick::pixelAt(logical, dpr, QSize(100, 100)), expected);
}

void tst_ColorPickOverlay::nudgeMovesOnePhysicalPixelAndStaysInside()
{
    const QSize size(10, 8);
    QCOMPARE(ColorPick::nudged(QPoint(4, 4), QPoint(1, 0), size), QPoint(5, 4));
    QCOMPARE(ColorPick::nudged(QPoint(4, 4), QPoint(0, -1), size), QPoint(4, 3));
    QCOMPARE(ColorPick::nudged(QPoint(0, 0), QPoint(-1, -1), size), QPoint(0, 0));
    QCOMPARE(ColorPick::nudged(QPoint(9, 7), QPoint(1, 1), size), QPoint(9, 7));
}

void tst_ColorPickOverlay::colorAtIsTheStoredPixel()
{
    const QImage image = gradientImage(QSize(40, 30), 2.0);
    QCOMPARE(ColorPick::colorAt(image, QPoint(17, 23)), expectedAt(QPoint(17, 23)));
    QVERIFY(!ColorPick::colorAt(image, QPoint(40, 0)).isValid());

    // A see-through pixel (a rounded pin corner) still yields a visible color.
    QImage translucent(2, 2, QImage::Format_ARGB32_Premultiplied);
    translucent.fill(QColor(200, 100, 50, 128));
    const QColor picked = ColorPick::colorAt(translucent, QPoint(0, 0));
    QCOMPARE(picked.alpha(), 255);
    QVERIFY(qAbs(picked.red() - 200) <= 1);
}

void tst_ColorPickOverlay::hexNameIsUppercase()
{
    QCOMPARE(ColorPick::hexName(QColor(0x1a, 0x73, 0xe8)), QStringLiteral("#1A73E8"));
}

void tst_ColorPickOverlay::invalidSourceStartsNothing()
{
    bool called = false;
    QVERIFY(!ColorPickOverlay::begin(nullptr, ColorPickSource{},
                                     [&](const QColor&) { called = true; }));
    QVERIFY(!called);
}

void tst_ColorPickOverlay::clickPicksThePixelUnderThePointer_data()
{
    QTest::addColumn<qreal>("dpr");
    QTest::addColumn<QPointF>("logical");
    QTest::newRow("1x") << 1.0 << QPointF(30, 12);
    QTest::newRow("2x") << 2.0 << QPointF(30, 12);
}

void tst_ColorPickOverlay::clickPicksThePixelUnderThePointer()
{
    QFETCH(qreal, dpr);
    QFETCH(QPointF, logical);
    const QRect area(0, 0, 120, 80);
    ColorPickOverlay overlay(
        {area, area, gradientImage((QSizeF(area.size()) * dpr).toSize(), dpr)});
    QSignalSpy picked(&overlay, &ColorPickOverlay::colorPicked);
    QSignalSpy cancelled(&overlay, &ColorPickOverlay::cancelled);

    const QPoint pos = logical.toPoint();
    QTest::mousePress(&overlay, Qt::LeftButton, {}, pos);
    QTest::mouseRelease(&overlay, Qt::LeftButton, {}, pos);

    QCOMPARE(picked.size(), 1);
    QCOMPARE(cancelled.size(), 0);
    const QPoint pixel = ColorPick::pixelAt(logical, dpr, (QSizeF(area.size()) * dpr).toSize());
    QCOMPARE(picked.first().first().value<QColor>(), expectedAt(pixel));
}

void tst_ColorPickOverlay::arrowKeysMoveOnePhysicalPixelAndEnterPicks()
{
    const qreal dpr = 2.0;
    const QRect area(0, 0, 120, 80);
    ColorPickOverlay overlay({area, area, gradientImage(area.size() * 2, dpr)});
    QSignalSpy picked(&overlay, &ColorPickOverlay::colorPicked);

    QMouseEvent move(QEvent::MouseMove, QPointF(10, 10), overlay.mapToGlobal(QPointF(10, 10)),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&overlay, &move);
    QVERIFY(overlay.hasSample());
    QCOMPARE(overlay.samplePixel(), QPoint(20, 20));

    QTest::keyClick(&overlay, Qt::Key_Right);
    QCOMPARE(overlay.samplePixel(), QPoint(21, 20));
    QTest::keyClick(&overlay, Qt::Key_Down);
    QTest::keyClick(&overlay, Qt::Key_Down);
    QCOMPARE(overlay.samplePixel(), QPoint(21, 22));
    QTest::keyClick(&overlay, Qt::Key_Left);
    QTest::keyClick(&overlay, Qt::Key_Up);
    QCOMPARE(overlay.samplePixel(), QPoint(20, 21));
    // Moving alone picks nothing.
    QCOMPARE(picked.size(), 0);

    QTest::keyClick(&overlay, Qt::Key_Return);
    QCOMPARE(picked.size(), 1);
    QCOMPARE(picked.first().first().value<QColor>(), expectedAt(QPoint(20, 21)));
}

void tst_ColorPickOverlay::escapeRightClickAndOutsideClickCancel_data()
{
    QTest::addColumn<int>("action");
    QTest::newRow("escape") << 0;
    QTest::newRow("right click") << 1;
    QTest::newRow("click outside the sample") << 2;
}

void tst_ColorPickOverlay::escapeRightClickAndOutsideClickCancel()
{
    QFETCH(int, action);
    // The sample (a pin) is smaller than the overlay (its screen).
    const QRect overlayArea(0, 0, 300, 200);
    const QRect sampleArea(100, 50, 60, 40);
    ColorPickOverlay overlay({overlayArea, sampleArea, gradientImage(sampleArea.size(), 1.0)});
    QSignalSpy picked(&overlay, &ColorPickOverlay::colorPicked);
    QSignalSpy cancelled(&overlay, &ColorPickOverlay::cancelled);

    if (action == 0) {
        QTest::keyClick(&overlay, Qt::Key_Escape);
    } else if (action == 1) {
        QTest::mousePress(&overlay, Qt::RightButton, {}, QPoint(120, 60));
    } else {
        QTest::mousePress(&overlay, Qt::LeftButton, {}, QPoint(10, 10));
        QVERIFY(!overlay.hasSample());
    }

    QCOMPARE(picked.size(), 0);
    QCOMPARE(cancelled.size(), 1);
    // Finishing twice reports nothing more.
    overlay.cancel();
    QCOMPARE(cancelled.size(), 1);
}

void tst_ColorPickOverlay::beginReportsOnlyAcceptedColors()
{
    QWidget host;
    host.resize(200, 120);
    const QRect area(0, 0, 100, 60);

    QList<QColor> results;
    auto onPicked = [&](const QColor& color) { results << color; };

    QPointer<ColorPickOverlay> cancelledPick = ColorPickOverlay::begin(
        &host, {area, area, gradientImage(area.size(), 1.0)}, onPicked);
    QVERIFY(cancelledPick);
    QTest::keyClick(cancelledPick, Qt::Key_Escape);
    QVERIFY(results.isEmpty());
    QTRY_VERIFY(!cancelledPick);  // deletes itself

    QPointer<ColorPickOverlay> acceptedPick = ColorPickOverlay::begin(
        &host, {area, area, gradientImage(area.size(), 1.0)}, onPicked);
    QVERIFY(acceptedPick);
    QTest::mousePress(acceptedPick, Qt::LeftButton, {}, QPoint(7, 9));
    QTest::mouseRelease(acceptedPick, Qt::LeftButton, {}, QPoint(7, 9));
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first(), expectedAt(QPoint(7, 9)));
    QTRY_VERIFY(!acceptedPick);
}

void tst_ColorPickOverlay::losingActivationCancelsWithoutReclaimingFocus()
{
    const QRect area(0, 0, 100, 60);
    ColorPickOverlay overlay({area, area, gradientImage(area.size(), 1.0)});
    QSignalSpy cancelled(&overlay, &ColorPickOverlay::cancelled);

    // A deactivation before the overlay was ever active is not the user leaving.
    QEvent deactivate(QEvent::WindowDeactivate);
    QCoreApplication::sendEvent(&overlay, &deactivate);
    QCOMPARE(cancelled.size(), 0);

    QEvent activate(QEvent::WindowActivate);
    QCoreApplication::sendEvent(&overlay, &activate);
    QCoreApplication::sendEvent(&overlay, &deactivate);
    QCOMPARE(cancelled.size(), 1);
    // begin() reads this to leave focus with the window the user chose.
    QVERIFY(overlay.lostActivation());

    // Esc is an explicit cancel: focus goes back to the host.
    ColorPickOverlay escaped({area, area, gradientImage(area.size(), 1.0)});
    QSignalSpy escapedCancelled(&escaped, &ColorPickOverlay::cancelled);
    QTest::keyClick(&escaped, Qt::Key_Escape);
    QCOMPARE(escapedCancelled.size(), 1);
    QVERIFY(!escaped.lostActivation());
}

void tst_ColorPickOverlay::magnifierSamplesTheDevicePixelItIsGiven()
{
    // A physical pixel that no logical point reaches at 2x is still shown,
    // wherever the panel itself is placed.
    const QImage image = gradientImage(QSize(80, 60), 2.0);
    const QPixmap pixmap = QPixmap::fromImage(image);
    MagnifierPanel panel;
    panel.setDevicePixelRatio(2.0);
    QImage canvas(400, 300, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    panel.drawAt(painter, QPoint(5, 5), QPoint(31, 17), canvas.size(), pixmap);
    painter.end();
    QCOMPARE(panel.currentColor(), expectedAt(QPoint(31, 17)));
    QCOMPARE(panel.coordinateString(), QStringLiteral("(31 , 17)"));
}

QTEST_MAIN(tst_ColorPickOverlay)
#include "tst_ColorPickOverlay.moc"
