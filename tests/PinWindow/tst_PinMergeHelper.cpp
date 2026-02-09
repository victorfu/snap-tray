#include <QtTest/QtTest>
#include <memory>
#include <vector>

#include "PinWindow.h"
#include "pinwindow/PinMergeHelper.h"

class TestPinMergeHelper : public QObject
{
    Q_OBJECT

private:
    static QPixmap createTestPixmap(int logicalWidth,
                                    int logicalHeight,
                                    const QColor& color = Qt::blue,
                                    qreal dpr = 1.0)
    {
        const QSize physicalSize(qMax(1, qRound(logicalWidth * dpr)),
                                 qMax(1, qRound(logicalHeight * dpr)));

        QPixmap pixmap(physicalSize);
        pixmap.setDevicePixelRatio(dpr);
        pixmap.fill(color);
        return pixmap;
    }

    static QSize logicalSize(const QPixmap& pixmap)
    {
        const qreal dpr = pixmap.devicePixelRatio() > 0.0 ? pixmap.devicePixelRatio() : 1.0;
        return QSize(qRound(static_cast<qreal>(pixmap.width()) / dpr),
                     qRound(static_cast<qreal>(pixmap.height()) / dpr));
    }

private slots:
    void testMergeTwoSameSize()
    {
        PinWindow first(createTestPixmap(100, 100, Qt::red), QPoint(0, 0));
        PinWindow second(createTestPixmap(100, 100, Qt::green), QPoint(120, 0));

        const PinMergeResult result = PinMergeHelper::merge(QList<PinWindow*>{&first, &second});

        QVERIFY(result.success);
        QCOMPARE(result.mergedWindows.size(), 2);
        QCOMPARE(logicalSize(result.composedPixmap), QSize(208, 100));
        QCOMPARE(result.regions.size(), 2);
        QCOMPARE(result.regions[0].rect, QRect(0, 0, 100, 100));
        QCOMPARE(result.regions[1].rect, QRect(108, 0, 100, 100));
    }

    void testMergeDifferentHeights()
    {
        PinWindow first(createTestPixmap(100, 50, Qt::yellow), QPoint(0, 0));
        PinWindow second(createTestPixmap(100, 100, Qt::cyan), QPoint(120, 0));

        const PinMergeResult result = PinMergeHelper::merge(QList<PinWindow*>{&first, &second});

        QVERIFY(result.success);
        QCOMPARE(logicalSize(result.composedPixmap), QSize(208, 100));
        QCOMPARE(result.regions[0].rect, QRect(0, 0, 100, 50));
        QCOMPARE(result.regions[1].rect, QRect(108, 0, 100, 100));
    }

    void testMergeCustomGap()
    {
        PinWindow first(createTestPixmap(80, 100, Qt::magenta), QPoint(0, 0));
        PinWindow second(createTestPixmap(120, 100, Qt::gray), QPoint(120, 0));

        const PinMergeResult resultNoGap = PinMergeHelper::merge(QList<PinWindow*>{&first, &second}, 0);
        QVERIFY(resultNoGap.success);
        QCOMPARE(logicalSize(resultNoGap.composedPixmap), QSize(200, 100));

        const PinMergeResult resultGap16 = PinMergeHelper::merge(QList<PinWindow*>{&first, &second}, 16);
        QVERIFY(resultGap16.success);
        QCOMPARE(logicalSize(resultGap16.composedPixmap), QSize(216, 100));
    }

    void testMergeDifferentDpr()
    {
        PinWindow first(createTestPixmap(100, 100, Qt::red, 1.0), QPoint(0, 0));
        PinWindow second(createTestPixmap(100, 100, Qt::green, 2.0), QPoint(120, 0));

        const PinMergeResult result = PinMergeHelper::merge(QList<PinWindow*>{&first, &second});

        QVERIFY(result.success);
        QCOMPARE(result.composedPixmap.devicePixelRatio(), 2.0);
        QCOMPARE(logicalSize(result.composedPixmap), QSize(208, 100));
        QCOMPARE(result.regions.size(), 2);
        QCOMPARE(result.regions[0].image.devicePixelRatio(), 2.0);
        QCOMPARE(result.regions[1].image.devicePixelRatio(), 2.0);
    }

    void testMergeSingleWindowFails()
    {
        PinWindow only(createTestPixmap(100, 100), QPoint(0, 0));
        const PinMergeResult result = PinMergeHelper::merge(QList<PinWindow*>{&only});
        QVERIFY(!result.success);
        QVERIFY(!result.errorMessage.isEmpty());
    }

    void testMergeEmptyListFails()
    {
        const PinMergeResult result = PinMergeHelper::merge({});
        QVERIFY(!result.success);
        QVERIFY(!result.errorMessage.isEmpty());
    }

