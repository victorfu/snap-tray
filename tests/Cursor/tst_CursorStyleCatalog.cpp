#include <QtTest/QtTest>

#include "cursor/CursorStyleCatalog.h"

class TestCursorStyleCatalog : public QObject
{
    Q_OBJECT

private slots:
    void testSystemShapeMapping();
    void testLegacyCursorPassthrough();
    void testMosaicBrushUsesCenteredHotspot();
    void testEraserBrushUsesCenteredHotspot();
};

void TestCursorStyleCatalog::testSystemShapeMapping()
{
    const auto spec = CursorStyleSpec::fromShape(Qt::PointingHandCursor);
    QCOMPARE(spec.styleId, CursorStyleId::PointingHand);
    QCOMPARE(CursorStyleCatalog::instance().cursorForStyle(spec).shape(), Qt::PointingHandCursor);
}

void TestCursorStyleCatalog::testLegacyCursorPassthrough()
{
    const QCursor waitCursor(Qt::WaitCursor);
    const auto spec = CursorStyleSpec::fromCursor(waitCursor);
    QCOMPARE(spec.styleId, CursorStyleId::LegacyCursor);
    QCOMPARE(CursorStyleCatalog::instance().cursorForStyle(spec).shape(), Qt::WaitCursor);
}

void TestCursorStyleCatalog::testMosaicBrushUsesCenteredHotspot()
{
    CursorStyleSpec spec;
    spec.styleId = CursorStyleId::MosaicBrush;
    spec.primaryValue = 18;

    const QCursor cursor = CursorStyleCatalog::instance().cursorForStyle(spec);
    const QPixmap pixmap = cursor.pixmap();
    const QSizeF logicalSize = pixmap.deviceIndependentSize();

    QVERIFY(!pixmap.isNull());
    QVERIFY(pixmap.devicePixelRatio() >= 2.0);
    QCOMPARE(cursor.hotSpot(),
             QPoint(qRound(logicalSize.width() / 2.0), qRound(logicalSize.height() / 2.0)));
}

void TestCursorStyleCatalog::testEraserBrushUsesCenteredHotspot()
{
    CursorStyleSpec spec;
    spec.styleId = CursorStyleId::EraserBrush;
    spec.primaryValue = 24;

    const QCursor cursor = CursorStyleCatalog::instance().cursorForStyle(spec);
    const QPixmap pixmap = cursor.pixmap();
    const QSizeF logicalSize = pixmap.deviceIndependentSize();

    QVERIFY(!pixmap.isNull());
    QVERIFY(pixmap.devicePixelRatio() >= 2.0);
    QCOMPARE(cursor.hotSpot(),
             QPoint(qRound(logicalSize.width() / 2.0), qRound(logicalSize.height() / 2.0)));
}

QTEST_MAIN(TestCursorStyleCatalog)
#include "tst_CursorStyleCatalog.moc"
