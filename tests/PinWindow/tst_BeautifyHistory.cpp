// #344: a beautified pin is saved to history as a new entry that carries its
// exact Beautify settings, and a pin continues from its own settings rather
// than from the global last-used ones.
#include <QtTest/QtTest>
#include <QGuiApplication>
#include <QColorSpace>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include "PinWindow.h"

#include "annotations/AnnotationLayer.h"
#include "annotations/ShapeAnnotation.h"
#include "beautify/BeautifyRenderer.h"
#include "beautify/BeautifySettingsJson.h"
#include "history/AnnotationSerializer.h"
#include "history/HistoryRecorder.h"
#include "history/HistoryStore.h"
#include "settings/BeautifySettingsManager.h"
#include "settings/FileSettingsManager.h"

namespace {

QPixmap patternPixmap(const QSize& pixelSize, qreal dpr, bool transparentCorner = false,
                      QImage::Format format = QImage::Format_ARGB32_Premultiplied)
{
    QImage image(pixelSize, format);
    for (int y = 0; y < pixelSize.height(); ++y) {
        for (int x = 0; x < pixelSize.width(); ++x) {
            image.setPixelColor(x, y, QColor(x % 256, y % 256, (x + y) % 256, 255));
        }
    }
    if (transparentCorner) {
        QPainter painter(&image);
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        painter.fillRect(QRect(0, 0, 12, 12), Qt::transparent);
    }
    QPixmap pixmap = QPixmap::fromImage(image, Qt::NoOpaqueDetection);
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

BeautifySettings pinLook()
{
    BeautifySettings settings;
    settings.backgroundType = BeautifyBackgroundType::Solid;
    settings.backgroundColor = QColor(12, 34, 56);
    settings.padding = 23;
    settings.cornerRadius = 5;
    settings.shadowEnabled = false;
    settings.borderEnabled = true;
    settings.borderWidth = 3;
    return settings;
}

BeautifySettings otherLook()
{
    BeautifySettings settings;
    settings.padding = 91;
    settings.gradientAngle = 10;
    settings.aspectRatio = BeautifyAspectRatio::Square_1_1;
    return settings;
}

QJsonObject json(const BeautifySettings& settings)
{
    return SnapTray::beautifySettingsToJson(settings);
}

QByteArray fileBytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Pixels only: a PNG read back carries no device pixel ratio (the entry's
// manifest does), and QCOMPARE would report that as a difference.
QImage normalized(const QImage& image)
{
    QImage pixels = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    pixels.setColorSpace(QColorSpace());
    pixels.setDevicePixelRatio(1.0);
    return pixels;
}

// The capture a pin was made from, as RegionSelector records it.
SnapTray::HistoryEntry writeCaptureFor(const QPixmap& pinPixmap)
{
    AnnotationLayer annotations;
    annotations.addItem(std::make_unique<ShapeAnnotation>(
        QRect(4, 6, 32, 24), ShapeType::Rectangle, QColor(255, 0, 0), 3, false));

    SnapTray::CaptureSessionWriteRequest request;
    request.canvasImage = QImage(QSize(640, 400), QImage::Format_ARGB32_Premultiplied);
    request.canvasImage.fill(QColor(70, 80, 90));
    request.resultImage = pinPixmap.toImage();
    request.selectionRect = QRect(10, 20, 100, 60);
    request.annotationsJson = SnapTray::serializeAnnotationLayer(annotations);
    request.devicePixelRatio = pinPixmap.devicePixelRatio();
    request.canvasLogicalSize = QSize(640, 400);
    request.createdAt = QDateTime(QDate(2026, 1, 1), QTime(12, 0, 0, 0));
    const auto entry = SnapTray::HistoryStore::writeCaptureSession(request);
    return entry.value_or(SnapTray::HistoryEntry{});
}

} // namespace

class TestPinWindowBeautifyHistory : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void panelContinuesFromThePinsOwnSettings();
    void unmodifiedPinKeepsItsEditableDocument_data();
    void unmodifiedPinKeepsItsEditableDocument();
    void editedPinIsFlattenedButKeepsBeautify();
    void pinWithoutCaptureIsFlattened();

private:
    // Saves through the same path as the panel's Save button.
    void beautifySave(PinWindow& window, const BeautifySettings& settings)
    {
        window.onBeautifySave(settings);
        QVERIFY(SnapTray::HistoryRecorder::instance().waitForIdleForTests());
    }

    QTemporaryDir m_dir;
};

void TestPinWindowBeautifyHistory::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for PinWindow tests in this environment.");
    }
    QVERIFY(m_dir.isValid());
}

void TestPinWindowBeautifyHistory::init()
{
    const QString base = m_dir.filePath(QString::fromLatin1(QTest::currentTestFunction()) +
                                        QLatin1Char('_') +
                                        QString::fromLatin1(QTest::currentDataTag()));
    qputenv("SNAPTRAY_HISTORY_DIR", (base + QStringLiteral("/history")).toLocal8Bit());
    QVERIFY(QDir().mkpath(base + QStringLiteral("/saved")));
    FileSettingsManager::instance().saveAutoSaveScreenshots(true);
    FileSettingsManager::instance().saveScreenshotPath(base + QStringLiteral("/saved"));
    BeautifySettingsManager::instance().saveSettings(BeautifySettings{});
}

