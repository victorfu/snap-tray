#include <QtTest>
#include <QPaintEvent>
#include <QScreen>
#include <QBackingStore>
#include <QElapsedTimer>
#include <QtQml/qqmlextensionplugin.h>
#include "RegionSelectorTestAccess.h"
#include "PinWindow.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

// Capture after QWidget::paintEvent, while Qt is still between beginPaint and
// endPaint. Reading paintDevice() after processing events can instead observe a
// recycled Cocoa back buffer. Never use grab(), which would repaint stale pixels.
class PaintProbe : public QObject
{
    Q_OBJECT
public:
    PaintProbe(QWidget* surface, const QRect& bounds)
        : QObject(surface), sampleBounds(bounds)
    {
        sincePaint.start();
    }

    void recordPaint(QWidget* surface, const QRegion& region)
    {
        regions.append(region);
        sincePaint.restart();
        auto* store = surface->backingStore();
        auto* image = store ? dynamic_cast<QImage*>(store->paintDevice()) : nullptr;
        if (!image) {
            pixels = {};
            return;
        }
        const qreal dpr = image->devicePixelRatio();
        pixels = image->copy(QRect(qFloor(sampleBounds.x() * dpr),
                                   qFloor(sampleBounds.y() * dpr),
                                   qCeil(sampleBounds.width() * dpr),
                                   qCeil(sampleBounds.height() * dpr)));
    }

    QVector<QRegion> regions;
    QElapsedTimer sincePaint;
    QImage pixels;
    const QRect sampleBounds;
};

class PaintTestApplication : public QApplication
{
public:
    using QApplication::QApplication;

    bool notify(QObject* receiver, QEvent* event) override
    {
        if (event->type() != QEvent::Paint) {
            return QApplication::notify(receiver, event);
        }
        auto* surface = qobject_cast<QWidget*>(receiver);
        const QRegion region = static_cast<QPaintEvent*>(event)->region();
        const bool handled = QApplication::notify(receiver, event);
        if (surface) {
            for (auto* probe : surface->findChildren<PaintProbe*>(
                     QString(), Qt::FindDirectChildrenOnly)) {
                probe->recordPaint(surface, region);
            }
        }
        return handled;
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
    QPixmap background(screen->size() * screen->devicePixelRatio());
    background.setDevicePixelRatio(screen->devicePixelRatio());
    background.fill(Qt::white);
    // Keep the preset inside the logical screen even at high scale factors.
    const QRect selection(QPoint(50, 50), screen->size() / 2);
    selector.initializeWithRegion(screen, selection.translated(screen->geometry().topLeft()), background);
    RegionSelectorTestAccess::markInitialRevealRevealed(selector);
    RegionSelectorTestAccess::showForRevealTests(selector);
    QCoreApplication::processEvents();
    QWidget* surface = &selector;
    auto* chrome = RegionSelectorTestAccess::captureChromeWindow(selector);
    if (chrome && chrome->isVisible()) {
        surface = chrome;
    }
    auto* spinner = RegionSelectorTestAccess::loadingSpinner(selector);
    const QRect bounds = spinner->bounds(selection.center());
    PaintProbe probe(surface, bounds);
    surface->update();
    QVERIFY(waitForQuiet(probe));
    const QImage idle = probe.pixels;
    QVERIFY(!idle.isNull());
    RegionSelectorTestAccess::setOcrBusy(selector, true);
    stopAnimationClock(spinner);
    QVERIFY(waitForQuiet(probe));
    QVERIFY(probe.pixels != idle);
    verifyAnimationFrames(surface, spinner, probe, bounds);
    probe.regions.clear();
    RegionSelectorTestAccess::setOcrBusy(selector, false);
    QVERIFY(waitForQuiet(probe));
    QRegion erased;
    for (const auto& region : probe.regions) {
        erased += region;
    }
    QVERIFY(QRegion(bounds).subtracted(erased).isEmpty());
    QCOMPARE(probe.pixels, idle);
    selector.hide();
}

void TestOCRBusyRepaint::pinAnimationRepaintsAndErases()
{
    QPixmap image(500, 350);
    image.fill(Qt::white);
    PinWindow window(image, QPoint(50, 50), nullptr, false, false);
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    auto* spinner = window.m_loadingSpinner;
    const QRect bounds = spinner->bounds(window.rect().center());
    PaintProbe probe(&window, bounds);
    window.show();
    QVERIFY(waitForQuiet(probe));
    const QImage idle = probe.pixels;
    QVERIFY(!idle.isNull());
    window.m_ocrInProgress = true;
    window.updateLoadingSpinnerState();
    stopAnimationClock(spinner);
    QVERIFY(waitForQuiet(probe));
    QVERIFY(probe.pixels != idle);
    verifyAnimationFrames(&window, spinner, probe, bounds);
    probe.regions.clear();
    window.m_ocrInProgress = false;
    window.updateLoadingSpinnerState();
    QVERIFY(waitForQuiet(probe));
    QVERIFY(!probe.regions.isEmpty());
    QCOMPARE(probe.pixels, idle);
    window.hide();
}

int main(int argc, char** argv)
{
    PaintTestApplication app(argc, argv);
    TestOCRBusyRepaint test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_OCRBusyRepaint.moc"
