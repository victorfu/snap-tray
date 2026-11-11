#include <QtTest>
#include <QPaintEvent>
#include <QScreen>
#include <QtQml/qqmlextensionplugin.h>
#include "RegionSelectorTestAccess.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

class PaintProbe : public QObject
{
public:
    QVector<QRegion> regions;
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::Paint) {
            regions.append(static_cast<QPaintEvent*>(event)->region());
        }
        return false;
    }
};

class TestOCRBusyRepaint : public QObject
{
    Q_OBJECT
private slots:
    void animationRepaintsOnlySpinner();
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
    RegionSelectorTestAccess::setOcrBusy(selector, true);
    QTest::qWait(250); // Let first reveal/state-change full repaints finish.
    QWidget* surface = &selector;
    auto* chrome = RegionSelectorTestAccess::captureChromeWindow(selector);
    if (chrome && chrome->isVisible()) {
        surface = chrome;
    }
    PaintProbe probe;
    surface->installEventFilter(&probe);
    QTRY_VERIFY_WITH_TIMEOUT(probe.regions.size() >= 5, 2000);
    surface->removeEventFilter(&probe);
    RegionSelectorTestAccess::setOcrBusy(selector, false);
    LoadingSpinnerRenderer spinner;
    const QRegion expected(spinner.bounds(selection.center()));
    qint64 maximumArea = 0;
    for (const auto& region : probe.regions) {
        qint64 area = 0;
        for (const auto& rect : region) {
            area += qint64(rect.width()) * rect.height();
        }
        maximumArea = qMax(maximumArea, area);
    }
    qInfo() << "Spinner repaint: max pixels/frame" << maximumArea
            << "full surface" << qint64(surface->width()) * surface->height();
    for (const auto& region : probe.regions) {
        QVERIFY2(region.subtracted(expected).isEmpty(),
                 qPrintable(QStringLiteral("Unexpected repaint %1x%2")
                     .arg(region.boundingRect().width()).arg(region.boundingRect().height())));
    }
    selector.hide();
}

QTEST_MAIN(TestOCRBusyRepaint)
#include "tst_OCRBusyRepaint.moc"
