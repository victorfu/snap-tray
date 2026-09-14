#include <QtTest/QtTest>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QPixmap>
#include <QPainter>
#include <QAction>
#include <QClipboard>

#include "PinWindow.h"
#include "PlatformFeatures.h"
#include "annotations/MosaicStroke.h"
#include "tools/ToolManager.h"

class TestPinWindowTransform : public QObject
{
    Q_OBJECT

private:
    QPixmap createTestPixmap(int width, int height, const QColor& color = Qt::red) {
        QPixmap pixmap(width, height);
        pixmap.fill(color);
        // Draw a marker in top-left corner to verify transformations
        QPainter painter(&pixmap);
        painter.fillRect(0, 0, 10, 10, Qt::blue);
        return pixmap;
    }

private slots:
    void testManualMosaicSamplesZoomedCanvas_data() {
        QTest::addColumn<qreal>("zoom");
        QTest::addColumn<qreal>("dpr");
        QTest::addColumn<bool>("transform");
        for (qreal zoom : {0.5, 2.0}) {
            for (qreal dpr : {1.0, 2.0}) {
                for (bool transform : {false, true}) {
                    QTest::newRow(qPrintable(QString("zoom%1-dpr%2-transform%3").arg(zoom).arg(dpr).arg(transform)))
                        << zoom << dpr << transform;
                }
            }
        }
    }

    void testManualMosaicSamplesZoomedCanvas() {
        QFETCH(qreal, zoom);
        QFETCH(qreal, dpr);
        QFETCH(bool, transform);
        QPixmap source(QSize(100, 80) * dpr);
        source.setDevicePixelRatio(dpr);
        source.fill(Qt::red);
        { QPainter painter(&source); painter.fillRect(50, 0, 50, 80, Qt::blue); }
        PinWindow window(source, QPoint(), nullptr, false, false);
        window.setZoomLevel(zoom);
        if (transform) { window.rotateRight(); window.flipHorizontal(); }
        window.initializeAnnotationComponents();
        window.m_toolManager->setCurrentTool(ToolId::Mosaic);
        window.m_toolManager->setWidth(2);
        const QPoint start(qRound(76 * zoom), qRound(48 * zoom));
        const QPoint end = start + QPoint(2, 0);
        window.m_toolManager->handleMousePress(window.mapToOriginalCoords(window.mapFromOriginalCoords(start)));
        window.m_toolManager->handleMouseRelease(window.mapToOriginalCoords(window.mapFromOriginalCoords(end)));
        QCOMPARE(window.m_annotationLayer->itemCount(), size_t(1));
        QImage actual(QSize(100, 80) * zoom * dpr, QImage::Format_ARGB32);
        actual.setDevicePixelRatio(dpr);
        actual.fill(Qt::transparent);
        { QPainter painter(&actual); window.m_annotationLayer->draw(painter); }
        QCOMPARE(actual.pixelColor(start * dpr), QColor(Qt::blue));

        // A subsequent zoom must also refresh strokes held by undo history.
        window.m_annotationLayer->undo();
        window.setZoomLevel(2.0);
        window.m_annotationLayer->redo();
        QCOMPARE(window.m_sharedSourcePixmap->deviceIndependentSize().toSize(), QSize(200, 160));
        QImage replayed(QSize(200, 160) * dpr, QImage::Format_ARGB32);
        replayed.setDevicePixelRatio(dpr);
        replayed.fill(Qt::transparent);
        { QPainter painter(&replayed); window.m_annotationLayer->draw(painter); }
        QCOMPARE(replayed.pixelColor(start * dpr), QColor(zoom < 1.0 ? Qt::red : Qt::blue));
    }

    void testMosaicCanvasFollowsZoomedCrop() {
        QPixmap source(100, 80);
        source.fill(Qt::red);
        { QPainter painter(&source); painter.fillRect(50, 0, 50, 80, Qt::blue); }
        PinWindow window(source, QPoint(), nullptr, false, false);
        window.setZoomLevel(2.0);
        window.initializeAnnotationComponents();
        window.applyCrop(QRect(100, 0, 100, 160));
        QCOMPARE(window.m_sharedSourcePixmap->size(), QSize(100, 160));
        QCOMPARE(window.m_sharedSourcePixmap->toImage().pixelColor(50, 80), QColor(Qt::blue));
        window.undoCrop();
        QCOMPARE(window.m_sharedSourcePixmap->size(), QSize(200, 160));
        QCOMPARE(window.m_sharedSourcePixmap->toImage().pixelColor(50, 80), QColor(Qt::red));
        window.redoCrop();
        QCOMPARE(window.m_sharedSourcePixmap->size(), QSize(100, 160));
        QCOMPARE(window.m_sharedSourcePixmap->toImage().pixelColor(50, 80), QColor(Qt::blue));
    }

