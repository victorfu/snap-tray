#include <QtTest/QtTest>

#include "colorwidgets/ColorPickerDialogCompat.h"
#include "qml/QmlDialog.h"
#include "qml/QmlToast.h"

#include <QGuiApplication>

class tst_PlacementHelpers : public QObject
{
    Q_OBJECT

private slots:
    void testQmlDialog_centeredTopLeftForBounds();
    void testColorPicker_preferredPlacementBounds();
    void testColorPicker_centeredTopLeftForBounds();
    void testQmlToast_preferredScreenGeometryForScreenToast();
    void testQmlToast_screenTopRightPositionForGeometry();
    void testQmlToast_screenToastCleanupForShutdown();
};

void tst_PlacementHelpers::testQmlDialog_centeredTopLeftForBounds()
{
    const QPoint topLeft = SnapTray::QmlDialog::centeredTopLeftForBounds(
        QRect(100, 200, 800, 600),
        QSize(200, 100));

    QCOMPARE(topLeft, QPoint(399, 449));
}

void tst_PlacementHelpers::testColorPicker_preferredPlacementBounds()
{
    const QRect parentBounds(10, 20, 300, 200);
    const QRect anchorBounds(1000, 2000, 500, 400);
    const QRect primaryBounds(50, 60, 700, 500);

    QCOMPARE(
        snaptray::colorwidgets::ColorPickerDialogCompat::preferredPlacementBounds(
            parentBounds, anchorBounds, primaryBounds),
        parentBounds);
    QCOMPARE(
        snaptray::colorwidgets::ColorPickerDialogCompat::preferredPlacementBounds(
            QRect(), anchorBounds, primaryBounds),
        anchorBounds);
    QCOMPARE(
        snaptray::colorwidgets::ColorPickerDialogCompat::preferredPlacementBounds(
            QRect(), QRect(), primaryBounds),
        primaryBounds);
}

void tst_PlacementHelpers::testColorPicker_centeredTopLeftForBounds()
{
    const QPoint topLeft =
        snaptray::colorwidgets::ColorPickerDialogCompat::centeredTopLeftForBounds(
            QRect(0, 0, 600, 400),
            QSize(120, 80));

    QCOMPARE(topLeft, QPoint(240, 160));
}

void tst_PlacementHelpers::testQmlToast_preferredScreenGeometryForScreenToast()
{
    const QRect activeGeometry(0, 0, 100, 100);
    const QRect cursorGeometry(100, 100, 200, 200);
    const QRect primaryGeometry(200, 200, 300, 300);

    QCOMPARE(
        SnapTray::QmlToast::preferredScreenGeometryForScreenToast(
            activeGeometry, cursorGeometry, primaryGeometry),
        activeGeometry);
    QCOMPARE(
        SnapTray::QmlToast::preferredScreenGeometryForScreenToast(
            QRect(), cursorGeometry, primaryGeometry),
        cursorGeometry);
    QCOMPARE(
        SnapTray::QmlToast::preferredScreenGeometryForScreenToast(
            QRect(), QRect(), primaryGeometry),
        primaryGeometry);
}

void tst_PlacementHelpers::testQmlToast_screenTopRightPositionForGeometry()
{
    const QPoint topLeft = SnapTray::QmlToast::screenTopRightPositionForGeometry(
        QRect(100, 200, 800, 600),
        QSize(200, 60),
        20);

    QCOMPARE(topLeft, QPoint(679, 220));
}

void tst_PlacementHelpers::testQmlToast_screenToastCleanupForShutdown()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("QQuickView shutdown cleanup requires a real screen");
    }

    auto& toast = SnapTray::QmlToast::screenToast();

    toast.showToast(SnapTray::QmlToast::Level::Info,
                    QStringLiteral("Test title"),
                    QStringLiteral("Test message"),
                    100);
    QCoreApplication::processEvents();

    QVERIFY(QMetaObject::invokeMethod(&toast, "cleanupForShutdown", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(&toast, "cleanupForShutdown", Qt::DirectConnection));

    toast.showToast(SnapTray::QmlToast::Level::Success,
                    QStringLiteral("Shown again"),
                    QStringLiteral("Recreated after cleanup"),
                    100);
    QCoreApplication::processEvents();

    QVERIFY(QMetaObject::invokeMethod(&toast, "cleanupForShutdown", Qt::DirectConnection));
}

QTEST_MAIN(tst_PlacementHelpers)
#include "tst_PlacementHelpers.moc"
