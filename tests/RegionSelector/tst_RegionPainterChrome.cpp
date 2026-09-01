#include <QtTest/QtTest>

#include <QColorSpace>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QWidget>

#include "region/RegionPainter.h"
#include "region/SelectionDimensionLabel.h"
#include "region/SelectionDirtyRegionPlanner.h"
#include "region/SelectionStateManager.h"
#include "annotations/AnnotationLayer.h"
#include "tools/ToolManager.h"

namespace {

const QRect kHostRect(0, 0, 420, 320);
const QRect kSelectionRect(90, 80, 180, 120);
constexpr int kDetachedAnnotationViewportMargin = 64;

QImage paintImage(RegionPainter& painter,
                  SelectionStateManager& selectionManager,
                  QWidget& hostWidget,
                  bool detectedWindow,
                  QRect* dimensionRect = nullptr)
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

    if (dimensionRect) {
        *dimensionRect = painter.lastDimensionInfoRect();
    }

    return canvas;
}

bool imagesEqualOutsideRect(const QImage& lhs, const QImage& rhs, const QRect& ignoredRect)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (int y = 0; y < lhs.height(); ++y) {
        for (int x = 0; x < lhs.width(); ++x) {
            if (ignoredRect.contains(x, y)) {
                continue;
            }
            if (lhs.pixelColor(x, y) != rhs.pixelColor(x, y)) {
                return false;
            }
        }
    }

    return true;
}

bool regionHasRedStrokePixel(const QImage& image, const QRect& probe)
{
    const QRect boundedProbe = probe.intersected(image.rect());
    for (int y = boundedProbe.top(); y <= boundedProbe.bottom(); ++y) {
        for (int x = boundedProbe.left(); x <= boundedProbe.right(); ++x) {
            const QColor color = image.pixelColor(x, y);
            if (color.red() > color.green() + 40 &&
                color.red() > color.blue() + 40 &&
                color.alpha() > 0) {
                return true;
            }
        }
    }
    return false;
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
    void testDirectDimmingMatchesCached_data();
    void testDirectDimmingMatchesCached();
    void testDetectedWindowChromeMatchesSelectionChrome();
    void testSelectionDimensionLabelUsesPlatformUnits();
    void testWindowHighlightVisualRectIncludesHandlesAndPanel();
    void testCompactSelectionDimensionLabelStaysOutsideSelection();
    void testTopEdgeShortSelectionUsesCompactDimensionLabel();
    void testFractionalDprPartialBackgroundRepaintMatchesFullPaint();
    void testFractionalDprSelectionTransitionNeedsFullRepaint();
    void testPencilPreviewExtendsBeyondDetachedAnnotationViewport();
};

void tst_RegionPainterChrome::testDirectDimmingMatchesCached_data()
{
    QTest::addColumn<QString>("selectionState");
    QTest::addColumn<qreal>("devicePixelRatio");
    QTest::addColumn<bool>("partialRepaint");
    QTest::addColumn<bool>("displayP3");

    const QStringList states = {
        QStringLiteral("none"),
        QStringLiteral("highlight"),
        QStringLiteral("selection"),
    };
    const QList<qreal> devicePixelRatios = {1.0, 2.0};

    for (const QString& state : states) {
        for (qreal dpr : devicePixelRatios) {
            for (bool partial : {false, true}) {
                for (bool displayP3 : {false, true}) {
                    const QByteArray rowName = QStringLiteral("%1-dpr%2-%3-%4")
                        .arg(state)
                        .arg(dpr)
                        .arg(partial ? QStringLiteral("dirty") : QStringLiteral("full"))
                        .arg(displayP3 ? QStringLiteral("p3") : QStringLiteral("srgb"))
                        .toLatin1();
                    QTest::newRow(rowName.constData())
                        << state << dpr << partial << displayP3;
                }
            }
        }
    }
}