    void testInfoCopyTracksCurrentTransform() {
        PinWindow window(createTestPixmap(200, 160), QPoint(), nullptr, false, false);
        window.createContextMenu();
        const auto verifyInfo = [&] {
            window.refreshInfoMenu();
            const QList<QPair<QAction*, QString>> rows{
                {window.m_sizeInfoAction, window.currentDisplaySizeText()},
                {window.m_zoomInfoAction, QString("%1%").arg(qRound(window.zoomLevel() * 100))},
                {window.m_rotationInfoAction, QString::fromUtf8("%1°").arg(window.m_rotationAngle)},
                {window.m_opacityInfoAction, QString("%1%").arg(qRound(window.opacity() * 100))},
                {window.m_flipHorizontalInfoAction, window.m_flipHorizontal ? window.tr("Yes") : window.tr("No")},
                {window.m_flipVerticalInfoAction, window.m_flipVertical ? window.tr("Yes") : window.tr("No")}
            };
            for (const auto& row : rows) {
                QVERIFY(row.first);
                QVERIFY(row.first->text().endsWith(": " + row.second));
                QGuiApplication::clipboard()->setText(QStringLiteral("stale"));
                row.first->trigger();
                QCOMPARE(QGuiApplication::clipboard()->text(), row.second);
            }
        };
        verifyInfo();
        window.setZoomLevel(1.75);
        window.rotateRight();
        window.setOpacity(0.6);
        window.flipHorizontal();
        window.flipVertical();
        verifyInfo();
        window.applyCrop(QRect(10, 10, 60, 40));
        verifyInfo();
        window.setZoomLevel(0.75);
        window.rotateLeft();
        window.setOpacity(0.9);
        window.flipHorizontal();
        window.flipVertical();
        verifyInfo();
    }

    void initTestCase() {
        if (QGuiApplication::screens().isEmpty()) {
            QSKIP("No screens available for PinWindow tests in this environment.");
        }
    }

    // =========================================================================
    // Initial State Tests
    // =========================================================================

