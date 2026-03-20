#include <QtTest/QtTest>

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>

#include "qml/CanvasToolbarViewModel.h"
#include "qml/QmlFloatingToolbar.h"

class tst_QmlFloatingToolbar : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testSizeHintAndPreShowPosition();
    void testDragHandleSignalsReachBridge();
};

void tst_QmlFloatingToolbar::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for QmlFloatingToolbar tests in this environment.");
    }
}

void tst_QmlFloatingToolbar::testSizeHintAndPreShowPosition()
{
    CanvasToolbarViewModel viewModel;
    SnapTray::QmlFloatingToolbar toolbar(&viewModel);

    const QSize size = toolbar.sizeHint();
    QVERIFY(!size.isEmpty());
    QVERIFY(!toolbar.isVisible());

    const QPoint expectedTopLeft(120, 84);
    toolbar.setPosition(expectedTopLeft);

    QCOMPARE(toolbar.geometry().topLeft(), expectedTopLeft);
    QCOMPARE(toolbar.geometry().size(), size);
    QVERIFY(!toolbar.isVisible());
}

void tst_QmlFloatingToolbar::testDragHandleSignalsReachBridge()
{
    CanvasToolbarViewModel viewModel;
    SnapTray::QmlFloatingToolbar toolbar(&viewModel);

    QVERIFY(!toolbar.sizeHint().isEmpty());

    auto* view = qobject_cast<QQuickView*>(toolbar.window());
    QVERIFY(view != nullptr);

    QQuickItem* root = view->rootObject();
    QVERIFY(root != nullptr);
    QVERIFY(root->findChild<QObject*>(QStringLiteral("toolbarDragHandle")) != nullptr);

    QSignalSpy startedSpy(&toolbar, &SnapTray::QmlFloatingToolbar::dragStarted);
    QSignalSpy movedSpy(&toolbar, &SnapTray::QmlFloatingToolbar::dragMoved);
    QSignalSpy finishedSpy(&toolbar, &SnapTray::QmlFloatingToolbar::dragFinished);

    QVERIFY(QMetaObject::invokeMethod(root, "dragStarted"));
    QVERIFY(QMetaObject::invokeMethod(
        root, "dragMoved", Q_ARG(double, 12.0), Q_ARG(double, -4.0)));
    QVERIFY(QMetaObject::invokeMethod(root, "dragFinished"));

    QCOMPARE(startedSpy.count(), 1);
    QCOMPARE(movedSpy.count(), 1);
    QCOMPARE(finishedSpy.count(), 1);
}

QTEST_MAIN(tst_QmlFloatingToolbar)
#include "tst_FloatingToolbar.moc"