    void testMergeTooManyWindowsFails()
    {
        std::vector<std::unique_ptr<PinWindow>> ownedWindows;
        QList<PinWindow*> windows;
        const int windowCount = LayoutModeConstants::kMaxRegionCount + 1;
        ownedWindows.reserve(windowCount);
        windows.reserve(windowCount);

        for (int i = 0; i < windowCount; ++i) {
            auto window = std::make_unique<PinWindow>(createTestPixmap(64, 64),
                                                      QPoint(i * 10, 0),
                                                      nullptr,
                                                      false);
            windows.append(window.get());
            ownedWindows.push_back(std::move(window));
        }

        const PinMergeResult result = PinMergeHelper::merge(windows);
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QString::number(LayoutModeConstants::kMaxRegionCount)));
    }

    void testMergeExceedingMaxCanvasSizeFails()
    {
        const int maxSize = LayoutModeConstants::kMaxCanvasSize;
        PinWindow first(createTestPixmap(maxSize / 2 + 100, 200), QPoint(0, 0), nullptr, false);
        PinWindow second(createTestPixmap(maxSize / 2 + 100, 200), QPoint(120, 0), nullptr, false);

        const PinMergeResult result = PinMergeHelper::merge(QList<PinWindow*>{&first, &second}, 8);
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QString::number(maxSize)));

        PinWindow tallFirst(createTestPixmap(200, maxSize + 1), QPoint(0, 0), nullptr, false);
        PinWindow tallSecond(createTestPixmap(200, 50), QPoint(220, 0), nullptr, false);
        const PinMergeResult tallResult = PinMergeHelper::merge(QList<PinWindow*>{&tallFirst, &tallSecond}, 8);
        QVERIFY(!tallResult.success);
        QVERIFY(tallResult.errorMessage.contains(QString::number(maxSize)));
    }

    void testFailedMergeDoesNotExitRegionLayoutMode()
    {
        const int maxSize = LayoutModeConstants::kMaxCanvasSize;
        PinWindow first(createTestPixmap(maxSize / 2 + 100, 200), QPoint(0, 0), nullptr, false);
        PinWindow second(createTestPixmap(200, maxSize + 1), QPoint(120, 0), nullptr, false);

        QVector<LayoutRegion> regions;
        LayoutRegion region1;
        region1.rect = QRect(0, 0, 80, 200);
        region1.originalRect = region1.rect;
        region1.image = QImage(80, 200, QImage::Format_ARGB32);
        region1.image.fill(Qt::red);
        region1.color = Qt::red;
        region1.index = 1;
        regions.append(region1);

        LayoutRegion region2;
        region2.rect = QRect(80, 0, 80, 200);
        region2.originalRect = region2.rect;
        region2.image = QImage(80, 200, QImage::Format_ARGB32);
        region2.image.fill(Qt::green);
        region2.color = Qt::green;
        region2.index = 2;
        regions.append(region2);

        first.setMultiRegionData(regions);
        first.enterRegionLayoutMode();
        QVERIFY(first.isRegionLayoutMode());

        const PinMergeResult result = PinMergeHelper::merge(QList<PinWindow*>{&first, &second}, 8);
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains(QString::number(maxSize)));
        QVERIFY(first.isRegionLayoutMode());
    }

    void testExportPixmapForMergeUsesActiveLayoutSnapshot()
    {
        PinWindow window(createTestPixmap(100, 100, Qt::blue), QPoint(0, 0), nullptr, false);

        QVector<LayoutRegion> regions;
        LayoutRegion leftRegion;
        leftRegion.rect = QRect(0, 0, 50, 100);
        leftRegion.originalRect = leftRegion.rect;
        leftRegion.image = QImage(50, 100, QImage::Format_ARGB32);
        leftRegion.image.fill(Qt::red);
        leftRegion.color = Qt::red;
        leftRegion.index = 1;
        regions.append(leftRegion);

        LayoutRegion rightRegion;
        rightRegion.rect = QRect(50, 0, 50, 100);
        rightRegion.originalRect = rightRegion.rect;
        rightRegion.image = QImage(50, 100, QImage::Format_ARGB32);
        rightRegion.image.fill(Qt::green);
        rightRegion.color = Qt::green;
        rightRegion.index = 2;
        regions.append(rightRegion);

        window.setMultiRegionData(regions);
        window.enterRegionLayoutMode();
        QVERIFY(window.isRegionLayoutMode());

        const QPixmap mergedSource = window.exportPixmapForMerge();
        QVERIFY(!mergedSource.isNull());
        QCOMPARE(logicalSize(mergedSource), QSize(100, 100));

        const QImage image = mergedSource.toImage();
        QVERIFY(image.pixelColor(10, 50) == QColor(Qt::red));
        QVERIFY(image.pixelColor(90, 50) == QColor(Qt::green));
    }

    void testExportPixmapForMergeIsStableAcrossRepeatedCallsWithTransform()
    {
        PinWindow window(createTestPixmap(120, 80, Qt::yellow), QPoint(0, 0), nullptr, false);
        window.rotateRight();
        window.flipHorizontal();

        const QPixmap firstExport = window.exportPixmapForMerge();
        const QPixmap secondExport = window.exportPixmapForMerge();

        QVERIFY(!firstExport.isNull());
        QVERIFY(!secondExport.isNull());
        QCOMPARE(logicalSize(firstExport), QSize(80, 120));
        QCOMPARE(logicalSize(secondExport), QSize(80, 120));
        QCOMPARE(firstExport.devicePixelRatio(), secondExport.devicePixelRatio());
        QCOMPARE(firstExport.toImage().convertToFormat(QImage::Format_ARGB32),
                 secondExport.toImage().convertToFormat(QImage::Format_ARGB32));
    }
};

QTEST_MAIN(TestPinMergeHelper)
#include "tst_PinMergeHelper.moc"
