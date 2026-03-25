#include <QtTest/QtTest>

#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QWidget>

#include "region/RegionPainter.h"
#include "region/SelectionDimensionLabel.h"
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

QImage applyFractionalTransitionPaint(RegionPainter& painter,
                                      SelectionStateManager& selectionManager,
                                      QWidget& hostWidget,
                                      const QPixmap& background,
                                      const QRect& previousHighlightRect,
                                      const QRect& currentSelectionRect,
                                      const QRegion& dirtyRegion)
{
    painter.setParentWidget(&hostWidget);
    painter.setSelectionManager(&selectionManager);
    painter.setCornerRadius(0);
    painter.setDevicePixelRatio(background.devicePixelRatio());

    selectionManager.clearSelection();
    painter.setHighlightedWindowRect(previousHighlightRect);

    QImage canvas(hostWidget.size(), QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    {
        QPainter previousPainter(&canvas);
        previousPainter.setRenderHint(QPainter::Antialiasing);
        painter.paint(previousPainter, background);
    }

    selectionManager.setSelectionRect(currentSelectionRect);
    painter.setHighlightedWindowRect(QRect());

    QPainter transitionPainter(&canvas);
    transitionPainter.setRenderHint(QPainter::Antialiasing);
    if (!dirtyRegion.isEmpty()) {
        transitionPainter.setClipRegion(dirtyRegion);
    }
    painter.paint(transitionPainter, background, dirtyRegion);
    transitionPainter.end();

    return canvas;
}

} // namespace

class tst_RegionPainterChrome : public QObject
{
    Q_OBJECT

private slots:
    void testDetectedWindowChromeMatchesSelectionChrome();
    void testSelectionDimensionLabelUsesPlatformUnits();
    void testWindowHighlightVisualRectIncludesHandlesAndPanel();
    void testFractionalDprPartialBackgroundRepaintMatchesFullPaint();
    void testFractionalDprSelectionTransitionNeedsFullRepaint();
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

void tst_RegionPainterChrome::testSelectionDimensionLabelUsesPlatformUnits()
{
    const QRect logicalRect(1, 1, 3, 3);
    const QSize physicalSize(5120, 2820);
    const auto metrics = SelectionDimensionLabel::displayMetrics(logicalRect, 1.5);

#ifdef Q_OS_MACOS
    QCOMPARE(metrics.size, QSize(3, 3));
    QCOMPARE(metrics.unit, QStringLiteral("pt"));
    QCOMPARE(SelectionDimensionLabel::displayMetrics(physicalSize, 2.0).size, QSize(2560, 1410));
    QCOMPARE(SelectionDimensionLabel::displayMetrics(physicalSize, 2.0).unit, QStringLiteral("pt"));
    QCOMPARE(SelectionDimensionLabel::label(QRect(0, 0, 2560, 1410), 2.0),
             QStringLiteral("2560 x 1410 pt"));
    QCOMPARE(SelectionDimensionLabel::label(physicalSize, 2.0), QStringLiteral("2560 x 1410 pt"));
    QCOMPARE(SelectionDimensionLabel::sampleLabel(), QStringLiteral("99999 x 99999 pt"));
#else
    QCOMPARE(metrics.size, QSize(5, 5));
    QCOMPARE(metrics.unit, QStringLiteral("px"));
    QCOMPARE(SelectionDimensionLabel::displayMetrics(physicalSize, 2.0).size, physicalSize);
    QCOMPARE(SelectionDimensionLabel::displayMetrics(physicalSize, 2.0).unit, QStringLiteral("px"));
    QCOMPARE(SelectionDimensionLabel::label(logicalRect, 1.5), QStringLiteral("5 x 5 px"));
    QCOMPARE(SelectionDimensionLabel::label(physicalSize, 2.0), QStringLiteral("5120 x 2820 px"));
    QCOMPARE(SelectionDimensionLabel::sampleLabel(), QStringLiteral("99999 x 99999 px"));
#endif
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

void tst_RegionPainterChrome::testFractionalDprSelectionTransitionNeedsFullRepaint()
{
    QWidget hostWidget;
    hostWidget.resize(QSize(200, 120));

    SelectionStateManager selectionManager;
    selectionManager.setBounds(QRect(QPoint(0, 0), hostWidget.size()));

    QImage backgroundImage(QSize(300, 180), QImage::Format_ARGB32_Premultiplied);
    backgroundImage.fill(Qt::black);
    for (int y = 0; y < backgroundImage.height(); ++y) {
        backgroundImage.setPixelColor(0, y, QColor(64, 96, 160));
        backgroundImage.setPixelColor(1, y, QColor(128, 160, 224));
        backgroundImage.setPixelColor(2, y, QColor(32, 64, 128));
    }

    QPixmap background = QPixmap::fromImage(backgroundImage);
    background.setDevicePixelRatio(1.5);

    RegionPainter painter;
    const QRect previousHighlightRect(0, 0, 160, 80);
    const QRect currentSelectionRect(60, 32, 90, 54);

    selectionManager.setSelectionRect(currentSelectionRect);
    const QImage fullSelectionCanvas = paintFractionalDprImage(
        painter, selectionManager, hostWidget, background);

    const QRegion partialDirtyRegion(QRect(56, 28, 98, 62));
    const QImage partialTransitionCanvas = applyFractionalTransitionPaint(
        painter,
        selectionManager,
        hostWidget,
        background,
        previousHighlightRect,
        currentSelectionRect,
        partialDirtyRegion);
    QVERIFY(partialTransitionCanvas != fullSelectionCanvas);

    const QImage fullTransitionCanvas = applyFractionalTransitionPaint(
        painter,
        selectionManager,
        hostWidget,
        background,
        previousHighlightRect,
        currentSelectionRect,
        QRegion(hostWidget.rect()));
    QCOMPARE(fullTransitionCanvas, fullSelectionCanvas);
}

QTEST_MAIN(tst_RegionPainterChrome)
#include "tst_RegionPainterChrome.moc"
