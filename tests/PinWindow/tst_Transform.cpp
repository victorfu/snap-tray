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
#include "annotations/MosaicRectAnnotation.h"
#include "annotations/MarkerStroke.h"
#include "capture/ICaptureEngine.h"
#include "pinwindow/ResizeHandler.h"
#include "tools/ToolManager.h"
#include <QScreen>

namespace {
class PinFrameFixture final : public ICaptureEngine {
public:
    explicit PinFrameFixture(QObject* parent) : ICaptureEngine(parent) {}
    QImage frame;
    bool setRegion(const QRect&, QScreen*) override { return true; }
    bool start() override { return true; }
    void stop() override {}
    bool isRunning() const override { return true; }
    QImage captureFrame() override { return frame; }
    QString engineName() const override { return QStringLiteral("fixture"); }
};
}

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
    void testNativeStopEndsLivePin_data() {
        QTest::addColumn<bool>("replaceEngine");
        QTest::newRow("active-engine") << false;
        QTest::newRow("stale-engine") << true;
    }

    void testNativeStopEndsLivePin() {
        PinWindow window(createTestPixmap(100, 80), QPoint(), nullptr, false, false);
        QFETCH(bool, replaceEngine);
        const auto startFixture = [&window]() {
            auto* capture = new PinFrameFixture(&window);
            window.m_captureEngine = capture;
            window.m_isLiveMode = true;
            window.m_captureTimer = new QTimer(&window);
            window.m_captureTimer->start(1000);
            window.m_liveIndicatorTimer = new QTimer(&window);
            window.m_liveIndicatorTimer->start(1000);
            window.connectLiveCaptureEngineSignals();
            return capture;
        };
        auto* capture = startFixture();
        capture->stoppedByUser();
        if (replaceEngine) {
            window.stopLiveCapture();
            capture = startFixture();
            QCoreApplication::sendPostedEvents(&window, QEvent::MetaCall);
            QVERIFY(window.isLiveMode());
            QCOMPARE(window.m_captureEngine, capture);
            capture->stoppedByUser();
        }
        QTRY_VERIFY(!window.isLiveMode());
        QVERIFY(!window.m_captureEngine);
        QVERIFY(!window.m_captureTimer);
        QVERIFY(!window.m_liveIndicatorTimer);
        QVERIFY(!window.isLivePaused());
    }

    void testHiddenLiveMosaicToolDefersSources_data() {
        QTest::addColumn<bool>("showToolbar");
        QTest::newRow("toolbar-reopen") << true;
        QTest::newRow("annotation-mode-entry") << false;
    }

    void testHiddenLiveMosaicToolDefersSources() {
        QFETCH(bool, showToolbar);
        auto* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen);
        QPixmap source(QSize(100, 80) * screen->devicePixelRatio());
        source.setDevicePixelRatio(screen->devicePixelRatio());
        source.fill(Qt::red);
        PinWindow window(source, QPoint(), nullptr, false, false);
        window.setSourceRegion(QRect(screen->geometry().topLeft(), QSize(100, 80)), screen);
        window.setZoomLevel(2.0);
        window.rotateRight();
        window.flipHorizontal();
        window.showToolbar();
        window.handleToolbarToolSelected(static_cast<int>(ToolId::Mosaic));
        QVERIFY(window.isAnnotationMode());
        auto* capture = new PinFrameFixture(&window);
        capture->frame = source.toImage();
        window.m_captureEngine = capture;
        window.m_isLiveMode = true;

        window.hideToolbarPreservingToolState();
        QVERIFY(!window.isAnnotationMode());
        QCOMPARE(window.m_toolManager->currentTool(), ToolId::Mosaic);
        const auto frozenSource = window.m_sharedSourcePixmap;
        const auto revision = window.m_annotationLayer->revision();
        capture->frame.fill(Qt::blue);
        window.updateLiveFrame();
        QCOMPARE(window.m_displayPixmap.toImage().pixelColor(0, 0), QColor(Qt::blue));
        QCOMPARE(window.m_sharedSourcePixmap.get(), frozenSource.get());
        QCOMPARE(window.m_annotationLayer->revision(), revision);

        if (showToolbar) window.showToolbar();
        else window.enterAnnotationMode();
        QVERIFY(window.isAnnotationMode());
        QCOMPARE(window.m_sharedSourcePixmap->deviceIndependentSize().toSize(), QSize(200, 160));
        QCOMPARE(window.m_sharedSourcePixmap->toImage().pixelColor(0, 0), QColor(Qt::blue));
        capture->frame.fill(Qt::green);
        window.updateLiveFrame();
        QCOMPARE(window.m_sharedSourcePixmap->toImage().pixelColor(0, 0), QColor(Qt::green));
        window.stopLiveCapture();
    }

    void testLiveFramesDeferUnusedMosaicSources() {
        auto* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen);
        const qreal dpr = screen->devicePixelRatio();
        QPixmap source(QSize(100, 80) * dpr);
        source.setDevicePixelRatio(dpr);
        source.fill(Qt::red);
        PinWindow window(source, QPoint(), nullptr, false, false);
        window.setSourceRegion(QRect(screen->geometry().topLeft(), QSize(100, 80)), screen);
        window.setZoomLevel(2.0);
        window.rotateRight();
        window.flipHorizontal();
        window.initializeAnnotationComponents();
        window.m_annotationLayer->addItem(std::make_unique<MarkerStroke>(
            QVector<QPointF>{QPointF(10, 10), QPointF(30, 10)}, QColor(Qt::black), 4));
        const auto originalSource = window.m_sharedSourcePixmap;
        const auto revision = window.m_annotationLayer->revision();
        auto* capture = new PinFrameFixture(&window);
        capture->frame = source.toImage();
        window.m_captureEngine = capture;
        window.m_isLiveMode = true;
        for (const QColor& color : {QColor(Qt::green), QColor(Qt::blue)}) {
            capture->frame.fill(color);
            window.updateLiveFrame();
            QCOMPARE(window.m_displayPixmap.toImage().pixelColor(0, 0), color);
            QCOMPARE(window.m_sharedSourcePixmap.get(), originalSource.get());
            QCOMPARE(window.m_annotationLayer->revision(), revision);
        }
        window.pauseLiveCapture();
        QCOMPARE(window.m_sharedSourcePixmap->toImage().pixelColor(0, 0), QColor(Qt::blue));
        window.resumeLiveCapture();
        capture->frame.fill(Qt::green);
        window.updateLiveFrame();
        // Selecting mosaic after deferred frames must use the latest pixels.
        window.handleToolbarToolSelected(static_cast<int>(ToolId::Mosaic));
        QCOMPARE(window.m_sharedSourcePixmap->toImage().pixelColor(0, 0), QColor(Qt::green));
        const auto editingRevision = window.m_annotationLayer->revision();
        capture->frame.fill(Qt::yellow);
        window.updateLiveFrame();
        QCOMPARE(window.m_sharedSourcePixmap->toImage().pixelColor(0, 0), QColor(Qt::yellow));
        QCOMPARE(window.m_annotationLayer->revision(), editingRevision);
        window.handleToolbarToolSelected(static_cast<int>(ToolId::Selection));
        capture->frame.fill(Qt::blue);
        window.updateLiveFrame();
        window.stopLiveCapture();
        QCOMPARE(window.m_sharedSourcePixmap->deviceIndependentSize().toSize(), QSize(200, 160));
        QCOMPARE(window.m_sharedSourcePixmap->toImage().pixelColor(0, 0), QColor(Qt::blue));
    }

    void testLiveMosaicsUseCurrentUnrotatedFrame_data() {
        QTest::addColumn<qreal>("zoom");
        QTest::addColumn<bool>("rectangle");
        for (qreal zoom : {1.0, 2.0}) {
            for (bool rectangle : {false, true}) {
                QTest::addRow("zoom-%.1f-rect-%d", zoom, rectangle) << zoom << rectangle;
            }
        }
    }

    void testLiveMosaicsUseCurrentUnrotatedFrame() {
        QFETCH(qreal, zoom);
        QFETCH(bool, rectangle);
        auto* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen);
        const qreal dpr = screen->devicePixelRatio();
        QPixmap source(QSize(100, 80) * dpr);
        source.setDevicePixelRatio(dpr);
        source.fill(Qt::red);
        PinWindow window(source, QPoint(), nullptr, false, false);
        window.setSourceRegion(QRect(screen->geometry().topLeft(), QSize(100, 80)), screen);
        window.setZoomLevel(zoom);
        window.rotateRight();
        window.flipHorizontal();
        window.initializeAnnotationComponents();
        const QPoint sample(qRound(60 * zoom), qRound(40 * zoom));
        if (rectangle) {
            window.m_annotationLayer->addItem(std::make_unique<MosaicRectAnnotation>(
                QRect(sample - QPoint(10, 10), QSize(30, 30)), window.m_sharedSourcePixmap, 12));
        } else {
            window.m_annotationLayer->addItem(std::make_unique<MosaicStroke>(
                QVector<QPoint>{sample, sample + QPoint(2, 0)}, window.m_sharedSourcePixmap, 2));
        }
        auto* capture = new PinFrameFixture(&window);
        capture->frame = source.toImage();
        for (int y = 0; y < capture->frame.height(); ++y) {
            for (int x = 0; x < capture->frame.width(); ++x) {
                capture->frame.setPixelColor(x, y, x % 2 ? Qt::blue : Qt::green);
            }
        }
        window.m_captureEngine = capture;
        window.m_isLiveMode = true;
        window.updateLiveFrame();
        if (zoom == 1.0) {
            QCOMPARE(window.m_sharedSourcePixmap->cacheKey(), window.m_originalPixmap.cacheKey());
        }
        const QPixmap expected = window.m_originalPixmap.scaled(
            QSize(100, 80) * zoom * dpr, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        QCOMPARE(window.m_sharedSourcePixmap->toImage(), expected.toImage());
        QImage liveMask(expected.size(), QImage::Format_ARGB32_Premultiplied);
        liveMask.setDevicePixelRatio(dpr);
        liveMask.fill(Qt::transparent);
        { QPainter painter(&liveMask); window.m_annotationLayer->draw(painter); }
        const QColor livePixel = liveMask.pixelColor(sample * dpr);
        QCOMPARE(livePixel.alpha(), 255);
        QCOMPARE(livePixel.red(), 0); // The old source was red; the new frame is blue/green.
        QVERIFY(livePixel.green() + livePixel.blue() > 0);

        // Redo after unused live frames must refresh a history-owned mosaic.
        window.m_annotationLayer->undo();
        capture->frame.fill(Qt::yellow);
        window.updateLiveFrame();
        window.m_annotationLayer->redo();
        QImage rendered(expected.size(), QImage::Format_ARGB32_Premultiplied);
        rendered.setDevicePixelRatio(dpr);
        rendered.fill(Qt::transparent);
        { QPainter painter(&rendered); window.m_annotationLayer->draw(painter); }
        QCOMPARE(rendered.pixelColor(sample * dpr), QColor(Qt::yellow));
        window.stopLiveCapture();
    }

    void testInteractiveResizeDefersMosaicRefresh_data() {
        QTest::addColumn<bool>("transform");
        QTest::addColumn<bool>("expireDebounce");
        for (bool transform : {false, true}) {
            for (bool expireDebounce : {false, true}) {
                QTest::addRow("transform-%d-expired-%d", transform, expireDebounce)
                    << transform << expireDebounce;
            }
        }
    }

    void testInteractiveResizeDefersMosaicRefresh() {
        QFETCH(bool, transform);
        QFETCH(bool, expireDebounce);
        constexpr qreal dpr = 2.0;
        const QSize logicalSize(100, 80);
        QPixmap source(logicalSize * dpr);
        source.setDevicePixelRatio(dpr);
        source.fill(Qt::red);
        { QPainter painter(&source); painter.fillRect(50, 0, 50, 80, Qt::blue); }
        PinWindow window(source, QPoint(), nullptr, false, false);
        if (transform) { window.rotateRight(); window.flipHorizontal(); }
        window.initializeAnnotationComponents();
        const QPoint sample(65, 40);
        window.m_annotationLayer->addItem(std::make_unique<MosaicStroke>(
            QVector<QPoint>{sample, sample + QPoint(2, 0)}, window.m_sharedSourcePixmap, 2));
        const auto render = [&window]() {
            const auto& canvas = *window.m_sharedSourcePixmap;
            QImage image(canvas.size(), QImage::Format_ARGB32_Premultiplied);
            image.setDevicePixelRatio(canvas.devicePixelRatio());
            image.fill(Qt::transparent);
            QPainter painter(&image);
            window.m_annotationLayer->drawCached(
                painter, canvas.deviceIndependentSize().toSize(), canvas.devicePixelRatio());
            return image;
        };
        QCOMPARE(render().pixelColor(sample * dpr), QColor(Qt::blue));
        const auto originalSource = window.m_sharedSourcePixmap;
        const auto cachedEntries = window.m_annotationLayer->cacheStats().entryCount;
        QVERIFY(cachedEntries > 0);

        const QSize originalSize = window.size();
        const QPoint corner(originalSize.width() - 1, originalSize.height() - 1);
        const QPoint globalCorner = window.mapToGlobal(corner);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(corner), QPointF(globalCorner),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        window.mousePressEvent(&press);
        QVERIFY(window.m_isResizing);
        for (qreal scale : {1.5, 2.0}) {
            const QSize newSize = originalSize * scale;
            const QPoint delta(newSize.width() - originalSize.width(),
                               newSize.height() - originalSize.height());
            QMouseEvent move(QEvent::MouseMove, QPointF(corner + delta), QPointF(globalCorner + delta),
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            window.mouseMoveEvent(&move);
            QCOMPARE(window.size(), newSize);
            QCOMPARE(window.m_sharedSourcePixmap.get(), originalSource.get());
            QCOMPARE(window.m_annotationLayer->cacheStats().entryCount, cachedEntries);
        }
        if (expireDebounce) {
            // The single-shot timer may fire while the pointer is held still.
            QTRY_VERIFY_WITH_TIMEOUT(!window.m_resizeFinishTimer->isActive(), 1000);
            QCOMPARE(window.m_sharedSourcePixmap.get(), originalSource.get());
            QVERIFY(window.m_pendingHighQualityUpdate);
        }
        const QPoint releasePos(window.width() - 1, window.height() - 1);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(releasePos),
                            QPointF(window.mapToGlobal(releasePos)), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        window.mouseReleaseEvent(&release);
        QVERIFY(!window.m_isResizing);
        QVERIFY(!window.m_resizeHandler->isResizing());
        QVERIFY(!window.m_pendingHighQualityUpdate);
        QVERIFY(!window.m_resizeFinishTimer->isActive());
        QVERIFY(window.m_sharedSourcePixmap != originalSource);
        QCOMPARE(window.m_sharedSourcePixmap->deviceIndependentSize().toSize(), logicalSize * 2);
        // This existing stroke must sample the newly enlarged canvas immediately.
        QCOMPARE(render().pixelColor(sample * dpr), QColor(Qt::red));
    }

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
