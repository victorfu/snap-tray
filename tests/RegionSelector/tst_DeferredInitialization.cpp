#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QImage>
#include <QScreen>
#include <QApplication>
#include <QtQml/qqmlextensionplugin.h>

#include "RegionSelector.h"
#include "region/RegionInputHandler.h"
#include "region/MagnifierOverlay.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

namespace {

QRect clampRectToScreen(const QRect& screenRect, const QPoint& center, const QSize& size)
{
    if (!screenRect.isValid() || size.isEmpty()) {
        return {};
    }

    const int maxX = screenRect.right() - size.width() + 1;
    const int maxY = screenRect.bottom() - size.height() + 1;
    const int x = qBound(screenRect.left(), center.x() - size.width() / 2, maxX);
    const int y = qBound(screenRect.top(), center.y() - size.height() / 2, maxY);
    return QRect(QPoint(x, y), size);
}

QPixmap makePreCapture(QScreen* screen)
{
    const QRect screenRect = screen->geometry();
    const qreal dpr = screen->devicePixelRatio() > 0.0 ? screen->devicePixelRatio() : 1.0;
    const QSize physicalSize(
        qMax(1, qCeil(screenRect.width() * dpr)),
        qMax(1, qCeil(screenRect.height() * dpr)));

    QImage image(physicalSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(32, 48, 64));

    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

DetectedElement makeElement(const QRect& bounds)
{
    DetectedElement element;
    element.bounds = bounds;
    element.windowTitle = QStringLiteral("DeferredInit");
    element.ownerApp = QStringLiteral("SnapTrayTests");
    element.windowLayer = 0;
    element.windowId = 99;
    element.elementType = ElementType::Window;
    element.ownerPid = 4242;
    return element;
}

class HeadlessRegionSelector final : public RegionSelector
{
public:
    using RegionSelector::RegionSelector;
};

} // namespace

class tst_RegionSelectorDeferredInitialization : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();
    void testInitializeKeepsFullMagnifierCacheCold();
    void testShowRevealsImmediatelyWhileDetectorRefreshes();
    void testWindowListReadyRefreshesHighlightAfterReveal();

private:
    static QScreen* targetScreen();
    static void configurePendingDetector(WindowDetector& detector, QScreen* screen);
    static void configureReadyDetector(WindowDetector& detector,
                                       QScreen* screen,
                                       const QRect& globalBounds);
    static void configureSelector(HeadlessRegionSelector& selector, WindowDetector* detector);
};

QScreen* tst_RegionSelectorDeferredInitialization::targetScreen()
{
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    return screen;
}

void tst_RegionSelectorDeferredInitialization::configurePendingDetector(WindowDetector& detector,
                                                                        QScreen* screen)
{
    detector.setEnabled(true);
    detector.setScreen(screen);
    detector.m_refreshComplete = false;
    detector.m_cacheReady = false;
    detector.m_cacheScreen = screen;
    detector.m_windowCache.clear();
}

void tst_RegionSelectorDeferredInitialization::configureReadyDetector(WindowDetector& detector,
                                                                      QScreen* screen,
                                                                      const QRect& globalBounds)
{
    detector.setEnabled(true);
    detector.setScreen(screen);
    detector.m_windowCache = {makeElement(globalBounds)};
    detector.m_cacheScreen = screen;
    detector.m_cacheReady = true;
    detector.m_refreshComplete = true;
}

void tst_RegionSelectorDeferredInitialization::configureSelector(HeadlessRegionSelector& selector,
                                                                 WindowDetector* detector)
{
    selector.setAttribute(Qt::WA_DeleteOnClose, false);
    selector.setAttribute(Qt::WA_DontShowOnScreen, true);
    selector.setWindowDetector(detector);
    selector.m_magnifierOverlay.reset();
}

void tst_RegionSelectorDeferredInitialization::testInitializeKeepsFullMagnifierCacheCold()
{
    QScreen* screen = targetScreen();
    if (!screen) {
        QSKIP("No screen available for deferred initialization test.");
    }

    WindowDetector detector;
    configurePendingDetector(detector, screen);

    HeadlessRegionSelector selector;
    configureSelector(selector, &detector);
    selector.initializeForScreen(screen, makePreCapture(screen));

    QVERIFY(selector.m_magnifierPanel != nullptr);
    QVERIFY2(selector.m_magnifierPanel->m_backgroundImageCache.isNull(),
             "Initial entry should not eagerly convert the full background pixmap to QImage.");
}

void tst_RegionSelectorDeferredInitialization::cleanup()
{
    QApplication::closeAllWindows();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

void tst_RegionSelectorDeferredInitialization::testShowRevealsImmediatelyWhileDetectorRefreshes()
{
    QScreen* screen = targetScreen();
    if (!screen) {
        QSKIP("No screen available for deferred initialization test.");
    }

    WindowDetector detector;
    configurePendingDetector(detector, screen);

    HeadlessRegionSelector selector;
    configureSelector(selector, &detector);
    selector.initializeForScreen(screen, makePreCapture(screen));
    selector.setGeometry(screen->geometry());
    selector.show();
    QCoreApplication::processEvents();

    QCOMPARE(selector.m_initialRevealState, RegionSelector::InitialRevealState::Revealed);
    QCOMPARE(selector.windowOpacity(), 1.0);
    QVERIFY(selector.m_inputHandler != nullptr);
    QVERIFY2(!selector.m_inputHandler->lastMagnifierRect().isNull(),
             "RegionInputHandler should be seeded during initial reveal to avoid a full repaint on first move.");
    QVERIFY2(selector.m_magnifierPanel->m_backgroundImageCache.isNull(),
             "Initial reveal should keep the full background cache cold.");
}

void tst_RegionSelectorDeferredInitialization::testWindowListReadyRefreshesHighlightAfterReveal()
{
    QScreen* screen = targetScreen();
    if (!screen) {
        QSKIP("No screen available for deferred initialization test.");
    }

    WindowDetector detector;
    configurePendingDetector(detector, screen);

    HeadlessRegionSelector selector;
    configureSelector(selector, &detector);
    selector.initializeForScreen(screen, makePreCapture(screen));
    selector.setGeometry(screen->geometry());
    selector.show();
    QCoreApplication::processEvents();

    QVERIFY(!selector.m_inputState.hasDetectedWindow);
    QVERIFY(selector.m_inputState.highlightedWindowRect.isNull());

    const QRect globalBounds = clampRectToScreen(screen->geometry(), QCursor::pos(), QSize(260, 180));
    configureReadyDetector(detector, screen, globalBounds);
    emit detector.windowListReady();
    QCoreApplication::processEvents();

    QVERIFY(selector.m_inputState.hasDetectedWindow);
    QCOMPARE(selector.m_inputState.highlightedWindowRect,
             selector.globalToLocal(globalBounds).intersected(selector.rect()));
}

QTEST_MAIN(tst_RegionSelectorDeferredInitialization)
#include "tst_DeferredInitialization.moc"