void tst_RegionPainterChrome::testDirectDimmingMatchesCached()
{
    QFETCH(QString, selectionState);
    QFETCH(qreal, devicePixelRatio);
    QFETCH(bool, partialRepaint);
    QFETCH(bool, displayP3);

    const QSize logicalSize(120, 90);
    const QRect logicalBounds(QPoint(), logicalSize);
    const QSize physicalSize(
        qRound(logicalSize.width() * devicePixelRatio),
        qRound(logicalSize.height() * devicePixelRatio));

    QWidget hostWidget;
    hostWidget.resize(logicalSize);

    QImage backgroundImage(physicalSize, QImage::Format_ARGB32_Premultiplied);
    backgroundImage.setColorSpace(
        displayP3 ? QColorSpace(QColorSpace::DisplayP3)
                  : QColorSpace(QColorSpace::SRgb));
    for (int y = 0; y < backgroundImage.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(backgroundImage.scanLine(y));
        for (int x = 0; x < backgroundImage.width(); ++x) {
            row[x] = qRgb(
                (x * 17 + y * 3) % 256,
                (x * 5 + y * 13) % 256,
                (x * 11 + y * 7) % 256);
        }
    }
    QPixmap background = QPixmap::fromImage(backgroundImage);
    background.setDevicePixelRatio(devicePixelRatio);

    const QRect focusRect(24, 18, 58, 42);
    const QRegion dirtyRegion = partialRepaint
        ? QRegion(QRect(2, 4, 39, 31)).united(QRegion(QRect(66, 40, 48, 43)))
        : QRegion(logicalBounds);

    auto render = [&](RegionPainter::DimmingComposition composition,
                      bool* cacheMaterialized) {
        SelectionStateManager selectionManager;
        selectionManager.setBounds(logicalBounds);

        RegionPainter painter;
        painter.setParentWidget(&hostWidget);
        painter.setSelectionManager(&selectionManager);
        painter.setCornerRadius(0);
        painter.setDevicePixelRatio(devicePixelRatio);
        painter.m_dimmingComposition = composition;

        if (selectionState == QStringLiteral("selection")) {
            selectionManager.setSelectionRect(focusRect);
        } else if (selectionState == QStringLiteral("highlight")) {
            painter.setHighlightedWindowRect(focusRect);
        }

        QImage canvas(physicalSize, QImage::Format_ARGB32_Premultiplied);
        canvas.setDevicePixelRatio(devicePixelRatio);
        canvas.setColorSpace(backgroundImage.colorSpace());
        canvas.fill(QColor(31, 47, 63, 255));

        QPainter canvasPainter(&canvas);
        canvasPainter.setRenderHint(QPainter::Antialiasing);
        if (partialRepaint) {
            canvasPainter.setClipRegion(dirtyRegion);
        }
        painter.paint(canvasPainter, background, dirtyRegion);
        canvasPainter.end();
        if (cacheMaterialized) {
            *cacheMaterialized = !painter.m_dimmedBackgroundCache.isNull();
        }
        return canvas;
    };

    bool cachedMaterialized = false;
    bool directMaterialized = true;
    bool platformDefaultMaterialized = true;
    const QImage cached = render(
        RegionPainter::DimmingComposition::Cached, &cachedMaterialized);
    const QImage direct = render(
        RegionPainter::DimmingComposition::Direct, &directMaterialized);
    render(RegionPainter::DimmingComposition::PlatformDefault,
           &platformDefaultMaterialized);

    QCOMPARE(direct, cached);
    QVERIFY(cachedMaterialized);
    QVERIFY(!directMaterialized);
#ifdef Q_OS_MACOS
    QVERIFY(!platformDefaultMaterialized);
#else
    QVERIFY(platformDefaultMaterialized);
#endif
}

