#include <QtTest/QtTest>

#include <QGuiApplication>
#include <QScreen>

#include "ScreenCanvas.h"
#include "ScreenCanvasSession.h"
#include "qml/QmlFloatingToolbar.h"
#include "settings/ScreenCanvasSettingsManager.h"

namespace {
constexpr int kToolbarMargin = 8;
constexpr int kViewportInset = 10;
constexpr int kInsideInset = 8;

QScreen* primaryOrFirstScreen()
{
    if (QScreen* primaryScreen = QGuiApplication::primaryScreen()) {
        return primaryScreen;
    }

    const QList<QScreen*> screens = QGuiApplication::screens();
    return screens.isEmpty() ? nullptr : screens.first();
}

QRect clampBoundsForScreen(QScreen* screen)
{
    if (!screen) {
        return {};
    }

    return screen->availableGeometry().isValid()
        ? screen->availableGeometry()
        : screen->geometry();
}

qint64 overlapArea(const QRect& a, const QRect& b)
{
    const QRect intersection = a.intersected(b);
    if (!intersection.isValid() || intersection.isEmpty()) {
        return 0;
    }

    return static_cast<qint64>(intersection.width()) * intersection.height();
}

QRect insetViewportRect(const QRect& viewportRect, int inset)
{
    if (!viewportRect.isValid()) {
        return {};
    }

    const QRect insetRect = viewportRect.adjusted(inset, inset, -inset, -inset);
    return insetRect.isValid() && !insetRect.isEmpty() ? insetRect : viewportRect;
}

QRect insideSafeRect(const QRect& selectionRect)
{
    if (!selectionRect.isValid() || selectionRect.isEmpty()) {
        return {};
    }

    const QRect safeRect = selectionRect.adjusted(
        kInsideInset, kInsideInset, -kInsideInset, -kInsideInset);
    return safeRect.isValid() && !safeRect.isEmpty() ? safeRect : QRect();
}

qint64 selectionEdgeOverlapArea(const QRect& toolbarRect, const QRect& selectionRect)
{
    const QRect normalizedSelection = selectionRect.normalized();
    const QRect safeRect = insideSafeRect(normalizedSelection);
    const qint64 selectionOverlap = overlapArea(toolbarRect, normalizedSelection);
    const qint64 safeOverlap = safeRect.isValid() ? overlapArea(toolbarRect, safeRect) : 0;
    return qMax<qint64>(0, selectionOverlap - safeOverlap);
}

QPoint outsideBelowRightTopLeft(const QRect& selectionRect, const QSize& toolbarSize)
{
    return QPoint(selectionRect.right() + 1 - toolbarSize.width(),
                  selectionRect.bottom() + kToolbarMargin);
}

QPoint outsideAboveRightTopLeft(const QRect& selectionRect, const QSize& toolbarSize)
{
    return QPoint(selectionRect.right() + 1 - toolbarSize.width(),
                  selectionRect.top() - toolbarSize.height() - kToolbarMargin);
}

QPoint insideBottomRightTopLeft(const QRect& selectionRect, const QSize& toolbarSize)
{
    return QPoint(selectionRect.right() + 1 - kInsideInset - toolbarSize.width(),
                  selectionRect.bottom() + 1 - kInsideInset - toolbarSize.height());
}

QPoint insideTopRightTopLeft(const QRect& selectionRect, const QSize& toolbarSize)
{
    return QPoint(selectionRect.right() + 1 - kInsideInset - toolbarSize.width(),
                  selectionRect.top() + kInsideInset);
}

}

class TestScreenCanvasPlacement : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void testSettingsRoundTrip();
    void testResolveToolbarPlacementUsesSavedScreen();
    void testResolveToolbarPlacementFallsBackToActivationScreen();
    void testResolveToolbarPlacementDefaultsToActivationScreenBottomCenter();
    void testVirtualDesktopGeometryMatchesScreenUnion();
    void testDesktopCoordinateRoundTrip();
    void testSurfaceInitializationUsesScreenGeometry();
    void testToolbarDragClampSnapsToNearestVisibleScreen();
    void testSelectionToolbarPlacementPrefersBelowRightWhenClear();
    void testSelectionToolbarPlacementUsesAboveRightWhenBottomSpaceTight();
    void testSelectionToolbarPlacementPrefersOutsideBelowWhenOnlyAboveBlocked();
    void testSelectionToolbarPlacementUsesWidgetTopForAboveFallback();
    void testSelectionToolbarPlacementUsesInsideBottomRightWhenOutsideCandidatesBlocked();
    void testSelectionToolbarPlacementUsesInsideTopRightWhenInsideBottomBlocked();
    void testSelectionToolbarPlacementAvoidsSelectionOverlapInBottomRightCorner();
    void testSelectionToolbarPlacementPrefersInsideSafeCandidateOverLegacyTopClamp();

