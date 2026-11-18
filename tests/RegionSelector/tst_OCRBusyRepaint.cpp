#include <QtTest>
#include <QPaintEvent>
#include <QPainter>
#include <QScreen>
#include <QBackingStore>
#include <QElapsedTimer>
#include <QtQml/qqmlextensionplugin.h>
#include "RegionSelectorTestAccess.h"
#include "PinWindow.h"
#include "utils/CoordinateHelper.h"

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
            m_validRegion = {};
            return;
        }
        recordPixels(*image, region);
    }

    void recordPixels(const QImage& image, const QRegion& region)
    {
        const qreal dpr = image.devicePixelRatio();
        const QRect sampleRect = CoordinateHelper::toPhysicalCoveringRect(sampleBounds, dpr);
        if (!image.rect().contains(sampleRect)) {
            pixels = {};
            m_validRegion = {};
            return;
        }
        if (m_sampledPixels.size() != sampleRect.size()
                || m_sampledPixels.format() != image.format() || m_sampleDpr != dpr) {
            m_sampledPixels = QImage(sampleRect.size(), image.format());
            m_sampledPixels.fill(Qt::transparent);
            m_sampleDpr = dpr;
            m_validRegion = {};
            pixels = {};
        }
        const QRegion painted = region.intersected(QRegion(sampleBounds));
        if (painted.isEmpty()) return;
        // Cocoa finalizes untouched back-buffer areas only at flush. Keep our
        // own sample and copy just the pixels this paint event made valid.
        {
            QPainter painter(&m_sampledPixels); // physical pixels, DPR remains 1
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            for (const QRect& rect : painted) {
                const QRect source = CoordinateHelper::toPhysicalCoveringRect(rect, dpr)
                    .intersected(sampleRect);
                painter.drawImage(source.translated(-sampleRect.topLeft()), image, source);
            }
        }
        m_validRegion += painted;
        if (QRegion(sampleBounds).subtracted(m_validRegion).isEmpty()) {
            pixels = m_sampledPixels;
            pixels.setDevicePixelRatio(dpr);
        }
    }

    QVector<QRegion> regions;
    QElapsedTimer sincePaint;
    QImage pixels;
    const QRect sampleBounds;

private:
    QImage m_sampledPixels;
    QRegion m_validRegion;
    qreal m_sampleDpr = 0;
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
    // A newly shown native window may not paint immediately (notably on Cocoa).
    // Silence before the first captured frame is not a settled backing store.
    const auto hasSettledFrame = [&probe] {
        return !probe.regions.isEmpty() && !probe.pixels.isNull()
            && probe.sincePaint.elapsed() >= 100;
    };
    QElapsedTimer timeout;
    timeout.start();
    do {
        QTest::qWait(10);
    } while (!hasSettledFrame() && timeout.elapsed() < 5000);
    return hasSettledFrame();
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
    void probeSamplesOnlyPaintedPixels_data();
    void probeSamplesOnlyPaintedPixels();
    void animationRepaintsOnlySpinner();
    void pinAnimationRepaintsAndErases();
};

void TestOCRBusyRepaint::probeSamplesOnlyPaintedPixels_data()
{
    QTest::addColumn<int>("dpr");
    QTest::newRow("normal") << 1;
    QTest::newRow("retina") << 2;
}

void TestOCRBusyRepaint::probeSamplesOnlyPaintedPixels()
{
    QFETCH(int, dpr);
    QWidget surface;
    const QRect bounds(3, 5, 10, 8);
    PaintProbe probe(&surface, bounds);
    QImage buffer(QSize(40, 40) * dpr, QImage::Format_ARGB32_Premultiplied);
    buffer.setDevicePixelRatio(dpr);
    buffer.fill(Qt::white);
    probe.recordPixels(buffer, QRegion(bounds));
    const QImage idle = probe.pixels;
    QCOMPARE(idle.size(), bounds.size() * dpr);

    // Cocoa may provide a different, stale back buffer for each paint.
    // Painting elsewhere must not replace a previously sampled spinner area.
    buffer.fill(Qt::magenta);
    probe.recordPixels(buffer, QRegion(QRect(25, 25, 2, 2)));
    QCOMPARE(probe.pixels, idle);

    const QRect left(bounds.topLeft(), QSize(bounds.width() / 2, bounds.height()));
    const QRect right(left.topRight() + QPoint(1, 0), left.size());
    const auto paintPart = [&](const QRect& part, QColor color) {
        buffer.fill(Qt::magenta);
        QPainter painter(&buffer);
        painter.fillRect(part, color);
        painter.end();
        probe.recordPixels(buffer, QRegion(part));
    };
    paintPart(left, Qt::green);
    QImage expected = idle;
    { QPainter painter(&expected); painter.fillRect(QRect(QPoint(), left.size()), Qt::green); }
    QCOMPARE(probe.pixels, expected);
    paintPart(right, Qt::blue);
    { QPainter painter(&expected); painter.fillRect(QRect(QPoint(left.width(), 0), right.size()), Qt::blue); }
    QCOMPARE(probe.pixels, expected);
    // Separate dirty regions can together erase the complete spinner.
    paintPart(left, Qt::white);
    QVERIFY(probe.pixels != idle);
    paintPart(right, Qt::white);
    QCOMPARE(probe.pixels, idle);
}

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
    // Exercise the real reveal: merely setting the state to Revealed leaves
    // initializeWithRegion's opacity at zero. Cocoa can then defer drawing the
    // invisible window, making a backing-store comparison nondeterministic.
    RegionSelectorTestAccess::showForRevealTests(selector);
    QTRY_VERIFY(RegionSelectorTestAccess::initialRevealStateIsRevealed(selector));
    QCOMPARE(selector.windowOpacity(), 1.0);
    QVERIFY(QTest::qWaitForWindowExposed(&selector));
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
    // A quiet old frame is not evidence that the requested state was painted.
    // Cocoa may deliver the new backing-store update after the first wait tick.
    probe.regions.clear();
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
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.update();
    QVERIFY(waitForQuiet(probe));
    const QImage idle = probe.pixels;
    QVERIFY(!idle.isNull());
    probe.regions.clear();
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
