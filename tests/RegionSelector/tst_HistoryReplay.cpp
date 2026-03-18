#include <QtTest/QtTest>

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>
#include <QtQml/qqmlextensionplugin.h>

#include "RegionSelector.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/ShapeAnnotation.h"
#include "history/AnnotationSerializer.h"
#include "history/HistoryRecorder.h"
#include "history/HistoryStore.h"
#include "qml/QmlDialog.h"
#include "region/RegionExportManager.h"
#include "share/ShareUploadClient.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

namespace {

class HeadlessQmlDialog final : public SnapTray::QmlDialog
{
public:
    HeadlessQmlDialog(const QUrl& qmlSource,
                      QObject* viewModel,
                      const QString& contextPropertyName,
                      QObject* parent = nullptr)
        : QmlDialog(qmlSource, viewModel, contextPropertyName, parent)
    {
    }

    void showAt(const QPoint& pos = QPoint()) override
    {
        Q_UNUSED(pos);
        m_visible = true;
    }

    void close() override
    {
        if (!m_visible) {
            return;
        }

        m_visible = false;
        emit closed();
        deleteLater();
    }

private:
    bool m_visible = false;
};

class HeadlessRegionSelector final : public RegionSelector
{
public:
    using RegionSelector::RegionSelector;

protected:
    void closeEvent(QCloseEvent* event) override
    {
        event->ignore();
    }
};

QImage makeImage(const QSize& size, const QColor& color)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(color);
    return image;
}

QPixmap makePixmap(const QSize& size, const QColor& color)
{
    QPixmap pixmap = QPixmap::fromImage(makeImage(size, color));
    pixmap.setDevicePixelRatio(1.0);
    return pixmap;
}

QByteArray makeShapePayload(const QRect& rect, const QColor& color)
{
    AnnotationLayer layer;
    layer.addItem(std::make_unique<ShapeAnnotation>(rect, ShapeType::Rectangle, color, 2, false));
    return SnapTray::serializeAnnotationLayer(layer);
}

QRect firstShapeRect(const AnnotationLayer& layer)
{
    QRect rect;
    layer.forEachItem([&rect](const AnnotationItem* item) {
        if (!rect.isValid()) {
            if (const auto* shape = dynamic_cast<const ShapeAnnotation*>(item)) {
                rect = shape->rect();
            }
        }
    });
    return rect;
}

} // namespace

class tst_RegionSelectorHistoryReplay : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void testRecordCaptureSessionWithoutPrecomputedHistoryCache();
    void testHistoryReplayAppliesAndRestoresCanvasGeometry();
    void testShareSuccessUsesFrozenHistorySnapshot();

private:
    static void installHeadlessTransientUi(RegionSelector& selector);
    static void configureSelectorCanvas(RegionSelector& selector,
                                        QScreen* screen,
                                        const QSize& canvasSize,
                                        const QRect& selectionRect,
                                        const QColor& backgroundColor);
};

void tst_RegionSelectorHistoryReplay::installHeadlessTransientUi(RegionSelector& selector)
{
    selector.m_dialogFactory =
        [](const QUrl& qmlSource, QObject* viewModel, const QString& contextPropertyName, QObject* parent) {
            return new HeadlessQmlDialog(qmlSource, viewModel, contextPropertyName, parent);
        };
    selector.m_restoreAfterDialogCancelledHook = []() {};
}

void tst_RegionSelectorHistoryReplay::configureSelectorCanvas(RegionSelector& selector,
                                                              QScreen* screen,
                                                              const QSize& canvasSize,
                                                              const QRect& selectionRect,
                                                              const QColor& backgroundColor)
{
    selector.m_currentScreen = screen;
    selector.m_devicePixelRatio = 1.0;
    selector.m_inputState.multiRegionMode = false;
    selector.applyCanvasGeometry(canvasSize);

    selector.m_backgroundPixmap = makePixmap(canvasSize, backgroundColor);
    selector.m_sharedSourcePixmap = std::make_shared<const QPixmap>(selector.m_backgroundPixmap);
    selector.m_toolManager->setSourcePixmap(selector.m_sharedSourcePixmap);
    selector.m_toolManager->setDevicePixelRatio(selector.m_devicePixelRatio);
    selector.m_exportManager->setBackgroundPixmap(selector.m_backgroundPixmap);
    selector.m_exportManager->setDevicePixelRatio(selector.m_devicePixelRatio);
    selector.m_exportManager->setSourceScreen(screen);
    selector.m_annotationLayer->clear();
    selector.m_selectionManager->clearSelection();
    selector.m_selectionManager->setSelectionRect(selectionRect);
    selector.m_selectionManager->setFromDetectedWindow(selectionRect);
}