private:
    ScreenCanvasToolbarPlacement m_originalPlacement;
    bool m_hadOriginalPlacement = false;
};

void TestScreenCanvasPlacement::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for ScreenCanvas tests in this environment.");
    }
}

void TestScreenCanvasPlacement::init()
{
    auto& settingsManager = ScreenCanvasSettingsManager::instance();
    m_originalPlacement = settingsManager.loadToolbarPlacement();
    m_hadOriginalPlacement = m_originalPlacement.isValid();
    settingsManager.clearToolbarPlacement();
}

void TestScreenCanvasPlacement::cleanup()
{
    auto& settingsManager = ScreenCanvasSettingsManager::instance();
    if (m_hadOriginalPlacement) {
        settingsManager.saveToolbarPlacement(
            m_originalPlacement.screenId,
            m_originalPlacement.topLeftInScreen);
    } else {
        settingsManager.clearToolbarPlacement();
    }
}

void TestScreenCanvasPlacement::testSettingsRoundTrip()
{
    auto& settingsManager = ScreenCanvasSettingsManager::instance();

    QVERIFY(settingsManager.loadToolbarScreenId().isEmpty());
    QCOMPARE(settingsManager.loadToolbarPositionInScreen(), QPoint());

    settingsManager.saveToolbarPlacement(QStringLiteral("screen-a"), QPoint(12, 34));

    QCOMPARE(settingsManager.loadToolbarScreenId(), QStringLiteral("screen-a"));
    QCOMPARE(settingsManager.loadToolbarPositionInScreen(), QPoint(12, 34));

    const ScreenCanvasToolbarPlacement placement = settingsManager.loadToolbarPlacement();
    QVERIFY(placement.isValid());
    QCOMPARE(placement.screenId, QStringLiteral("screen-a"));
    QCOMPARE(placement.topLeftInScreen, QPoint(12, 34));
}

void TestScreenCanvasPlacement::testResolveToolbarPlacementUsesSavedScreen()
{
    const QList<QScreen*> screens = QGuiApplication::screens();
    QScreen* activationScreen = primaryOrFirstScreen();
    QVERIFY(activationScreen);

    QScreen* savedScreen = screens.size() > 1 ? screens.last() : activationScreen;
    QVERIFY(savedScreen);

    const QSize toolbarSize(220, 72);
    const ScreenCanvasToolbarPlacement savedPlacement{
        ScreenCanvasSettingsManager::screenIdentifier(savedScreen),
        QPoint(24, 36)
    };

    const auto resolution = ScreenCanvasSession::resolveToolbarPlacement(
        savedPlacement,
        screens,
        activationScreen,
        toolbarSize);

    const QPoint expectedGlobalTopLeft = ScreenCanvasSession::clampToolbarTopLeftToBounds(
        savedScreen->geometry().topLeft() + savedPlacement.topLeftInScreen,
        clampBoundsForScreen(savedScreen),
        toolbarSize);

    QVERIFY(resolution.usedStoredPlacement);
    QCOMPARE(resolution.screenId, ScreenCanvasSettingsManager::screenIdentifier(savedScreen));
    QCOMPARE(resolution.globalTopLeft, expectedGlobalTopLeft);
    QCOMPARE(resolution.topLeftInScreen,
             expectedGlobalTopLeft - savedScreen->geometry().topLeft());
}

