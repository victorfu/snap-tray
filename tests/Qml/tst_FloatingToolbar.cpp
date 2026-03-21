#include <QtTest/QtTest>

#include <QGuiApplication>
#include <QPointer>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QDateTime>
#include <QVariantList>
#include <QWidget>

#include "qml/CanvasToolbarViewModel.h"
#include "qml/PinToolbarViewModel.h"
#include "qml/PinToolOptionsViewModel.h"
#include "qml/QmlDialog.h"
#include "qml/QmlEmojiPickerPopup.h"
#include "qml/QmlFloatingSubToolbar.h"
#include "qml/QmlFloatingToolbar.h"
#include "qml/QmlWindowedToolbar.h"
#include "qml/ShareResultViewModel.h"

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

QWindow* findNewVisibleTopLevelWindow(const QList<QWindow*>& before)
{
    const auto windows = QGuiApplication::topLevelWindows();
    for (QWindow* window : windows) {
        if (!before.contains(window) && window && window->isVisible()) {
            return window;
        }
    }
    return nullptr;
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
    void testFloatingToolbarRetainsFramelessChromeAfterParentChangeOnWindows();
    void testFloatingSubToolbarRetainsFramelessChromeAfterParentChangeOnWindows();
    void testEmojiPickerRetainsFramelessChromeAfterParentChangeOnWindows();
    void testWindowedToolbarStripsNativeWindowChromeOnWindows();
    void testWindowedToolbarUsesAssociatedPinWindowAsTransientParentOnWindows();
    void testWindowedToolbarTooltipUsesToolbarAsTransientParentOnWindows();
    void testQmlDialogUsesParentWidgetAsTransientParentOnWindows();
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

void tst_QmlFloatingToolbar::testFloatingToolbarRetainsFramelessChromeAfterParentChangeOnWindows()
{
    CanvasToolbarViewModel viewModel;
    SnapTray::QmlFloatingToolbar toolbar(&viewModel);
    QWidget host;

    host.show();
    QTRY_VERIFY(host.windowHandle() != nullptr);

    toolbar.show();
    QCoreApplication::processEvents();
    verifyFramelessToolWindow(toolbar.window());

    toolbar.setParentWidget(&host);
    QCoreApplication::processEvents();

    verifyFramelessToolWindow(toolbar.window());
    toolbar.close();
}

void tst_QmlFloatingToolbar::testFloatingSubToolbarRetainsFramelessChromeAfterParentChangeOnWindows()
{
    PinToolOptionsViewModel viewModel;
    SnapTray::QmlFloatingSubToolbar toolbar(&viewModel);
    QWidget host;

    host.show();
    QTRY_VERIFY(host.windowHandle() != nullptr);

    toolbar.show();
    QCoreApplication::processEvents();
    verifyFramelessToolWindow(toolbar.window());

    toolbar.setParentWidget(&host);
    QCoreApplication::processEvents();

    verifyFramelessToolWindow(toolbar.window());
    toolbar.close();
}

void tst_QmlFloatingToolbar::testEmojiPickerRetainsFramelessChromeAfterParentChangeOnWindows()
{
    SnapTray::QmlEmojiPickerPopup popup;
    QWidget firstHost;
    QWidget secondHost;

    firstHost.show();
    secondHost.show();
    QTRY_VERIFY(firstHost.windowHandle() != nullptr);
    QTRY_VERIFY(secondHost.windowHandle() != nullptr);

    popup.setParentWidget(&firstHost);
    popup.showAt(QRect(160, 120, 40, 40));
    QCoreApplication::processEvents();
    verifyFramelessToolWindow(popup.window());

    popup.setParentWidget(&secondHost);
    QCoreApplication::processEvents();

    verifyFramelessToolWindow(popup.window());
    popup.close();
}

void tst_QmlFloatingToolbar::testWindowedToolbarStripsNativeWindowChromeOnWindows()
{
    SnapTray::QmlWindowedToolbar toolbar;

    toolbar.show();
    QCoreApplication::processEvents();

    verifyFramelessToolWindow(toolbar.window());
    toolbar.close();
}

void tst_QmlFloatingToolbar::testWindowedToolbarUsesAssociatedPinWindowAsTransientParentOnWindows()
{
    SnapTray::QmlWindowedToolbar toolbar;
    QWidget host;

    host.show();
    QTRY_VERIFY(host.windowHandle() != nullptr);

    toolbar.setAssociatedWidgets(&host, nullptr);
    toolbar.show();
    QCoreApplication::processEvents();

    QVERIFY(toolbar.window() != nullptr);
    QCOMPARE(toolbar.window()->transientParent(), host.windowHandle());
    verifyFramelessToolWindow(toolbar.window());
    toolbar.close();
}

void tst_QmlFloatingToolbar::testWindowedToolbarTooltipUsesToolbarAsTransientParentOnWindows()
{
    SnapTray::QmlWindowedToolbar toolbar;
    toolbar.show();
    QCoreApplication::processEvents();

    auto* view = qobject_cast<QQuickView*>(toolbar.window());
    QVERIFY(view != nullptr);

    QQuickItem* root = view->rootObject();
    QVERIFY(root != nullptr);

    const QVariantList buttons = toolbar.viewModel()->buttons();
    QVERIFY(!buttons.isEmpty());
    const int buttonId = buttons.constFirst().toMap().value(QStringLiteral("id")).toInt();

    QVERIFY(QMetaObject::invokeMethod(
        root,
        "buttonHovered",
        Q_ARG(int, buttonId),
        Q_ARG(double, 10.0),
        Q_ARG(double, 8.0),
        Q_ARG(double, 28.0),
        Q_ARG(double, 24.0)));

    QTRY_VERIFY(toolbar.tooltipWindow() != nullptr);
    QTRY_VERIFY(toolbar.tooltipWindow()->isVisible());

    QCOMPARE(toolbar.tooltipWindow()->transientParent(), toolbar.window());
    verifyFramelessToolWindow(toolbar.tooltipWindow());
    toolbar.close();
}

void tst_QmlFloatingToolbar::testQmlDialogUsesParentWidgetAsTransientParentOnWindows()
{
    QWidget host;
    host.show();
    QTRY_VERIFY(host.windowHandle() != nullptr);

    ShareResultViewModel viewModel;
    viewModel.setResult(QStringLiteral("https://example.com/share/abc"),
                        QDateTime::currentDateTimeUtc().addDays(1),
                        false);

    const QList<QWindow*> beforeWindows = QGuiApplication::topLevelWindows();

    QPointer<SnapTray::QmlDialog> dialog = new SnapTray::QmlDialog(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/dialogs/ShareResultDialog.qml")),
        &viewModel,
        QStringLiteral("viewModel"),
        &host);
    dialog->showAt();

    QWindow* dialogWindow = nullptr;
    QTRY_VERIFY((dialogWindow = findNewVisibleTopLevelWindow(beforeWindows)) != nullptr);

    QCOMPARE(dialogWindow->transientParent(), host.windowHandle());
    verifyFramelessToolWindow(dialogWindow);
    dialog->close();
    QTRY_VERIFY(dialog.isNull());
}
#endif

QTEST_MAIN(tst_QmlFloatingToolbar)
#include "tst_FloatingToolbar.moc"
