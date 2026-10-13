#include <QtTest/QtTest>

#include "capture/QuickCaptureTarget.h"

using snaptray::capture::QuickCaptureTarget;
using snaptray::capture::findFrontmostWindow;
using snaptray::capture::resolveQuickCaptureRegion;

namespace {

const QRect kScreen(0, 0, 1920, 1080);

DetectedElement makeElement(
    const QRect& bounds,
    const QString& title,
    ElementType elementType = ElementType::Window)
{
    DetectedElement element;
    element.bounds = bounds;
    element.windowTitle = title;
    element.ownerApp = QStringLiteral("App ") + title;
    element.windowLayer = 0;
    element.windowId = 1;
    element.elementType = elementType;
    return element;
}

} // namespace

class tst_QuickCaptureTarget : public QObject
{
    Q_OBJECT

private slots:
    void frontmostWindow_returnsFirstWindow();
    void frontmostWindow_skipsNonWindowElements();
    void frontmostWindow_acceptsDialogs();
    void frontmostWindow_skipsWindowsOffScreen();
    void frontmostWindow_skipsSlivers();
    void frontmostWindow_emptyList();
    void resolve_fullScreenIgnoresWindow();
    void resolve_activeWindowUsesWindowBoundsAndMetadata();
    void resolve_activeWindowClipsToScreen();
    void resolve_activeWindowFallsBackToFullScreen();
    void resolve_respectsScreenOffset();
};

void tst_QuickCaptureTarget::frontmostWindow_returnsFirstWindow()
{
    const std::vector<DetectedElement> windows{
        makeElement(QRect(100, 100, 800, 600), QStringLiteral("Front")),
        makeElement(QRect(0, 0, 1920, 1080), QStringLiteral("Back")),
    };

    const auto window = findFrontmostWindow(windows, kScreen);
    QVERIFY(window.has_value());
    QCOMPARE(window->windowTitle, QStringLiteral("Front"));
}

void tst_QuickCaptureTarget::frontmostWindow_skipsNonWindowElements()
{
    const std::vector<DetectedElement> windows{
        makeElement(QRect(10, 10, 200, 300), QStringLiteral("Menu"), ElementType::ContextMenu),
        makeElement(QRect(0, 0, 400, 30), QStringLiteral("Tray"), ElementType::StatusBarItem),
        makeElement(QRect(100, 100, 800, 600), QStringLiteral("Editor")),
    };

    const auto window = findFrontmostWindow(windows, kScreen);
    QVERIFY(window.has_value());
    QCOMPARE(window->windowTitle, QStringLiteral("Editor"));
}

void tst_QuickCaptureTarget::frontmostWindow_acceptsDialogs()
{
    const std::vector<DetectedElement> windows{
        makeElement(QRect(500, 300, 400, 200), QStringLiteral("Save As"), ElementType::Dialog),
        makeElement(QRect(100, 100, 800, 600), QStringLiteral("Editor")),
    };

    const auto window = findFrontmostWindow(windows, kScreen);
    QVERIFY(window.has_value());
    QCOMPARE(window->windowTitle, QStringLiteral("Save As"));
}

void tst_QuickCaptureTarget::frontmostWindow_skipsWindowsOffScreen()
{
    const std::vector<DetectedElement> windows{
        makeElement(QRect(2000, 100, 800, 600), QStringLiteral("Other Screen")),
        makeElement(QRect(100, 100, 800, 600), QStringLiteral("Editor")),
    };

    const auto window = findFrontmostWindow(windows, kScreen);
    QVERIFY(window.has_value());
    QCOMPARE(window->windowTitle, QStringLiteral("Editor"));
}

void tst_QuickCaptureTarget::frontmostWindow_skipsSlivers()
{
    const std::vector<DetectedElement> windows{
        makeElement(QRect(1916, 100, 800, 600), QStringLiteral("Edge")),
        makeElement(QRect(100, 100, 800, 600), QStringLiteral("Editor")),
    };

    const auto window = findFrontmostWindow(windows, kScreen);
    QVERIFY(window.has_value());
    QCOMPARE(window->windowTitle, QStringLiteral("Editor"));
}

void tst_QuickCaptureTarget::frontmostWindow_emptyList()
{
    QVERIFY(!findFrontmostWindow({}, kScreen).has_value());
}

void tst_QuickCaptureTarget::resolve_fullScreenIgnoresWindow()
{
    const auto window = makeElement(QRect(100, 100, 800, 600), QStringLiteral("Editor"));
    const auto region = resolveQuickCaptureRegion(QuickCaptureTarget::FullScreen, kScreen, window);

    QCOMPARE(region.globalRect, kScreen);
    QVERIFY(!region.isWindow);
    QVERIFY(region.windowTitle.isEmpty());
    QVERIFY(region.ownerApp.isEmpty());
}

void tst_QuickCaptureTarget::resolve_activeWindowUsesWindowBoundsAndMetadata()
{
    const auto window = makeElement(QRect(100, 100, 800, 600), QStringLiteral("Editor"));
    const auto region = resolveQuickCaptureRegion(QuickCaptureTarget::ActiveWindow, kScreen, window);

    QCOMPARE(region.globalRect, QRect(100, 100, 800, 600));
    QVERIFY(region.isWindow);
    QCOMPARE(region.windowTitle, QStringLiteral("Editor"));
    QCOMPARE(region.ownerApp, QStringLiteral("App Editor"));
}

void tst_QuickCaptureTarget::resolve_activeWindowClipsToScreen()
{
    const auto window = makeElement(QRect(-50, 900, 800, 600), QStringLiteral("Editor"));
    const auto region = resolveQuickCaptureRegion(QuickCaptureTarget::ActiveWindow, kScreen, window);

    QCOMPARE(region.globalRect, QRect(0, 900, 750, 180));
    QVERIFY(region.isWindow);
}

void tst_QuickCaptureTarget::resolve_activeWindowFallsBackToFullScreen()
{
    const auto region =
        resolveQuickCaptureRegion(QuickCaptureTarget::ActiveWindow, kScreen, std::nullopt);
    QCOMPARE(region.globalRect, kScreen);
    QVERIFY(!region.isWindow);

    const auto offScreen = makeElement(QRect(3000, 0, 800, 600), QStringLiteral("Far"));
    const auto offScreenRegion =
        resolveQuickCaptureRegion(QuickCaptureTarget::ActiveWindow, kScreen, offScreen);
    QCOMPARE(offScreenRegion.globalRect, kScreen);
    QVERIFY(!offScreenRegion.isWindow);
}

void tst_QuickCaptureTarget::resolve_respectsScreenOffset()
{
    const QRect secondScreen(1920, -200, 2560, 1440);
    const std::vector<DetectedElement> windows{
        makeElement(QRect(100, 100, 800, 600), QStringLiteral("Primary")),
        makeElement(QRect(2000, 0, 1000, 700), QStringLiteral("Secondary")),
    };

    const auto window = findFrontmostWindow(windows, secondScreen);
    QVERIFY(window.has_value());
    QCOMPARE(window->windowTitle, QStringLiteral("Secondary"));

    const auto region =
        resolveQuickCaptureRegion(QuickCaptureTarget::ActiveWindow, secondScreen, window);
    QCOMPARE(region.globalRect, QRect(2000, 0, 1000, 700));
}

QTEST_MAIN(tst_QuickCaptureTarget)
#include "tst_QuickCaptureTarget.moc"