void TestScreenCanvasPlacement::testResolveToolbarPlacementFallsBackToActivationScreen()
{
    const QList<QScreen*> screens = QGuiApplication::screens();
    QScreen* activationScreen = primaryOrFirstScreen();
    QVERIFY(activationScreen);

    const QSize toolbarSize(220, 72);
    const ScreenCanvasToolbarPlacement savedPlacement{
        QStringLiteral("missing-screen"),
        QPoint(99999, 99999)
    };

    const auto resolution = ScreenCanvasSession::resolveToolbarPlacement(
        savedPlacement,
        screens,
        activationScreen,
        toolbarSize);

    const QPoint expectedGlobalTopLeft = ScreenCanvasSession::clampToolbarTopLeftToBounds(
        activationScreen->geometry().topLeft() + savedPlacement.topLeftInScreen,
        clampBoundsForScreen(activationScreen),
        toolbarSize);

    QVERIFY(resolution.usedStoredPlacement);
    QCOMPARE(resolution.screenId, ScreenCanvasSettingsManager::screenIdentifier(activationScreen));
    QCOMPARE(resolution.globalTopLeft, expectedGlobalTopLeft);
    QCOMPARE(resolution.topLeftInScreen,
             expectedGlobalTopLeft - activationScreen->geometry().topLeft());
}

void TestScreenCanvasPlacement::testResolveToolbarPlacementDefaultsToActivationScreenBottomCenter()
{
    const QList<QScreen*> screens = QGuiApplication::screens();
    QScreen* activationScreen = primaryOrFirstScreen();
    QVERIFY(activationScreen);

    const QSize toolbarSize(220, 72);
    const ScreenCanvasToolbarPlacement savedPlacement;

    const auto resolution = ScreenCanvasSession::resolveToolbarPlacement(
        savedPlacement,
        screens,
        activationScreen,
        toolbarSize);

    const QPoint expectedGlobalTopLeft = ScreenCanvasSession::defaultToolbarTopLeftForScreen(
        clampBoundsForScreen(activationScreen),
        toolbarSize);

    QVERIFY(!resolution.usedStoredPlacement);
    QCOMPARE(resolution.screenId, ScreenCanvasSettingsManager::screenIdentifier(activationScreen));
    QCOMPARE(resolution.globalTopLeft, expectedGlobalTopLeft);
    QCOMPARE(resolution.topLeftInScreen,
             expectedGlobalTopLeft - activationScreen->geometry().topLeft());
}

void TestScreenCanvasPlacement::testVirtualDesktopGeometryMatchesScreenUnion()
{
    const QList<QScreen*> screens = QGuiApplication::screens();

    QRect expectedGeometry;
    for (QScreen* screen : screens) {
        expectedGeometry = expectedGeometry.isValid()
            ? expectedGeometry.united(screen->geometry())
            : screen->geometry();
    }

    QCOMPARE(ScreenCanvasSession::virtualDesktopGeometry(screens), expectedGeometry);
}

void TestScreenCanvasPlacement::testDesktopCoordinateRoundTrip()
{
    QScreen* activationScreen = primaryOrFirstScreen();
    QVERIFY(activationScreen);

    const QRect desktopGeometry = ScreenCanvasSession::virtualDesktopGeometry(QGuiApplication::screens());
    const QPoint globalPoint = activationScreen->geometry().topLeft() + QPoint(40, 50);
    const QPoint desktopPoint = ScreenCanvasSession::globalToDesktop(globalPoint, desktopGeometry);
    const QPoint localPoint = ScreenCanvasSession::desktopToLocal(
        desktopPoint,
        activationScreen->geometry(),
        desktopGeometry);

    QCOMPARE(localPoint, QPoint(40, 50));
    QCOMPARE(desktopPoint + desktopGeometry.topLeft(), globalPoint);
}

void TestScreenCanvasPlacement::testSurfaceInitializationUsesScreenGeometry()
{
    QScreen* activationScreen = primaryOrFirstScreen();
    QVERIFY(activationScreen);

    const QRect desktopGeometry = ScreenCanvasSession::virtualDesktopGeometry(QGuiApplication::screens());

    ScreenCanvas canvas;
    canvas.initializeForScreenSurface(activationScreen, desktopGeometry);

    QCOMPARE(canvas.geometry(), activationScreen->geometry());
    QCOMPARE(canvas.size(), activationScreen->geometry().size());
    QCOMPARE(canvas.annotationOffset(),
             activationScreen->geometry().topLeft() - desktopGeometry.topLeft());
}