void TestPinWindowBeautifyHistory::cleanup()
{
    QVERIFY(SnapTray::HistoryRecorder::instance().waitForIdleForTests());
    qunsetenv("SNAPTRAY_HISTORY_DIR");
}

void TestPinWindowBeautifyHistory::panelContinuesFromThePinsOwnSettings()
{
    const QPixmap pixmap = patternPixmap(QSize(120, 80), 1.0);
    BeautifySettingsManager::instance().saveSettings(otherLook());

    // A fresh pin starts from the global last-used settings, as before.
    PinWindow fresh(pixmap, QPoint(0, 0), nullptr, false, false);
    QCOMPARE(json(fresh.beautifyPanelSettings()), json(otherLook()));

    // A pin reopened from a beautified history entry starts from that entry's.
    PinWindow reopened(pixmap, QPoint(0, 0), nullptr, false, false);
    reopened.setHistoryOrigin(QStringLiteral("capture_some_entry"), pinLook());
    QCOMPARE(json(reopened.beautifyPanelSettings()), json(pinLook()));

    // Saving sets both this pin's settings and the global default for new work.
    beautifySave(fresh, pinLook());
    QCOMPARE(json(BeautifySettingsManager::instance().loadSettings()), json(pinLook()));

    // Changing the global default afterwards changes neither pin.
    BeautifySettingsManager::instance().saveSettings(BeautifySettings{});
    QCOMPARE(json(fresh.beautifyPanelSettings()), json(pinLook()));
    QCOMPARE(json(reopened.beautifyPanelSettings()), json(pinLook()));
}

void TestPinWindowBeautifyHistory::unmodifiedPinKeepsItsEditableDocument_data()
{
    QTest::addColumn<QSize>("pixelSize");
    QTest::addColumn<qreal>("devicePixelRatio");
    QTest::addColumn<bool>("transparent");
    QTest::addColumn<bool>("screenGrab");

    // What a screen grab is: opaque RGB32, tagged with a colour space.
    QTest::newRow("1x screen grab") << QSize(100, 60) << qreal(1.0) << false << true;
    QTest::newRow("1x") << QSize(100, 60) << qreal(1.0) << false << false;
    QTest::newRow("2x") << QSize(200, 120) << qreal(2.0) << false << false;
    QTest::newRow("1.5x") << QSize(150, 90) << qreal(1.5) << false << false;
    // 101x61 logical: what a selection of that size captures at 150 %.
    QTest::newRow("1.5x odd logical size") << QSize(152, 92) << qreal(1.5) << false << false;
    QTest::newRow("1.25x") << QSize(125, 75) << qreal(1.25) << false << false;
    QTest::newRow("2x transparent") << QSize(200, 120) << qreal(2.0) << true << false;
}

void TestPinWindowBeautifyHistory::unmodifiedPinKeepsItsEditableDocument()
{
    QFETCH(QSize, pixelSize);
    QFETCH(qreal, devicePixelRatio);
    QFETCH(bool, transparent);
    QFETCH(bool, screenGrab);

    QPixmap pixmap = patternPixmap(pixelSize, devicePixelRatio, transparent,
                                   screenGrab ? QImage::Format_RGB32
                                              : QImage::Format_ARGB32_Premultiplied);
    if (screenGrab) {
        QImage tagged = pixmap.toImage();
        tagged.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
        pixmap = QPixmap::fromImage(tagged);
        pixmap.setDevicePixelRatio(devicePixelRatio);
    }
    const SnapTray::HistoryEntry capture = writeCaptureFor(pixmap);
    QVERIFY(!capture.id.isEmpty());
    const QByteArray captureManifest =
        fileBytes(QDir(capture.entryDirectory).filePath(QStringLiteral("manifest.json")));

    PinWindow window(pixmap, QPoint(0, 0), nullptr, false, false);
    window.setHistoryOrigin(capture.id, std::nullopt);
    const QPixmap source = window.getExportPixmapWithAnnotations();
    const QImage expected = BeautifyRenderer::applyToPixmap(source, pinLook()).toImage();
    QVERIFY(!expected.isNull());

    QSignalSpy saved(&window, &PinWindow::saveCompleted);
    beautifySave(window, pinLook());
    QCOMPARE(saved.count(), 1);

    const QList<SnapTray::HistoryEntry> entries = SnapTray::HistoryStore::loadEntries();
    QCOMPARE(entries.size(), 2);
    const SnapTray::HistoryEntry& beautified = entries.first();
    QVERIFY(beautified.id != capture.id);
    QVERIFY(beautified.replayAvailable);
    QVERIFY(beautified.beautify);
    QCOMPARE(json(*beautified.beautify), json(pinLook()));

    // Saved as a new entry; the capture it came from is untouched.
    QCOMPARE(fileBytes(QDir(capture.entryDirectory).filePath(QStringLiteral("manifest.json"))),
             captureManifest);

    // The original picture and its annotations, not a flattened copy.
    QCOMPARE(fileBytes(beautified.canvasPath), fileBytes(capture.canvasPath));
    QCOMPARE(fileBytes(beautified.annotationsPath), fileBytes(capture.annotationsPath));
    QCOMPARE(beautified.selectionRect, capture.selectionRect);
    QCOMPARE(beautified.devicePixelRatio, devicePixelRatio);

    // The stored result is the beautified picture that was exported.
    QCOMPARE(beautified.resultSize, expected.size());
    QCOMPARE(normalized(QImage(beautified.resultPath)), normalized(expected));

    // Reopening: the capture's own result pinned again, beautified with the
    // stored settings while the global default is something else, gives the
    // same picture at the same size.
    BeautifySettingsManager::instance().saveSettings(otherLook());
    QPixmap reopenedPixmap = QPixmap::fromImage(QImage(capture.resultPath), Qt::NoOpaqueDetection);
    reopenedPixmap.setDevicePixelRatio(capture.devicePixelRatio);
    PinWindow reopened(reopenedPixmap, QPoint(0, 0), nullptr, false, false);
    reopened.setHistoryOrigin(beautified.id, beautified.beautify);
    const QImage again = BeautifyRenderer::applyToPixmap(
        reopened.getExportPixmapWithAnnotations(), reopened.beautifyPanelSettings()).toImage();
    QCOMPARE(again.size(), expected.size());
    QCOMPARE(normalized(again), normalized(expected));
}