void tst_RegionSelectorHistoryReplay::init()
{
    QVERIFY(SnapTray::HistoryRecorder::instance().waitForIdleForTests());
    qunsetenv("SNAPTRAY_HISTORY_DIR");
}

void tst_RegionSelectorHistoryReplay::cleanup()
{
    QVERIFY(SnapTray::HistoryRecorder::instance().waitForIdleForTests());
    qunsetenv("SNAPTRAY_HISTORY_DIR");
}

void tst_RegionSelectorHistoryReplay::testRecordCaptureSessionWithoutPrecomputedHistoryCache()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    qputenv("SNAPTRAY_HISTORY_DIR", tempDir.path().toLocal8Bit());

    HeadlessRegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);

    QScreen* screen = QGuiApplication::primaryScreen();
    configureSelectorCanvas(selector, screen, QSize(90, 70), QRect(10, 8, 30, 20), QColor(10, 20, 30));

    selector.recordCaptureSession(makeImage(QSize(30, 20), QColor(40, 50, 60)));
    QCoreApplication::processEvents();
    QVERIFY(SnapTray::HistoryRecorder::instance().waitForIdleForTests());

    const QList<SnapTray::HistoryEntry> entries = SnapTray::HistoryStore::loadEntries();
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().canvasLogicalSize, QSize(90, 70));
    QCOMPARE(entries.first().selectionRect, QRect(10, 8, 30, 20));
}

void tst_RegionSelectorHistoryReplay::testHistoryReplayAppliesAndRestoresCanvasGeometry()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    qputenv("SNAPTRAY_HISTORY_DIR", tempDir.path().toLocal8Bit());

    SnapTray::CaptureSessionWriteRequest request;
    request.canvasImage = makeImage(QSize(240, 160), QColor(20, 30, 40));
    request.resultImage = makeImage(QSize(40, 30), QColor(100, 110, 120));
    request.selectionRect = QRect(25, 18, 40, 30);
    request.annotationsJson = makeShapePayload(QRect(28, 20, 12, 10), Qt::red);
    request.canvasLogicalSize = QSize(240, 160);
    request.devicePixelRatio = 1.0;
    request.createdAt = QDateTime(QDate(2026, 3, 18), QTime(13, 0, 0));

    const auto entry = SnapTray::HistoryStore::writeCaptureSession(request);
    QVERIFY(entry.has_value());

    HeadlessRegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);

    const QSize liveCanvasSize(180, 120);
    const QRect liveSelectionRect(6, 7, 30, 22);
    const QRect liveShapeRect(8, 9, 14, 10);

    QScreen* screen = QGuiApplication::primaryScreen();
    configureSelectorCanvas(selector, screen, liveCanvasSize, liveSelectionRect, QColor(5, 15, 25));
    selector.m_annotationLayer->addItem(
        std::make_unique<ShapeAnnotation>(liveShapeRect, ShapeType::Rectangle, Qt::green, 2, false));

    QVERIFY(selector.beginHistoryReplay(entry->id));
    QCOMPARE(selector.size(), QSize(240, 160));
    QCOMPARE(selector.m_selectionManager->bounds().size(), QSize(240, 160));
    QCOMPARE(selector.m_selectionManager->selectionRect(), request.selectionRect);
    QCOMPARE(firstShapeRect(*selector.m_annotationLayer), QRect(28, 20, 12, 10));

    selector.navigateHistoryReplay(1);
    QCOMPARE(selector.size(), liveCanvasSize);
    QCOMPARE(selector.m_selectionManager->bounds().size(), liveCanvasSize);
    QCOMPARE(selector.m_selectionManager->selectionRect(), liveSelectionRect);
    QCOMPARE(firstShapeRect(*selector.m_annotationLayer), liveShapeRect);
}