void TestScreenCanvasPlacement::testToolbarDragClampSnapsToNearestVisibleScreen()
{
    const QList<QRect> bounds = {
        QRect(0, 0, 1920, 1080),
        QRect(1920, 200, 1600, 900)
    };

    const QPoint clamped = SnapTray::QmlFloatingToolbar::clampTopLeftToBounds(
        QPoint(2100, 40),
        QSize(220, 72),
        bounds);

    QCOMPARE(clamped, QPoint(2100, 200));
}

void TestScreenCanvasPlacement::testSelectionToolbarPlacementPrefersBelowRightWhenClear()
{
    const QRect selectionRect(100, 80, 160, 120);
    const QSize toolbarSize(220, 32);
    const QRect viewportRect(0, 0, 640, 480);

    const QPoint topLeft = SnapTray::QmlFloatingToolbar::resolveTopLeftForSelection(
        selectionRect,
        toolbarSize,
        viewportRect,
        SnapTray::QmlFloatingToolbar::HorizontalAlignment::RightEdge);

    QCOMPARE(topLeft, QPoint(selectionRect.right() + 1 - toolbarSize.width(),
                             selectionRect.bottom() + 8));
}

void TestScreenCanvasPlacement::testSelectionToolbarPlacementUsesAboveRightWhenBottomSpaceTight()
{
    const QRect selectionRect(180, 360, 160, 100);
    const QSize toolbarSize(220, 32);
    const QRect viewportRect(0, 0, 640, 480);

    const QPoint topLeft = SnapTray::QmlFloatingToolbar::resolveTopLeftForSelection(
        selectionRect,
        toolbarSize,
        viewportRect,
        SnapTray::QmlFloatingToolbar::HorizontalAlignment::RightEdge);

    QCOMPARE(topLeft, QPoint(selectionRect.right() + 1 - toolbarSize.width(),
                              selectionRect.top() - toolbarSize.height() - 8));
}

void TestScreenCanvasPlacement::testSelectionToolbarPlacementPrefersOutsideBelowWhenOnlyAboveBlocked()
{
    const QRect selectionRect(180, 30, 220, 100);
    const QSize toolbarSize(200, 32);
    const QRect viewportRect(0, 0, 640, 480);

    const QPoint topLeft = SnapTray::QmlFloatingToolbar::resolveTopLeftForSelection(
        selectionRect,
        toolbarSize,
        viewportRect,
        SnapTray::QmlFloatingToolbar::HorizontalAlignment::RightEdge);

    QCOMPARE(topLeft, outsideBelowRightTopLeft(selectionRect, toolbarSize));
}

void TestScreenCanvasPlacement::testSelectionToolbarPlacementUsesWidgetTopForAboveFallback()
{
    const QRect selectionRect(180, 120, 220, 190);
    const QRect widgetRect(180, 72, 120, 28);
    const QSize toolbarSize(220, 32);
    const QRect viewportRect(0, 0, 640, 320);

    const QPoint topLeft = SnapTray::QmlFloatingToolbar::resolveTopLeftForSelection(
        selectionRect,
        toolbarSize,
        viewportRect,
        SnapTray::QmlFloatingToolbar::HorizontalAlignment::RightEdge,
        widgetRect);

    QCOMPARE(topLeft, QPoint(selectionRect.right() + 1 - toolbarSize.width(),
                             widgetRect.top() - toolbarSize.height() - 8));
}

void TestScreenCanvasPlacement::testSelectionToolbarPlacementUsesInsideBottomRightWhenOutsideCandidatesBlocked()
{
    const QRect selectionRect(180, 8, 220, 304);
    const QRect avoidRect(180, 16, 120, 24);
    const QSize toolbarSize(200, 32);
    const QRect viewportRect(0, 0, 640, 320);

    const QPoint topLeft = SnapTray::QmlFloatingToolbar::resolveTopLeftForSelection(
        selectionRect,
        toolbarSize,
        viewportRect,
        SnapTray::QmlFloatingToolbar::HorizontalAlignment::RightEdge,
        avoidRect);
    const QRect toolbarRect(topLeft, toolbarSize);

    QCOMPARE(topLeft, insideBottomRightTopLeft(selectionRect, toolbarSize));
    QVERIFY(insideSafeRect(selectionRect).contains(toolbarRect));
    QCOMPARE(selectionEdgeOverlapArea(toolbarRect, selectionRect), 0);
    QCOMPARE(overlapArea(toolbarRect, avoidRect), 0);
}

