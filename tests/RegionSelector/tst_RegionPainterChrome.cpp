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

} // namespace

class tst_RegionPainterChrome : public QObject
{
    Q_OBJECT

private slots:
    void testDetectedWindowChromeMatchesSelectionChrome();
    void testWindowHighlightVisualRectIncludesHandlesAndPanel();
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

QTEST_MAIN(tst_RegionPainterChrome)
#include "tst_RegionPainterChrome.moc"
