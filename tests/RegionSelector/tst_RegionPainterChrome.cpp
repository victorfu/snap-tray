#include <QtTest/QtTest>

#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QWidget>

#include "region/RegionPainter.h"
#include "region/SelectionStateManager.h"

namespace {

const QRect kHostRect(0, 0, 420, 320);
const QRect kSelectionRect(90, 80, 180, 120);

QImage paintImage(RegionPainter& painter,
                  SelectionStateManager& selectionManager,
                  QWidget& hostWidget,
                  bool detectedWindow)
{
    selectionManager.clearSelection();
    if (!detectedWindow) {
        selectionManager.setSelectionRect(kSelectionRect);
    }

    painter.setParentWidget(&hostWidget);
    painter.setSelectionManager(&selectionManager);
    painter.setCornerRadius(0);
    painter.setDevicePixelRatio(1.0);
    painter.setHighlightedWindowRect(detectedWindow ? kSelectionRect : QRect());

    QPixmap background(hostWidget.size());
    background.fill(QColor(245, 247, 250));

    QImage canvas(hostWidget.size(), QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);

    QPainter qp(&canvas);
    qp.setRenderHint(QPainter::Antialiasing);
    painter.paint(qp, background);
    qp.end();

    return canvas;
}

QImage paintFractionalDprImage(RegionPainter& painter,
                               SelectionStateManager& selectionManager,
                               QWidget& hostWidget,
                               const QPixmap& background,
                               const QRegion& dirtyRegion = QRegion())
{
    painter.setParentWidget(&hostWidget);
    painter.setSelectionManager(&selectionManager);
    painter.setCornerRadius(0);
    painter.setDevicePixelRatio(background.devicePixelRatio());
    painter.setHighlightedWindowRect(QRect());

    QImage canvas(hostWidget.size(), QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);

    QPainter qp(&canvas);
    qp.setRenderHint(QPainter::Antialiasing);
    if (!dirtyRegion.isEmpty()) {
        qp.setClipRegion(dirtyRegion);
    }
    painter.paint(qp, background, dirtyRegion);
    qp.end();

    return canvas;
}

} // namespace

class tst_RegionPainterChrome : public QObject
{
    Q_OBJECT

private slots:
    void testDetectedWindowChromeMatchesSelectionChrome();
    void testWindowHighlightVisualRectIncludesHandlesAndPanel();
    void testFractionalDprPartialBackgroundRepaintMatchesFullPaint();
};

void tst_RegionPainterChrome::testDetectedWindowChromeMatchesSelectionChrome()
{
    QWidget hostWidget;
    hostWidget.resize(kHostRect.size());

    SelectionStateManager selectionManager;
    selectionManager.setBounds(kHostRect);

    RegionPainter painter;

    const QImage detectedImage =
        paintImage(painter, selectionManager, hostWidget, true);
    const QImage selectionImage =
        paintImage(painter, selectionManager, hostWidget, false);

    QCOMPARE(detectedImage, selectionImage);
}

void tst_RegionPainterChrome::testWindowHighlightVisualRectIncludesHandlesAndPanel()
{
    QWidget hostWidget;
    hostWidget.resize(kHostRect.size());

    SelectionStateManager selectionManager;
    selectionManager.setBounds(kHostRect);
    selectionManager.setSelectionRect(kSelectionRect);

    RegionPainter painter;
    painter.setParentWidget(&hostWidget);
    painter.setSelectionManager(&selectionManager);
    painter.setCornerRadius(0);
    painter.setDevicePixelRatio(1.0);

    QPixmap background(hostWidget.size());
    background.fill(Qt::white);

    QImage canvas(hostWidget.size(), QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter qp(&canvas);
    qp.setRenderHint(QPainter::Antialiasing);
    painter.paint(qp, background);
    qp.end();

    const QRect visualRect = painter.getWindowHighlightVisualRect(kSelectionRect);
    const QRect handleBounds = kSelectionRect.adjusted(-4, -4, 4, 4);

    QVERIFY(visualRect.contains(handleBounds));
    QVERIFY(visualRect.contains(painter.lastDimensionInfoRect()));
}

void tst_RegionPainterChrome::testFractionalDprPartialBackgroundRepaintMatchesFullPaint()
{
    QWidget hostWidget;
    hostWidget.resize(QSize(200, 120));

    SelectionStateManager selectionManager;
    selectionManager.setBounds(QRect(QPoint(0, 0), hostWidget.size()));

    QImage backgroundImage(QSize(300, 180), QImage::Format_ARGB32_Premultiplied);
    backgroundImage.fill(Qt::black);
    for (int y = 0; y < backgroundImage.height(); ++y) {
        backgroundImage.setPixelColor(151, y, Qt::red);
        backgroundImage.setPixelColor(152, y, Qt::green);
        backgroundImage.setPixelColor(153, y, Qt::blue);
    }

    QPixmap background = QPixmap::fromImage(backgroundImage);
    background.setDevicePixelRatio(1.5);

    RegionPainter painter;
    const QImage fullCanvas = paintFractionalDprImage(
        painter, selectionManager, hostWidget, background);

    const QRegion dirtyRegion(QRect(101, 20, 1, 10));
    const QImage partialCanvas = paintFractionalDprImage(
        painter, selectionManager, hostWidget, background, dirtyRegion);

    for (int y = dirtyRegion.boundingRect().top(); y <= dirtyRegion.boundingRect().bottom(); ++y) {
        for (int x = dirtyRegion.boundingRect().left(); x <= dirtyRegion.boundingRect().right(); ++x) {
            QVERIFY(dirtyRegion.contains(QPoint(x, y)));
            QCOMPARE(partialCanvas.pixelColor(x, y), fullCanvas.pixelColor(x, y));
        }
    }
}

QTEST_MAIN(tst_RegionPainterChrome)
#include "tst_RegionPainterChrome.moc"