void TestScreenCanvasPlacement::testSelectionToolbarPlacementUsesInsideTopRightWhenInsideBottomBlocked()
{
    const QRect selectionRect(180, 8, 220, 464);
    const QRect avoidRect(286, 430, 106, 26);
    const QSize toolbarSize(200, 32);
    const QRect viewportRect(0, 0, 640, 480);

    const QPoint topLeft = SnapTray::QmlFloatingToolbar::resolveTopLeftForSelection(
        selectionRect,
        toolbarSize,
        viewportRect,
        SnapTray::QmlFloatingToolbar::HorizontalAlignment::RightEdge,
        avoidRect);
    const QRect toolbarRect(topLeft, toolbarSize);

    QCOMPARE(topLeft, insideTopRightTopLeft(selectionRect, toolbarSize));
    QVERIFY(insideSafeRect(selectionRect).contains(toolbarRect));
    QCOMPARE(selectionEdgeOverlapArea(toolbarRect, selectionRect), 0);
    QCOMPARE(overlapArea(toolbarRect, avoidRect), 0);
}

void TestScreenCanvasPlacement::testSelectionToolbarPlacementAvoidsSelectionOverlapInBottomRightCorner()
{
    // Keep the selection pinned to the bottom-right while leaving enough
    // headroom for the "above selection" placement this test is asserting.
    const QRect selectionRect(390, 60, 220, 390);
    const QSize toolbarSize(220, 32);
    const QRect viewportRect(0, 0, 640, 480);
    const QRect forbiddenRect = selectionRect.adjusted(-8, -8, 8, 8);

    const QPoint topLeft = SnapTray::QmlFloatingToolbar::resolveTopLeftForSelection(
        selectionRect,
        toolbarSize,
        viewportRect,
        SnapTray::QmlFloatingToolbar::HorizontalAlignment::RightEdge);
    const QRect toolbarRect(topLeft, toolbarSize);

    QVERIFY(insetViewportRect(viewportRect, 10).contains(toolbarRect));
    QCOMPARE(overlapArea(toolbarRect, forbiddenRect), 0);
    QCOMPARE(toolbarRect.bottom(), selectionRect.top() - 8 - 1);
}

void TestScreenCanvasPlacement::testSelectionToolbarPlacementPrefersInsideSafeCandidateOverLegacyTopClamp()
{
    const QRect selectionRect(30, 30, 240, 160);
    const QSize toolbarSize(220, 72);
    const QRect viewportRect(0, 0, 300, 220);
    const QRect forbiddenRect = selectionRect.adjusted(-8, -8, 8, 8);

    const QPoint resolvedTopLeft = SnapTray::QmlFloatingToolbar::resolveTopLeftForSelection(
        selectionRect,
        toolbarSize,
        viewportRect,
        SnapTray::QmlFloatingToolbar::HorizontalAlignment::RightEdge);
    const QRect resolvedRect(resolvedTopLeft, toolbarSize);

    // Compare against the legacy "inside at bottom" fallback that used to be
    // chosen once above/below no longer fit cleanly.
    const QRect legacyRect(
        QPoint(qBound(10,
                      selectionRect.right() + 1 - toolbarSize.width(),
                      viewportRect.width() - toolbarSize.width() - 10),
               selectionRect.bottom() - toolbarSize.height() - 10),
        toolbarSize);

    QCOMPARE(resolvedTopLeft, insideBottomRightTopLeft(selectionRect, toolbarSize));
    QVERIFY(insideSafeRect(selectionRect).contains(resolvedRect));
    QCOMPARE(selectionEdgeOverlapArea(resolvedRect, selectionRect), 0);
    QVERIFY(overlapArea(resolvedRect, forbiddenRect) <= overlapArea(legacyRect, forbiddenRect));
}

QTEST_MAIN(TestScreenCanvasPlacement)
#include "tst_Placement.moc"
