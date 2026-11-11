#include <QtTest>
#include <QPaintEvent>
#include <QScreen>
#include <QBackingStore>
#include <QElapsedTimer>
#include <QtQml/qqmlextensionplugin.h>
#include "RegionSelectorTestAccess.h"
#include "PinWindow.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

class PaintProbe : public QObject
{
public:
    QVector<QRegion> regions;
    QElapsedTimer sincePaint;
    PaintProbe() { sincePaint.start(); }
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::Paint) {
            regions.append(static_cast<QPaintEvent*>(event)->region());
            sincePaint.restart();
        }
        return false;
    }
};

namespace {
bool waitForQuiet(PaintProbe& probe)
{
    QElapsedTimer timeout;
    timeout.start();
    do {
        QTest::qWait(10);
    } while (probe.sincePaint.elapsed() < 100 && timeout.elapsed() < 5000);
    return probe.sincePaint.elapsed() >= 100;
}

void stopAnimationClock(LoadingSpinnerRenderer* spinner)
{
    // Deliver animation ticks explicitly so settling cannot race a 16 ms timer.
    for (auto* timer : spinner->findChildren<QTimer*>()) {
        timer->stop();
    }
}

QImage backingPixels(QWidget* surface, const QRect& rect)
{
    // Read the existing raster backing store; QWidget::grab() would repaint and
    // hide stale pixels, defeating the erase regression check.
    auto* store = surface->backingStore();
    auto* image = store ? dynamic_cast<QImage*>(store->paintDevice()) : nullptr;
    if (!image) {
        return {};
    }
    const qreal dpr = image->devicePixelRatio();
    return image->copy(QRect(qFloor(rect.x() * dpr), qFloor(rect.y() * dpr),
                            qCeil(rect.width() * dpr), qCeil(rect.height() * dpr)));
}

void verifyAnimationFrames(QWidget* surface, LoadingSpinnerRenderer* spinner,
                           PaintProbe& probe, const QRect& bounds)
{
    for (int frame = 0; frame < 5; ++frame) {
        probe.regions.clear();
        spinner->needsRepaint();
        // Dispatch only posted paint requests, not unrelated native input/expose
        // events. Initial reveal work was drained before entering this section.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
        QVERIFY(!probe.regions.isEmpty());
        for (const auto& region : probe.regions) {
            QVERIFY2(region.subtracted(QRegion(bounds)).isEmpty(),
                     qPrintable(QStringLiteral("Unexpected repaint %1x%2")
                         .arg(region.boundingRect().width()).arg(region.boundingRect().height())));
        }
    }
    qInfo() << "Spinner repaint bounds" << bounds.size()
            << "surface" << surface->size();
}
}

class TestOCRBusyRepaint : public QObject
{
    Q_OBJECT
private slots:
    void animationRepaintsOnlySpinner();
    void pinAnimationRepaintsAndErases();
};

void TestOCRBusyRepaint::animationRepaintsOnlySpinner()
{
    auto* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    RegionSelector selector;
    QPixmap background(screen->size());
    background.fill(Qt::white);
    const QRect selection(100, 100, 400, 300);
    selector.initializeWithRegion(screen, selection, background);
    RegionSelectorTestAccess::markInitialRevealRevealed(selector);
    RegionSelectorTestAccess::showForRevealTests(selector);
    QCoreApplication::processEvents();
    QWidget* surface = &selector;
    auto* chrome = RegionSelectorTestAccess::captureChromeWindow(selector);
    if (chrome && chrome->isVisible()) {
        surface = chrome;
    }
    PaintProbe probe;
    surface->installEventFilter(&probe);
    QVERIFY(waitForQuiet(probe));
    auto* spinner = RegionSelectorTestAccess::loadingSpinner(selector);
    const QRect bounds = spinner->bounds(selection.center());
    const QImage idle = backingPixels(surface, bounds);
    QVERIFY(!idle.isNull());
    RegionSelectorTestAccess::setOcrBusy(selector, true);
    stopAnimationClock(spinner);
    QVERIFY(waitForQuiet(probe));
    QVERIFY(backingPixels(surface, bounds) != idle);
    verifyAnimationFrames(surface, spinner, probe, bounds);
    probe.regions.clear();
    RegionSelectorTestAccess::setOcrBusy(selector, false);
    QVERIFY(waitForQuiet(probe));
    QRegion erased;
    for (const auto& region : probe.regions) {
        erased += region;
    }
    QVERIFY(QRegion(bounds).subtracted(erased).isEmpty());
    QCOMPARE(backingPixels(surface, bounds), idle);
    surface->removeEventFilter(&probe);
    selector.hide();
}

void TestOCRBusyRepaint::pinAnimationRepaintsAndErases()
{
    QPixmap image(500, 350);
    image.fill(Qt::white);
    PinWindow window(image, QPoint(50, 50), nullptr, false, false);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    PaintProbe probe;
    window.installEventFilter(&probe);
    window.show();
    QVERIFY(waitForQuiet(probe));
    auto* spinner = window.m_loadingSpinner;
    const QRect bounds = spinner->bounds(window.rect().center());
    const QImage idle = backingPixels(&window, bounds);
    QVERIFY(!idle.isNull());
    window.m_ocrInProgress = true;
    window.updateLoadingSpinnerState();
    stopAnimationClock(spinner);
    QVERIFY(waitForQuiet(probe));
    QVERIFY(backingPixels(&window, bounds) != idle);
    verifyAnimationFrames(&window, spinner, probe, bounds);
    probe.regions.clear();
    window.m_ocrInProgress = false;
    window.updateLoadingSpinnerState();
    QVERIFY(waitForQuiet(probe));
    QVERIFY(!probe.regions.isEmpty());
    QCOMPARE(backingPixels(&window, bounds), idle);
    window.removeEventFilter(&probe);
    window.hide();
}

QTEST_MAIN(TestOCRBusyRepaint)
#include "tst_OCRBusyRepaint.moc"
