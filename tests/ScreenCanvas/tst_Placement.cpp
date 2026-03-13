#include <QtTest/QtTest>

#include <QGuiApplication>
#include <QScreen>

#include "ScreenCanvas.h"
#include "ScreenCanvasSession.h"
#include "qml/QmlFloatingToolbar.h"
#include "settings/ScreenCanvasSettingsManager.h"

namespace {
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

QTEST_MAIN(TestScreenCanvasPlacement)
#include "tst_Placement.moc"
