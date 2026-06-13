#include <QtTest/QtTest>

#include "colorwidgets/ColorPickerDialogCompat.h"
#include "qml/QmlDialog.h"
#include "qml/QmlToast.h"

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>

namespace {
QQuickView* findToastViewWithTitle(const QString& title)
{
    const auto windows = QGuiApplication::topLevelWindows();
    for (QWindow* window : windows) {
        auto* view = qobject_cast<QQuickView*>(window);
        if (!view || !view->rootObject()) {
            continue;
        }

        if (view->rootObject()->property("title").toString() == title) {
            return view;
        }
    }

    return nullptr;
}
}

class tst_PlacementHelpers : public QObject
{
    Q_OBJECT

private slots:
    void testQmlDialog_centeredTopLeftForBounds();
    void testColorPicker_preferredPlacementBounds();
    void testColorPicker_centeredTopLeftForBounds();
    void testQmlToast_preferredScreenGeometryForScreenToast();
    void testQmlToast_screenTopRightPositionForGeometry();
    void testQmlToast_screenToastDoesNotAcceptInputOrFocus();
    void testQmlToast_hidesNativeWindowAfterDisplayDuration();
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

void tst_PlacementHelpers::testQmlToast_screenToastDoesNotAcceptInputOrFocus()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("QQuickView flag checks require a real screen");
    }

    const QString title = QStringLiteral("Native input transparency regression");
    auto& toast = SnapTray::QmlToast::screenToast();

    toast.showToast(SnapTray::QmlToast::Level::Info,
                    title,
                    QStringLiteral("Input-transparent toast"),
                    100);

    QQuickView* view = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((view = findToastViewWithTitle(title)) != nullptr, 1000);
    QVERIFY(view->flags().testFlag(Qt::WindowTransparentForInput));
    QVERIFY(view->flags().testFlag(Qt::WindowDoesNotAcceptFocus));
}

void tst_PlacementHelpers::testQmlToast_hidesNativeWindowAfterDisplayDuration()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("QQuickView visibility checks require a real screen");
    }

    const QString title = QStringLiteral("Native window hide regression");
    auto& toast = SnapTray::QmlToast::screenToast();

    toast.showToast(SnapTray::QmlToast::Level::Info,
                    title,
                    QStringLiteral("Short-lived toast"),
                    100);

    QQuickView* view = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((view = findToastViewWithTitle(title)) != nullptr, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(view->isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(!view->isVisible(), 3000);
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