void TestPinWindowBeautifyHistory::editedPinIsFlattenedButKeepsBeautify()
{
    const QPixmap pixmap = patternPixmap(QSize(200, 120), 2.0);
    const SnapTray::HistoryEntry capture = writeCaptureFor(pixmap);
    QVERIFY(!capture.id.isEmpty());

    PinWindow window(pixmap, QPoint(0, 0), nullptr, false);
    window.setHistoryOrigin(capture.id, std::nullopt);
    window.showToolbar();
    QVERIFY(window.m_annotationLayer != nullptr);
    // An edit made on the pin itself: the capture's document no longer
    // describes what is exported.
    window.m_annotationLayer->addItem(std::make_unique<ShapeAnnotation>(
        QRect(10, 10, 40, 20), ShapeType::Rectangle, QColor(0, 255, 0), 4, true));
    const QImage source = window.exportPixmapForMerge().toImage();
    QVERIFY(normalized(source) != normalized(pixmap.toImage()));

    beautifySave(window, pinLook());

    const QList<SnapTray::HistoryEntry> entries = SnapTray::HistoryStore::loadEntries();
    QCOMPARE(entries.size(), 2);
    const SnapTray::HistoryEntry& beautified = entries.first();
    QVERIFY(beautified.beautify);
    QCOMPARE(json(*beautified.beautify), json(pinLook()));
    QVERIFY(beautified.replayAvailable);

    // The pin's picture, edit included, is the entry's original; replaying it
    // and beautifying with the stored settings reproduces the saved result.
    QCOMPARE(normalized(QImage(beautified.canvasPath)), normalized(source));
    QCOMPARE(beautified.selectionRect, QRect(0, 0, 100, 60));
    QCOMPARE(beautified.canvasLogicalSize, QSize(100, 60));
    QCOMPARE(beautified.devicePixelRatio, 2.0);
    AnnotationLayer restored;
    QVERIFY(SnapTray::deserializeAnnotationLayer(
        fileBytes(beautified.annotationsPath), &restored, nullptr));
    QVERIFY(restored.isEmpty());
}

void TestPinWindowBeautifyHistory::pinWithoutCaptureIsFlattened()
{
    // A pasted or library pin: there is no capture document to take over.
    const QPixmap pixmap = patternPixmap(QSize(151, 91), 1.5, true);
    PinWindow window(pixmap, QPoint(0, 0), nullptr, false, false);
    // Opacity is how this pin is viewed, not part of the picture: the stored
    // original is opaque, the stored result is what was exported.
    window.setOpacity(0.5);
    const QPixmap source = window.getExportPixmapWithAnnotations();
    const QPixmap document = window.exportPixmapForMerge();
    QVERIFY(normalized(source.toImage()) != normalized(document.toImage()));

    beautifySave(window, pinLook());

    const QList<SnapTray::HistoryEntry> entries = SnapTray::HistoryStore::loadEntries();
    QCOMPARE(entries.size(), 1);
    const SnapTray::HistoryEntry& beautified = entries.first();
    QVERIFY(beautified.beautify);
    QVERIFY(beautified.replayAvailable);
    QCOMPARE(normalized(QImage(beautified.canvasPath)), normalized(document.toImage()));
    QCOMPARE(beautified.devicePixelRatio, 1.5);
    QCOMPARE(normalized(QImage(beautified.resultPath)),
             normalized(BeautifyRenderer::applyToPixmap(source, pinLook()).toImage()));
}

QTEST_MAIN(TestPinWindowBeautifyHistory)
#include "tst_BeautifyHistory.moc"
