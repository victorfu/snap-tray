#include "scrollcapture/WinScrollGeometry.h"

#include <QtTest>

using namespace snaptray::scrollcapture;

class tst_WinScrollGeometry : public QObject
{
    Q_OBJECT

private slots:
    void logicalBounds_data();
    void logicalBounds();
};

void tst_WinScrollGeometry::logicalBounds_data()
{
    QTest::addColumn<QRectF>("physicalBounds");
    QTest::addColumn<QPoint>("screenOrigin");
    QTest::addColumn<qreal>("devicePixelRatio");
    QTest::addColumn<QRect>("expected");

    // #427: the right edge is 14 physical pixels beyond a 1920 px screen.
    QTest::newRow("chrome-right-overhang") << QRectF(356, 167, 1578, 975) << QPoint()
                                          << qreal(1.5) << QRect(237, 111, 1053, 651);
    QTest::newRow("maximized-150-percent") << QRectF(0, 131, 1920, 998) << QPoint()
                                           << qreal(1.5) << QRect(0, 87, 1280, 666);
    QTest::newRow("negative-origin-and-overhang") << QRectF(-1662, -210, 1200, 900)
        << QPoint(-1600, -192) << qreal(1.25) << QRect(-1650, -207, 961, 721);
    QTest::newRow("unscaled") << QRectF(4, 80, 1912, 1068) << QPoint() << qreal(1)
                              << QRect(4, 80, 1912, 1068);
}

void tst_WinScrollGeometry::logicalBounds()
{
    QFETCH(QRectF, physicalBounds);
    QFETCH(QPoint, screenOrigin);
    QFETCH(qreal, devicePixelRatio);
    QFETCH(QRect, expected);

    QCOMPARE(logicalScrollBounds(physicalBounds, screenOrigin, devicePixelRatio), expected);
}

QTEST_APPLESS_MAIN(tst_WinScrollGeometry)
#include "tst_WinScrollGeometry.moc"