void tst_RegionPainterChrome::testDetectedWindowChromeMatchesSelectionChrome()
{
    QWidget hostWidget;
    hostWidget.resize(kHostRect.size());

    SelectionStateManager selectionManager;
    selectionManager.setBounds(kHostRect);

    RegionPainter painter;
    QRect detectedDimensionRect;
    QRect selectionDimensionRect;

    const QImage detectedImage =
        paintImage(painter, selectionManager, hostWidget, true, &detectedDimensionRect);
    const QImage selectionImage =
        paintImage(painter, selectionManager, hostWidget, false, &selectionDimensionRect);

    const QRect ignoredRect = detectedDimensionRect
        .united(selectionDimensionRect)
        .adjusted(-8, -8, 8, 8)
        .intersected(QRect(QPoint(0, 0), hostWidget.size()));
    QVERIFY(imagesEqualOutsideRect(
        detectedImage,
        selectionImage,
        ignoredRect));
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
    QCOMPARE(SelectionDimensionLabel::sampleLabel(), QStringLiteral("9999 x 9999 pt"));
    QCOMPARE(SelectionDimensionLabel::widgetLabel(QRect(0, 0, 2560, 1410), 2.0),
             QStringLiteral("2560×1410 pt"));
    QCOMPARE(SelectionDimensionLabel::widgetSampleLabel(), QStringLiteral("9999×9999 pt"));
#else
    QCOMPARE(metrics.size, QSize(5, 5));
    QCOMPARE(metrics.unit, QStringLiteral("px"));
    QCOMPARE(SelectionDimensionLabel::displayMetrics(physicalSize, 2.0).size, physicalSize);
    QCOMPARE(SelectionDimensionLabel::displayMetrics(physicalSize, 2.0).unit, QStringLiteral("px"));
    QCOMPARE(SelectionDimensionLabel::label(logicalRect, 1.5), QStringLiteral("5 x 5 px"));
    QCOMPARE(SelectionDimensionLabel::label(physicalSize, 2.0), QStringLiteral("5120 x 2820 px"));
    QCOMPARE(SelectionDimensionLabel::sampleLabel(), QStringLiteral("9999 x 9999 px"));
    QCOMPARE(SelectionDimensionLabel::widgetLabel(physicalSize, 2.0), QStringLiteral("5120×2820 px"));
    QCOMPARE(SelectionDimensionLabel::widgetSampleLabel(), QStringLiteral("9999×9999 px"));
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

void tst_RegionPainterChrome::testCompactSelectionDimensionLabelStaysOutsideSelection()
{
    QWidget hostWidget;
    hostWidget.resize(kHostRect.size());

    SelectionStateManager selectionManager;
    selectionManager.setBounds(kHostRect);
    const QRect compactSelectionRect(180, 160, 120, 30);
    selectionManager.setSelectionRect(compactSelectionRect);

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

    const QRect dimensionRect = painter.lastDimensionInfoRect();
    QVERIFY(dimensionRect.isValid());
    QCOMPARE(dimensionRect.top(), compactSelectionRect.top());
    QVERIFY(dimensionRect.right() < compactSelectionRect.left());
    QVERIFY(compactSelectionRect.left() - dimensionRect.right() >= 8);
    QVERIFY(!compactSelectionRect.contains(dimensionRect));
}

void tst_RegionPainterChrome::testTopEdgeShortSelectionUsesCompactDimensionLabel()
{
    QWidget hostWidget;
    hostWidget.resize(kHostRect.size());

    SelectionStateManager selectionManager;
    selectionManager.setBounds(kHostRect);
    const QRect topEdgeShortSelectionRect(180, 8, 220, 39);
    selectionManager.setSelectionRect(topEdgeShortSelectionRect);

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

    const QRect dimensionRect = painter.lastDimensionInfoRect();
    QVERIFY(dimensionRect.isValid());
    QVERIFY(dimensionRect.right() < topEdgeShortSelectionRect.left());
    QVERIFY(!topEdgeShortSelectionRect.intersects(dimensionRect));
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

void tst_RegionPainterChrome::testPencilPreviewExtendsBeyondDetachedAnnotationViewport()
{
    QWidget hostWidget;
    hostWidget.resize(kHostRect.size());

    const QRect selectionRect(20, 80, 60, 100);
    const QRect detachedViewport =
        selectionRect.adjusted(-kDetachedAnnotationViewportMargin,
                               -kDetachedAnnotationViewportMargin,
                               kDetachedAnnotationViewportMargin,
                               kDetachedAnnotationViewportMargin)
            .intersected(hostWidget.rect());

    SelectionStateManager selectionManager;
    selectionManager.setBounds(kHostRect);
    selectionManager.setSelectionRect(selectionRect);

    AnnotationLayer annotationLayer;
    ToolManager toolManager;
    toolManager.registerDefaultHandlers();
    toolManager.setAnnotationLayer(&annotationLayer);
    toolManager.setColor(Qt::red);
    toolManager.setWidth(5);
    toolManager.setLineStyle(LineStyle::Solid);
    toolManager.setCurrentTool(ToolId::Pencil);

    const QPointF start(50.0, 130.0);
    const QPointF outside(400.0, 130.0);
    toolManager.handleMousePress(start);
    for (qreal x = 100.0; x <= outside.x(); x += 50.0) {
        toolManager.handleMouseMove(QPointF(x, outside.y()));
    }
    QVERIFY(toolManager.isDrawing());
    QVERIFY(toolManager.currentHandler() != nullptr);
    const QRect previewBounds = toolManager.currentHandler()->previewBounds();
    QVERIFY(previewBounds.left() > detachedViewport.right());

    RegionPainter painter;
    painter.setParentWidget(&hostWidget);
    painter.setSelectionManager(&selectionManager);
    painter.setAnnotationLayer(&annotationLayer);
    painter.setToolManager(&toolManager);
    painter.setCurrentTool(static_cast<int>(ToolId::Pencil));
    painter.setDevicePixelRatio(1.0);
    painter.setAnnotationViewport(detachedViewport);

    QImage liveCanvas(hostWidget.size(), QImage::Format_ARGB32_Premultiplied);
    liveCanvas.fill(Qt::transparent);
    const QRect previewDirtyRect = previewBounds.adjusted(
        -SelectionDirtyRegionPlanner::kAnnotationRepaintMargin,
        -SelectionDirtyRegionPlanner::kAnnotationRepaintMargin,
        SelectionDirtyRegionPlanner::kAnnotationRepaintMargin,
        SelectionDirtyRegionPlanner::kAnnotationRepaintMargin).intersected(hostWidget.rect());
    {
        QPainter livePainter(&liveCanvas);
        livePainter.setRenderHint(QPainter::Antialiasing);
        livePainter.setClipRect(previewDirtyRect);
        painter.paint(livePainter, QPixmap(), QRegion(previewDirtyRect));
    }

    const QRect outsideProbe(
        qMax(previewBounds.left(), detachedViewport.right() + 10),
        124,
        qMin(50, previewBounds.right() - qMax(
            previewBounds.left(), detachedViewport.right() + 10) + 1),
        13);
    QVERIFY(outsideProbe.isValid() && !outsideProbe.isEmpty());
    QVERIFY(!detachedViewport.intersects(outsideProbe));
    QVERIFY(regionHasRedStrokePixel(liveCanvas, outsideProbe));

    // A full repaint must also redraw the older prefix between the static
    // viewport and the current dirty tail.
    const QRect prefixProbe(
        detachedViewport.right() + 4,
        124,
        previewBounds.left() - detachedViewport.right() - 8,
        13);
    QVERIFY(prefixProbe.isValid() && !prefixProbe.isEmpty());

    QImage fullLiveCanvas(hostWidget.size(), QImage::Format_ARGB32_Premultiplied);
    fullLiveCanvas.fill(Qt::transparent);
    {
        QPainter fullLivePainter(&fullLiveCanvas);
        fullLivePainter.setRenderHint(QPainter::Antialiasing);
        painter.paint(fullLivePainter, QPixmap(), QRegion(hostWidget.rect()));
    }
    QVERIFY(regionHasRedStrokePixel(fullLiveCanvas, prefixProbe));

    toolManager.handleMouseRelease(outside);
    QCOMPARE(annotationLayer.itemCount(), static_cast<size_t>(1));

    QImage completedCanvas(hostWidget.size(), QImage::Format_ARGB32_Premultiplied);
    completedCanvas.fill(Qt::transparent);
    {
        QPainter completedPainter(&completedCanvas);
        completedPainter.setRenderHint(QPainter::Antialiasing);
        painter.paint(completedPainter, QPixmap(), QRegion(hostWidget.rect()));
    }
    QVERIFY(regionHasRedStrokePixel(completedCanvas, outsideProbe));
    QVERIFY(regionHasRedStrokePixel(completedCanvas, prefixProbe));
}

QTEST_MAIN(tst_RegionPainterChrome)
#include "tst_RegionPainterChrome.moc"