    void testInitialZoomLevel() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QCOMPARE(window.zoomLevel(), 1.0);
    }

    void testInitialOpacity() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QCOMPARE(window.opacity(), 1.0);
    }

    // =========================================================================
    // Zoom Tests
    // =========================================================================

    void testSetZoomLevel() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        window.setZoomLevel(2.0);
        QCOMPARE(window.zoomLevel(), 2.0);

        window.setZoomLevel(0.5);
        QCOMPARE(window.zoomLevel(), 0.5);
    }

    void testZoomLevelClamping() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        // Test lower bound (0.1)
        window.setZoomLevel(0.05);
        QCOMPARE(window.zoomLevel(), 0.1);

        // Test upper bound (5.0)
        window.setZoomLevel(10.0);
        QCOMPARE(window.zoomLevel(), 5.0);
    }

    void testZoomLevelBoundaryValues() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        // Exactly at boundaries
        window.setZoomLevel(0.1);
        QCOMPARE(window.zoomLevel(), 0.1);

        window.setZoomLevel(5.0);
        QCOMPARE(window.zoomLevel(), 5.0);
    }

    // =========================================================================
    // Opacity Tests
    // =========================================================================

    void testSetOpacity() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        window.setOpacity(0.5);
        QCOMPARE(window.opacity(), 0.5);

        window.setOpacity(0.8);
        QCOMPARE(window.opacity(), 0.8);
    }

    void testOpacityClamping() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        // Test lower bound (0.1)
        window.setOpacity(0.0);
        QCOMPARE(window.opacity(), 0.1);

        // Test upper bound (1.0)
        window.setOpacity(1.5);
        QCOMPARE(window.opacity(), 1.0);
    }

    void testOpacityBoundaryValues() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        window.setOpacity(0.1);
        QCOMPARE(window.opacity(), 0.1);

        window.setOpacity(1.0);
        QCOMPARE(window.opacity(), 1.0);
    }

    // =========================================================================
    // Rotation Tests
    // =========================================================================

    void testRotateRightThenLeft() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QSize originalSize = window.size();

        window.rotateRight();
        window.rotateLeft();

        // Should return to original size
        QCOMPARE(window.size(), originalSize);
    }

    void testRotateNonSquarePixmap() {
        // Non-square pixmap: width != height
        QPixmap pixmap = createTestPixmap(200, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QSize originalSize = window.size();

        window.rotateRight();
        QSize rotatedSize = window.size();

        // Width and height should swap (accounting for shadow margin)
        // Original: 200 + margins x 100 + margins
        // Rotated: 100 + margins x 200 + margins
        QVERIFY(rotatedSize.width() != originalSize.width() ||
                rotatedSize.height() != originalSize.height());
    }

    // =========================================================================
    // Flip Tests
    // =========================================================================

    void testFlipHorizontal() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QSize originalSize = window.size();

        window.flipHorizontal();
        QCOMPARE(window.size(), originalSize);

        // Double flip = original
        window.flipHorizontal();
        QCOMPARE(window.size(), originalSize);
    }

    void testFlipVertical() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QSize originalSize = window.size();

        window.flipVertical();
        QCOMPARE(window.size(), originalSize);

        // Double flip = original
        window.flipVertical();
        QCOMPARE(window.size(), originalSize);
    }

    void testCombinedFlips() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QSize originalSize = window.size();

        window.flipHorizontal();
        window.flipVertical();

        // Size should remain the same
        QCOMPARE(window.size(), originalSize);

        // Flip both back
        window.flipHorizontal();
        window.flipVertical();
        QCOMPARE(window.size(), originalSize);
    }

    // =========================================================================
    // Combined Transform Tests
    // =========================================================================

    void testRotateAndZoom() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        window.setZoomLevel(2.0);
        window.rotateRight();

        QCOMPARE(window.zoomLevel(), 2.0);
    }

    void testTransformSequence() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        // Complex sequence of transforms
        window.setZoomLevel(1.5);
        window.rotateRight();
        window.flipHorizontal();
        window.setOpacity(0.7);
        window.rotateRight();
        window.flipVertical();
        window.setZoomLevel(0.8);

        // Verify state
        QCOMPARE(window.zoomLevel(), 0.8);
        QCOMPARE(window.opacity(), 0.7);
    }

    // =========================================================================
    // Watermark Tests
    // =========================================================================

    void testWatermarkSettings() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        WatermarkRenderer::Settings settings;
        settings.enabled = true;
        settings.imagePath = "/test/watermark.png";
        settings.position = WatermarkRenderer::TopRight;
        settings.opacity = 0.7;

        window.setWatermarkSettings(settings);

        WatermarkRenderer::Settings retrieved = window.watermarkSettings();
        QCOMPARE(retrieved.enabled, true);
        QCOMPARE(retrieved.imagePath, QString("/test/watermark.png"));
        QCOMPARE(retrieved.position, WatermarkRenderer::TopRight);
        QVERIFY(qFuzzyCompare(retrieved.opacity, 0.7));
    }

    // =========================================================================
    // Click-Through Tests
    // =========================================================================

    void testInitialClickThrough() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        QCOMPARE(window.isClickThrough(), false);
    }

    void testSetClickThrough() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow window(pixmap, QPoint(0, 0));

        window.setClickThrough(true);
        QCOMPARE(window.isClickThrough(),
                 PlatformFeatures::instance().capabilities().supportsClickThrough);

        window.setClickThrough(false);
        QCOMPARE(window.isClickThrough(), false);
    }

    // =========================================================================
    // Signal Tests
    // =========================================================================

    void testClosedSignal() {
        QPixmap pixmap = createTestPixmap(100, 100);
        PinWindow *window = new PinWindow(pixmap, QPoint(0, 0));

        QSignalSpy spy(window, &PinWindow::closed);
        QVERIFY(spy.isValid());

        window->close();

        QCOMPARE(spy.count(), 1);
        QList<QVariant> arguments = spy.takeFirst();
        QCOMPARE(arguments.at(0).value<PinWindow*>(), window);
    }
};

QTEST_MAIN(TestPinWindowTransform)
#include "tst_Transform.moc"
