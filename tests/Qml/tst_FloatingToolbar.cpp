#include <QtTest/QtTest>

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>

#include "qml/CanvasToolbarViewModel.h"
#include "qml/QmlFloatingToolbar.h"
#include "qml/QmlWindowedToolbar.h"

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

namespace {
#ifdef Q_OS_WIN
void verifyFramelessToolWindow(QWindow* window)
{
    QVERIFY(window != nullptr);

    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    QVERIFY(hwnd != nullptr);

    const LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    QVERIFY((style & WS_CAPTION) == 0);
    QVERIFY((style & WS_THICKFRAME) == 0);
    QVERIFY((style & WS_SYSMENU) == 0);
    QVERIFY((style & WS_MINIMIZEBOX) == 0);
    QVERIFY((style & WS_MAXIMIZEBOX) == 0);

    const LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    QVERIFY((exStyle & WS_EX_TOOLWINDOW) != 0);
    QVERIFY((exStyle & WS_EX_APPWINDOW) == 0);
}
#endif
} // namespace

class tst_QmlFloatingToolbar : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testSizeHintAndPreShowPosition();
    void testDragHandleSignalsReachBridge();
#ifdef Q_OS_WIN
    void testFloatingToolbarStripsNativeWindowChromeOnWindows();
    void testWindowedToolbarStripsNativeWindowChromeOnWindows();
#endif
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

#ifdef Q_OS_WIN
void tst_QmlFloatingToolbar::testFloatingToolbarStripsNativeWindowChromeOnWindows()
{
    CanvasToolbarViewModel viewModel;
    SnapTray::QmlFloatingToolbar toolbar(&viewModel);

    toolbar.show();
    QCoreApplication::processEvents();

    verifyFramelessToolWindow(toolbar.window());
    toolbar.close();
}

void tst_QmlFloatingToolbar::testWindowedToolbarStripsNativeWindowChromeOnWindows()
{
    SnapTray::QmlWindowedToolbar toolbar;

    toolbar.show();
    QCoreApplication::processEvents();

    verifyFramelessToolWindow(toolbar.window());
    toolbar.close();
}
#endif

QTEST_MAIN(tst_QmlFloatingToolbar)
#include "tst_FloatingToolbar.moc"
