#include <QtTest/QtTest>
#include <QPainter>
#include <QPixmap>

#include "region/CaptureShortcutHintsOverlay.h"

class tst_CaptureShortcutHintsOverlay : public QObject
{
    Q_OBJECT

private slots:
    void testRowCount();
    void testLayoutMetrics();
    void testPanelRectWithinViewport();
    void testDrawNoCrash();
};

void tst_CaptureShortcutHintsOverlay::testRowCount()
{
    CaptureShortcutHintsOverlay overlay;
    QCOMPARE(overlay.rowCount(), 7);
}

void tst_CaptureShortcutHintsOverlay::testLayoutMetrics()
{
    CaptureShortcutHintsOverlay overlay;
    const auto metrics = overlay.layoutMetrics();

    QVERIFY(metrics.keyColumnWidth > 0);
    QVERIFY(metrics.textColumnWidth > 0);
    QVERIFY(metrics.rowHeight > 0);
    QVERIFY(metrics.panelWidth > 0);
    QVERIFY(metrics.panelHeight > 0);
}

void tst_CaptureShortcutHintsOverlay::testPanelRectWithinViewport()
{
    CaptureShortcutHintsOverlay overlay;

    const QList<QSize> viewports{
        QSize(420, 280),
        QSize(900, 600),
        QSize(2560, 1440)
    };

    for (const QSize& viewport : viewports) {
        const QRect panelRect = overlay.panelRectForViewport(viewport);
        QVERIFY2(panelRect.isValid(), "Panel rect should be valid");
        QVERIFY2(panelRect.left() >= 0, "Panel rect should stay inside viewport");
        QVERIFY2(panelRect.top() >= 0, "Panel rect should stay inside viewport");
        QVERIFY2(panelRect.right() < viewport.width(), "Panel rect right edge should stay inside viewport");
        QVERIFY2(panelRect.bottom() < viewport.height(), "Panel rect bottom edge should stay inside viewport");
    }
}

void tst_CaptureShortcutHintsOverlay::testDrawNoCrash()
{
    CaptureShortcutHintsOverlay overlay;
    QPixmap canvas(1280, 720);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    overlay.draw(painter, canvas.size());
    QVERIFY(true);
}

QTEST_MAIN(tst_CaptureShortcutHintsOverlay)
#include "tst_CaptureShortcutHintsOverlay.moc"
