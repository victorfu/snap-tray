#include <QtTest/QtTest>

#include "cursor/CursorStyleCatalog.h"

namespace {
void verifyMoveCursor(const QCursor& cursor)
{
#ifdef Q_OS_MACOS
    const QCursor expected =
        CursorStyleCatalog::instance().cursorForStyle(CursorStyleSpec::fromShape(Qt::SizeAllCursor));
    QCOMPARE(cursor.shape(), Qt::BitmapCursor);
    QVERIFY(!cursor.pixmap().isNull());
    QCOMPARE(cursor.hotSpot(), expected.hotSpot());
    QCOMPARE(cursor.pixmap().deviceIndependentSize(),
             expected.pixmap().deviceIndependentSize());
#else
    QCOMPARE(cursor.shape(), Qt::SizeAllCursor);
#endif
}
}  // namespace

class TestCursorStyleCatalog : public QObject
{
    Q_OBJECT

private slots:
    void testSystemShapeMapping();
    void testMoveCursorUsesPlatformSafeMapping();
    void testLegacyCursorPassthrough();
    void testMosaicBrushUsesCenteredHotspot();
    void testEraserBrushUsesCenteredHotspot();
    void testMosaicBrushUsesStableCachedPixmap();
    void testEraserBrushUsesStableCachedPixmap();
};

void TestCursorStyleCatalog::testSystemShapeMapping()
{
    const auto spec = CursorStyleSpec::fromShape(Qt::PointingHandCursor);
    QCOMPARE(spec.styleId, CursorStyleId::PointingHand);
    QCOMPARE(CursorStyleCatalog::instance().cursorForStyle(spec).shape(), Qt::PointingHandCursor);
}

void TestCursorStyleCatalog::testMoveCursorUsesPlatformSafeMapping()
{
    const auto spec = CursorStyleSpec::fromShape(Qt::SizeAllCursor);
    QCOMPARE(spec.styleId, CursorStyleId::Move);
    verifyMoveCursor(CursorStyleCatalog::instance().cursorForStyle(spec));
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

void TestCursorStyleCatalog::testMosaicBrushUsesStableCachedPixmap()
{
    CursorStyleSpec spec;
    spec.styleId = CursorStyleId::MosaicBrush;
    spec.primaryValue = 18;

    const QCursor first = CursorStyleCatalog::instance().cursorForStyle(spec);
    const QCursor second = CursorStyleCatalog::instance().cursorForStyle(spec);

    QCOMPARE(first.pixmap().cacheKey(), second.pixmap().cacheKey());
    QCOMPARE(first.hotSpot(), second.hotSpot());
}

void TestCursorStyleCatalog::testEraserBrushUsesStableCachedPixmap()
{
    CursorStyleSpec spec;
    spec.styleId = CursorStyleId::EraserBrush;
    spec.primaryValue = 24;

    const QCursor first = CursorStyleCatalog::instance().cursorForStyle(spec);
    const QCursor second = CursorStyleCatalog::instance().cursorForStyle(spec);

    QCOMPARE(first.pixmap().cacheKey(), second.pixmap().cacheKey());
    QCOMPARE(first.hotSpot(), second.hotSpot());
}

QTEST_MAIN(TestCursorStyleCatalog)
#include "tst_CursorStyleCatalog.moc"