void tst_RegionSelectorHistoryReplay::testShareSuccessUsesFrozenHistorySnapshot()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    qputenv("SNAPTRAY_HISTORY_DIR", tempDir.path().toLocal8Bit());

    HeadlessRegionSelector selector;
    selector.setAttribute(Qt::WA_DeleteOnClose, false);
    installHeadlessTransientUi(selector);

    const QRect originalSelectionRect(10, 12, 50, 30);
    const QRect originalShapeRect(14, 16, 10, 8);

    QScreen* screen = QGuiApplication::primaryScreen();
    configureSelectorCanvas(selector, screen, QSize(160, 90), originalSelectionRect, QColor(220, 220, 220));
    selector.m_annotationLayer->addItem(
        std::make_unique<ShapeAnnotation>(originalShapeRect, ShapeType::Rectangle, Qt::red, 2, false));

    const auto snapshot = selector.makeHistoryCaptureSnapshot(
        QDateTime(QDate(2026, 3, 18), QTime(13, 30, 0)));
    QVERIFY(snapshot.has_value());

    const QPixmap selectedRegion = selector.m_exportManager->getSelectedRegion(originalSelectionRect, 0);
    QVERIFY(!selectedRegion.isNull());

    selector.m_pendingShareSubmission = RegionSelector::PendingHistorySubmission{
        *snapshot,
        selectedRegion.toImage()
    };
    selector.m_shareInProgress = true;

    selector.m_selectionManager->setSelectionRect(QRect(40, 10, 20, 15));
    selector.m_selectionManager->setFromDetectedWindow(QRect(40, 10, 20, 15));
    selector.m_annotationLayer->clear();
    selector.m_annotationLayer->addItem(
        std::make_unique<ShapeAnnotation>(QRect(42, 12, 6, 6), ShapeType::Rectangle, Qt::blue, 2, false));
    selector.m_cornerRadius = 6;

    QVERIFY(selector.m_shareClient);
    const bool invoked = QMetaObject::invokeMethod(
        selector.m_shareClient,
        "uploadSucceeded",
        Qt::DirectConnection,
        Q_ARG(QString, QStringLiteral("https://example.com/share/abc")),
        Q_ARG(QDateTime, QDateTime::currentDateTimeUtc().addDays(1)),
        Q_ARG(bool, false));
    QVERIFY(invoked);

    QCoreApplication::processEvents();
    QVERIFY(SnapTray::HistoryRecorder::instance().waitForIdleForTests());

    const QList<SnapTray::HistoryEntry> entries = SnapTray::HistoryStore::loadEntries();
    QCOMPARE(entries.size(), 1);

    const auto loadedEntry = SnapTray::HistoryStore::loadEntry(entries.first().id);
    QVERIFY(loadedEntry.has_value());
    QCOMPARE(loadedEntry->selectionRect, originalSelectionRect);
    QCOMPARE(loadedEntry->cornerRadius, 0);

    QImage persistedResult(loadedEntry->resultPath);
    QVERIFY(!persistedResult.isNull());
    QCOMPARE(persistedResult.size(), selectedRegion.toImage().size());

    QFile annotationsFile(loadedEntry->annotationsPath);
    QVERIFY(annotationsFile.open(QIODevice::ReadOnly));
    AnnotationLayer layer;
    QString errorMessage;
    const auto sourcePixmap = std::make_shared<const QPixmap>(makePixmap(QSize(160, 90), QColor(220, 220, 220)));
    QVERIFY(SnapTray::deserializeAnnotationLayer(
        annotationsFile.readAll(), &layer, sourcePixmap, &errorMessage));
    QCOMPARE(firstShapeRect(layer), originalShapeRect);
}

QTEST_MAIN(tst_RegionSelectorHistoryReplay)
#include "tst_HistoryReplay.moc"
