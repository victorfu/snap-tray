#include <QtTest/QtTest>

#include "history/AnnotationSerializer.h"
#include "history/HistoryStore.h"
#include "beautify/BeautifySettingsJson.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/ShapeAnnotation.h"

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTemporaryDir>

namespace {

QImage makeImage(const QSize& size, const QColor& color)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(color);
    return image;
}

QByteArray sampleAnnotations()
{
    AnnotationLayer layer;
    layer.addItem(std::make_unique<ShapeAnnotation>(
        QRect(4, 6, 32, 24),
        ShapeType::Rectangle,
        QColor(255, 0, 0),
        3,
        false));
    return SnapTray::serializeAnnotationLayer(layer);
}

BeautifySettings sampleBeautify()
{
    BeautifySettings settings;
    settings.backgroundType = BeautifyBackgroundType::RadialGradient;
    settings.backgroundColor = QColor(1, 2, 3, 4);
    settings.gradientStartColor = QColor(250, 120, 30);
    settings.gradientEndColor = QColor(20, 40, 60, 128);
    settings.gradientAngle = 45;
    settings.padding = 37;
    settings.cornerRadius = 9;
    settings.shadowEnabled = false;
    settings.shadowBlur = 11;
    settings.shadowOffsetX = -3;
    settings.shadowOffsetY = 5;
    settings.shadowColor = QColor(9, 8, 7, 6);
    settings.borderEnabled = true;
    settings.borderWidth = 4;
    settings.borderColor = QColor(200, 100, 50, 25);
    settings.aspectRatio = BeautifyAspectRatio::Wide_16_9;
    return settings;
}

SnapTray::CaptureSessionWriteRequest sampleRequest(int second = 0)
{
    SnapTray::CaptureSessionWriteRequest request;
    request.canvasImage = makeImage(QSize(320, 180), QColor(10, 20, 30));
    request.resultImage = makeImage(QSize(120, 90), QColor(40, 50, 60));
    request.selectionRect = QRect(12, 18, 120, 90);
    request.annotationsJson = sampleAnnotations();
    request.canvasLogicalSize = QSize(320, 180);
    request.createdAt = QDateTime(QDate(2026, 1, 1), QTime(12, 0, second, 0));
    return request;
}

QByteArray fileBytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString manifestPath(const SnapTray::HistoryEntry& entry)
{
    return QDir(entry.entryDirectory).filePath(QStringLiteral("manifest.json"));
}

// Rewrites an entry's manifest the way another SnapTray version, or damage,
// would have left it.
void editManifest(const SnapTray::HistoryEntry& entry,
                  const std::function<void(QJsonObject&)>& edit)
{
    QJsonObject manifest = QJsonDocument::fromJson(fileBytes(manifestPath(entry))).object();
    edit(manifest);
    QFile file(manifestPath(entry));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(manifest).toJson());
}

QStringList entriesRootListing()
{
    return QDir(SnapTray::HistoryStore::entriesRootPath())
        .entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
}

} // namespace

class tst_HistoryStore : public QObject
{
    Q_OBJECT

private slots:
    void testWriteAndLoadCapture();
    void testIgnoresNonCaptureManifest();
    void testRetentionTrimsOldestEntries();

    // #344: per-entry Beautify state, schema version and atomic commit.
    void init();
    void cleanup();
    void testBeautifySettingsJsonRoundTrip();
    void testBeautifySettingsJsonRejectsDamage_data();
    void testBeautifySettingsJsonRejectsDamage();
    void testBeautifySnapshotRoundTrip_data();
    void testBeautifySnapshotRoundTrip();
    void testLegacyManifestStaysReplayable();
    void testNewerSchemaIsListedButNeverReplayedOrOverwritten();
    void testDamagedBeautifyBlocksReplayOnly();
    void testDocumentSourceKeepsCanvasAndAnnotations();
    void testDocumentSourceFallsBackToOwnDocument();
    void testFailedWriteLeavesNoEntry_data();
    void testFailedWriteLeavesNoEntry();
    void testInterruptedWriteIsIgnoredAndCleared();

private:
    QTemporaryDir m_historyDir;
};

void tst_HistoryStore::init()
{
    // Each test gets an empty history; the older tests set their own.
    QVERIFY(m_historyDir.isValid());
    const QString path = m_historyDir.filePath(QString::fromLatin1(QTest::currentTestFunction()) +
                                               QLatin1Char('_') +
                                               QString::fromLatin1(QTest::currentDataTag()));
    qputenv("SNAPTRAY_HISTORY_DIR", path.toLocal8Bit());
}

void tst_HistoryStore::cleanup()
{
    SnapTray::HistoryStore::setWriteFaultForTests({});
    qunsetenv("SNAPTRAY_HISTORY_DIR");
}

void tst_HistoryStore::testWriteAndLoadCapture()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    qputenv("SNAPTRAY_HISTORY_DIR", tempDir.path().toLocal8Bit());

    SnapTray::CaptureSessionWriteRequest captureRequest;
    captureRequest.canvasImage = makeImage(QSize(320, 180), QColor(10, 20, 30));
    captureRequest.resultImage = makeImage(QSize(120, 90), QColor(40, 50, 60));
    captureRequest.selectionRect = QRect(12, 18, 120, 90);
    captureRequest.annotationsJson = sampleAnnotations();
    captureRequest.devicePixelRatio = 2.0;
    captureRequest.canvasLogicalSize = QSize(160, 90);
    captureRequest.cornerRadius = 8;
    captureRequest.windowTitle = QStringLiteral("Editor — Notes");
    captureRequest.ownerApp = QStringLiteral("Test App");
    captureRequest.createdAt = QDateTime(QDate(2026, 1, 1), QTime(12, 0, 0, 0));

    auto captureEntry = SnapTray::HistoryStore::writeCaptureSession(captureRequest);
    QVERIFY(captureEntry.has_value());
    QVERIFY(QFileInfo::exists(captureEntry->canvasPath));
    QVERIFY(QFileInfo::exists(captureEntry->resultPath));
    QVERIFY(QFileInfo::exists(captureEntry->annotationsPath));

    const QList<SnapTray::HistoryEntry> entries = SnapTray::HistoryStore::loadEntries();
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().id, captureEntry->id);
    QVERIFY(entries.first().replayAvailable);

    const auto loadedCapture = SnapTray::HistoryStore::loadEntry(captureEntry->id);
    QVERIFY(loadedCapture.has_value());
    QCOMPARE(loadedCapture->selectionRect, captureRequest.selectionRect);
    QCOMPARE(loadedCapture->canvasLogicalSize, captureRequest.canvasLogicalSize);
    QCOMPARE(loadedCapture->cornerRadius, captureRequest.cornerRadius);
    QCOMPARE(loadedCapture->devicePixelRatio, captureRequest.devicePixelRatio);
    QCOMPARE(loadedCapture->windowTitle, captureRequest.windowTitle);
    QCOMPARE(loadedCapture->ownerApp, captureRequest.ownerApp);

    // Existing manifests without the optional window context still load.
    QFile manifestFile(QDir(captureEntry->entryDirectory).filePath(QStringLiteral("manifest.json")));
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    QJsonObject legacyManifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    manifestFile.close();
    legacyManifest.remove(QStringLiteral("windowTitle"));
    legacyManifest.remove(QStringLiteral("ownerApp"));
    QVERIFY(manifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    manifestFile.write(QJsonDocument(legacyManifest).toJson());
    manifestFile.close();
    const auto legacyCapture = SnapTray::HistoryStore::loadEntry(captureEntry->id);
    QVERIFY(legacyCapture);
    QVERIFY(legacyCapture->windowTitle.isEmpty());
    QVERIFY(legacyCapture->ownerApp.isEmpty());

    QVERIFY(SnapTray::HistoryStore::deleteEntry(*captureEntry));
    QVERIFY(!QFileInfo::exists(captureEntry->entryDirectory));

    qunsetenv("SNAPTRAY_HISTORY_DIR");
}

void tst_HistoryStore::testIgnoresNonCaptureManifest()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    qputenv("SNAPTRAY_HISTORY_DIR", tempDir.path().toLocal8Bit());

    const QString entriesPath = SnapTray::HistoryStore::entriesRootPath();
    QVERIFY(QDir().mkpath(entriesPath));
    const QString entryPath = QDir(entriesPath).filePath(QStringLiteral("pin_legacy"));
    QVERIFY(QDir().mkpath(entryPath));

    const QJsonObject manifest{
        {QStringLiteral("id"), QStringLiteral("pin_legacy")},
        {QStringLiteral("kind"), QStringLiteral("pin_snapshot")},
        {QStringLiteral("createdAt"), QStringLiteral("2026-01-01T12:00:00.000")},
        {QStringLiteral("previewPath"), QStringLiteral("result.png")},
        {QStringLiteral("resultPath"), QStringLiteral("result.png")},
        {QStringLiteral("replayAvailable"), false},
    };
    QFile file(QDir(entryPath).filePath(QStringLiteral("manifest.json")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    file.close();

    QCOMPARE(SnapTray::HistoryStore::loadEntries().size(), 0);
    QVERIFY(!SnapTray::HistoryStore::loadEntry(QStringLiteral("pin_legacy")).has_value());

    qunsetenv("SNAPTRAY_HISTORY_DIR");
}

void tst_HistoryStore::testRetentionTrimsOldestEntries()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    qputenv("SNAPTRAY_HISTORY_DIR", tempDir.path().toLocal8Bit());

    for (int i = 0; i < 3; ++i) {
        SnapTray::CaptureSessionWriteRequest request;
        request.canvasImage = makeImage(QSize(320, 180), QColor(10 + i, 20 + i, 30 + i));
        request.resultImage = makeImage(QSize(80 + i, 60 + i), QColor(40 + i, 50 + i, 60 + i));
        request.selectionRect = QRect(0, 0, 80 + i, 60 + i);
        request.annotationsJson = QByteArrayLiteral("{\"items\":[]}");
        request.maxEntries = 2;
        request.createdAt = QDateTime(QDate(2026, 1, 1), QTime(12, 0, i, 0));
        QVERIFY(SnapTray::HistoryStore::writeCaptureSession(request).has_value());
    }

    const QList<SnapTray::HistoryEntry> entries = SnapTray::HistoryStore::loadEntries();
    QCOMPARE(entries.size(), 2);
    QVERIFY(std::all_of(entries.begin(), entries.end(), [](const SnapTray::HistoryEntry& entry) {
        return entry.createdAt >= QDateTime(QDate(2026, 1, 1), QTime(12, 0, 1, 0));
    }));

    qunsetenv("SNAPTRAY_HISTORY_DIR");
}

void tst_HistoryStore::testBeautifySettingsJsonRoundTrip()
{
    const BeautifySettings settings = sampleBeautify();
    const QJsonObject json = SnapTray::beautifySettingsToJson(settings);
    const auto restored = SnapTray::beautifySettingsFromJson(json);
    QVERIFY(restored);
    QCOMPARE(SnapTray::beautifySettingsToJson(*restored), json);
    QCOMPARE(restored->padding, settings.padding);
    QCOMPARE(restored->shadowOffsetX, settings.shadowOffsetX);
    QCOMPARE(restored->borderColor, settings.borderColor);
    QCOMPARE(restored->backgroundType, settings.backgroundType);
    QCOMPARE(restored->aspectRatio, settings.aspectRatio);

    // The defaults survive too, so "default look" is a stored fact, not a gap.
    const auto defaults = SnapTray::beautifySettingsFromJson(
        SnapTray::beautifySettingsToJson(BeautifySettings{}));
    QVERIFY(defaults);
    QCOMPARE(defaults->padding, BeautifySettings{}.padding);
}

void tst_HistoryStore::testBeautifySettingsJsonRejectsDamage_data()
{
    QTest::addColumn<QString>("key");
    QTest::addColumn<QJsonValue>("value");

    QTest::newRow("missing key") << QStringLiteral("padding") << QJsonValue(QJsonValue::Undefined);
    QTest::newRow("wrong type") << QStringLiteral("padding") << QJsonValue(QStringLiteral("64"));
    QTest::newRow("negative size") << QStringLiteral("padding") << QJsonValue(-1);
    QTest::newRow("absurd size") << QStringLiteral("padding") << QJsonValue(1000000);
    QTest::newRow("fractional size") << QStringLiteral("cornerRadius") << QJsonValue(1.5);
    QTest::newRow("unknown background") << QStringLiteral("backgroundType") << QJsonValue(7);
    QTest::newRow("unknown aspect") << QStringLiteral("aspectRatio") << QJsonValue(42);
    QTest::newRow("bad color") << QStringLiteral("shadowColor") << QJsonValue(QStringLiteral("red"));
    QTest::newRow("bool as number") << QStringLiteral("shadowEnabled") << QJsonValue(1);
}

void tst_HistoryStore::testBeautifySettingsJsonRejectsDamage()
{
    QFETCH(QString, key);
    QFETCH(QJsonValue, value);

    QJsonObject json = SnapTray::beautifySettingsToJson(sampleBeautify());
    if (value.isUndefined()) {
        json.remove(key);
    } else {
        json.insert(key, value);
    }
    QVERIFY(!SnapTray::beautifySettingsFromJson(json));
}

void tst_HistoryStore::testBeautifySnapshotRoundTrip_data()
{
    QTest::addColumn<qreal>("devicePixelRatio");
    QTest::addColumn<bool>("multiRegion");
    QTest::addColumn<bool>("transparent");

    QTest::newRow("1x") << qreal(1.0) << false << false;
    QTest::newRow("2x") << qreal(2.0) << false << false;
    QTest::newRow("1.5x") << qreal(1.5) << false << false;
    QTest::newRow("1.25x multi-region") << qreal(1.25) << true << false;
    QTest::newRow("2x transparent") << qreal(2.0) << false << true;
}

void tst_HistoryStore::testBeautifySnapshotRoundTrip()
{
    QFETCH(qreal, devicePixelRatio);
    QFETCH(bool, multiRegion);
    QFETCH(bool, transparent);

    SnapTray::CaptureSessionWriteRequest request = sampleRequest();
    request.devicePixelRatio = devicePixelRatio;
    request.beautify = sampleBeautify();
    // An odd pixel size: at 1.5x and 1.25x it is not a whole logical size.
    request.resultImage = makeImage(QSize(151, 93), transparent ? QColor(40, 50, 60, 0)
                                                               : QColor(40, 50, 60));
    if (transparent) {
        request.resultImage.setPixelColor(3, 4, QColor(255, 0, 0, 128));
    }
    if (multiRegion) {
        request.captureRegions = {
            MultiRegionManager::Region{QRect(5, 6, 70, 40), QColor(Qt::green), 1, false},
            MultiRegionManager::Region{QRect(90, 60, 50, 30), QColor(Qt::blue), 2, true},
        };
    }

    const auto written = SnapTray::HistoryStore::writeCaptureSession(request);
    QVERIFY(written);

    const auto entry = SnapTray::HistoryStore::loadEntry(written->id);
    QVERIFY(entry);
    QCOMPARE(entry->schemaVersion, SnapTray::HistoryStore::kSchemaVersion);
    QVERIFY(!entry->isFromNewerVersion());
    QVERIFY(entry->replayAvailable);
    QVERIFY(entry->beautify);
    QCOMPARE(SnapTray::beautifySettingsToJson(*entry->beautify),
             SnapTray::beautifySettingsToJson(sampleBeautify()));
    QCOMPARE(entry->devicePixelRatio, devicePixelRatio);
    QCOMPARE(entry->resultSize, request.resultImage.size());
    QCOMPARE(entry->captureRegions.size(), request.captureRegions.size());
    if (multiRegion) {
        QCOMPARE(entry->captureRegions.at(1).rect, request.captureRegions.at(1).rect);
        QCOMPARE(entry->captureRegions.at(1).isActive, true);
    }

    // The finished picture comes back pixel for pixel, alpha included.
    const QImage storedResult = QImage(entry->resultPath)
        .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(storedResult, request.resultImage);
    QCOMPARE(QImage(entry->canvasPath).convertToFormat(QImage::Format_ARGB32_Premultiplied),
             request.canvasImage);
    QCOMPARE(fileBytes(entry->annotationsPath), request.annotationsJson);

    // A plain capture carries no Beautify state.
    const auto plain = SnapTray::HistoryStore::writeCaptureSession(sampleRequest(1));
    QVERIFY(plain);
    QVERIFY(!plain->beautify);
    QCOMPARE(plain->schemaVersion, SnapTray::HistoryStore::kSchemaVersion);
}

void tst_HistoryStore::testLegacyManifestStaysReplayable()
{
    const auto written = SnapTray::HistoryStore::writeCaptureSession(sampleRequest());
    QVERIFY(written);
    // What a version before #344 wrote: no schemaVersion, no beautify.
    editManifest(*written, [](QJsonObject& manifest) {
        manifest.remove(QStringLiteral("schemaVersion"));
        manifest.remove(QStringLiteral("beautify"));
    });

    const auto legacy = SnapTray::HistoryStore::loadEntry(written->id);
    QVERIFY(legacy);
    QCOMPARE(legacy->schemaVersion, 1);
    QVERIFY(legacy->replayAvailable);
    QVERIFY(!legacy->beautify);
    QCOMPARE(legacy->selectionRect, QRect(12, 18, 120, 90));
    QCOMPARE(SnapTray::HistoryStore::loadEntries().size(), 1);
}

void tst_HistoryStore::testNewerSchemaIsListedButNeverReplayedOrOverwritten()
{
    const auto written = SnapTray::HistoryStore::writeCaptureSession(sampleRequest());
    QVERIFY(written);
    editManifest(*written, [](QJsonObject& manifest) {
        manifest.insert(QStringLiteral("schemaVersion"), SnapTray::HistoryStore::kSchemaVersion + 1);
        manifest.insert(QStringLiteral("beautify"), QJsonObject{{QStringLiteral("future"), true}});
    });
    const QByteArray futureManifest = fileBytes(manifestPath(*written));

    const auto future = SnapTray::HistoryStore::loadEntry(written->id);
    QVERIFY(future);
    QVERIFY(future->isFromNewerVersion());
    QVERIFY(!future->replayAvailable);
    QVERIFY(!future->beautify);
    // Still listed, and its finished picture is still there to pin or copy.
    QCOMPARE(SnapTray::HistoryStore::loadEntries().size(), 1);
    QVERIFY(!QImage(future->resultPath).isNull());

    // Writing under its id is refused rather than replacing it.
    SnapTray::CaptureSessionWriteRequest clash = sampleRequest(1);
    clash.id = written->id;
    QVERIFY(!SnapTray::HistoryStore::writeCaptureSession(clash));

    // Its document is not taken over by an entry this version writes.
    SnapTray::CaptureSessionWriteRequest derived = sampleRequest(2);
    derived.canvasImage = makeImage(QSize(64, 48), QColor(200, 0, 0));
    derived.selectionRect = QRect(0, 0, 64, 48);
    derived.documentSourceEntryId = written->id;
    derived.beautify = sampleBeautify();
    const auto derivedEntry = SnapTray::HistoryStore::writeCaptureSession(derived);
    QVERIFY(derivedEntry);
    QCOMPARE(derivedEntry->selectionRect, derived.selectionRect);
    QCOMPARE(QImage(derivedEntry->canvasPath).size(), QSize(64, 48));

    QCOMPARE(fileBytes(manifestPath(*written)), futureManifest);

    // A version that is not a whole number is unknown too, not "legacy".
    editManifest(*written, [](QJsonObject& manifest) {
        manifest.insert(QStringLiteral("schemaVersion"), QStringLiteral("3"));
    });
    const auto odd = SnapTray::HistoryStore::loadEntry(written->id);
    QVERIFY(odd);
    QVERIFY(odd->isFromNewerVersion());
    QVERIFY(!odd->replayAvailable);
}

void tst_HistoryStore::testDamagedBeautifyBlocksReplayOnly()
{
    SnapTray::CaptureSessionWriteRequest request = sampleRequest();
    request.beautify = sampleBeautify();
    const auto written = SnapTray::HistoryStore::writeCaptureSession(request);
    QVERIFY(written);
    editManifest(*written, [](QJsonObject& manifest) {
        QJsonObject beautify = manifest.value(QStringLiteral("beautify")).toObject();
        beautify.remove(QStringLiteral("padding"));
        manifest.insert(QStringLiteral("beautify"), beautify);
    });

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("unreadable beautify settings")));
    const auto damaged = SnapTray::HistoryStore::loadEntry(written->id);
    QVERIFY(damaged);
    // Not replayed with a guessed look; the finished picture is untouched.
    QVERIFY(!damaged->replayAvailable);
    QVERIFY(!damaged->beautify);
    QVERIFY(!damaged->isFromNewerVersion());
    QVERIFY(!QImage(damaged->resultPath).isNull());
}

void tst_HistoryStore::testDocumentSourceKeepsCanvasAndAnnotations()
{
    SnapTray::CaptureSessionWriteRequest capture = sampleRequest();
    capture.devicePixelRatio = 1.5;
    capture.cornerRadius = 6;
    capture.windowTitle = QStringLiteral("Source window");
    capture.captureRegions = {
        MultiRegionManager::Region{QRect(5, 6, 70, 40), QColor(Qt::green), 1, true},
    };
    const auto source = SnapTray::HistoryStore::writeCaptureSession(capture);
    QVERIFY(source);
    const QByteArray sourceManifest = fileBytes(manifestPath(*source));

    SnapTray::CaptureSessionWriteRequest beautified = sampleRequest(1);
    beautified.documentSourceEntryId = source->id;
    beautified.beautify = sampleBeautify();
    beautified.resultImage = makeImage(QSize(200, 160), QColor(1, 2, 3));
    // The fallback document must not be what ends up stored.
    beautified.canvasImage = makeImage(QSize(64, 48), QColor(200, 0, 0));
    beautified.selectionRect = QRect(0, 0, 64, 48);
    beautified.annotationsJson = QByteArrayLiteral("{\"items\":[]}");
    beautified.devicePixelRatio = 1.0;

    const auto entry = SnapTray::HistoryStore::writeCaptureSession(beautified);
    QVERIFY(entry);
    QVERIFY(entry->id != source->id);
    QVERIFY(entry->beautify);
    QVERIFY(entry->replayAvailable);
    // Same original picture and the same, still editable, annotations.
    QCOMPARE(fileBytes(entry->canvasPath), fileBytes(source->canvasPath));
    QCOMPARE(fileBytes(entry->annotationsPath), capture.annotationsJson);
    QCOMPARE(entry->selectionRect, capture.selectionRect);
    QCOMPARE(entry->captureRegions.size(), 1);
    QCOMPARE(entry->devicePixelRatio, 1.5);
    QCOMPARE(entry->cornerRadius, 6);
    QCOMPARE(entry->windowTitle, capture.windowTitle);
    QCOMPARE(entry->resultSize, QSize(200, 160));

    // The entry it came from is a separate work and did not change.
    QCOMPARE(fileBytes(manifestPath(*source)), sourceManifest);
    const auto sourceAgain = SnapTray::HistoryStore::loadEntry(source->id);
    QVERIFY(sourceAgain);
    QVERIFY(!sourceAgain->beautify);
    QCOMPARE(QImage(sourceAgain->resultPath).size(), capture.resultImage.size());

    // Deleting the source leaves the new entry complete.
    QVERIFY(SnapTray::HistoryStore::deleteEntry(*source));
    QVERIFY(!QImage(entry->canvasPath).isNull());
}

void tst_HistoryStore::testDocumentSourceFallsBackToOwnDocument()
{
    SnapTray::CaptureSessionWriteRequest request = sampleRequest();
    request.documentSourceEntryId = QStringLiteral("capture_trimmed_long_ago");
    request.beautify = sampleBeautify();

    const auto entry = SnapTray::HistoryStore::writeCaptureSession(request);
    QVERIFY(entry);
    QVERIFY(entry->beautify);
    QCOMPARE(entry->selectionRect, request.selectionRect);
    QCOMPARE(fileBytes(entry->annotationsPath), request.annotationsJson);
    QCOMPARE(QImage(entry->canvasPath).size(), request.canvasImage.size());
}

void tst_HistoryStore::testFailedWriteLeavesNoEntry_data()
{
    QTest::addColumn<QString>("failingStep");
    QTest::addColumn<bool>("fromDocumentSource");

    for (const char* step : {"canvas.png", "annotations.json", "result.png", "manifest.json", "commit"}) {
        QTest::addRow("%s", step) << QString::fromLatin1(step) << false;
    }
    // With a document source the new result is written next to a copied
    // document: the case that must never be left half done.
    QTest::newRow("result.png after copied document") << QStringLiteral("result.png") << true;
    QTest::newRow("commit after copied document") << QStringLiteral("commit") << true;
}

void tst_HistoryStore::testFailedWriteLeavesNoEntry()
{
    QFETCH(QString, failingStep);
    QFETCH(bool, fromDocumentSource);

    const auto existing = SnapTray::HistoryStore::writeCaptureSession(sampleRequest());
    QVERIFY(existing);
    const QStringList listingBefore = entriesRootListing();
    const QByteArray manifestBefore = fileBytes(manifestPath(*existing));
    const QByteArray resultBefore = fileBytes(existing->resultPath);

    SnapTray::CaptureSessionWriteRequest request = sampleRequest(1);
    request.beautify = sampleBeautify();
    request.resultImage = makeImage(QSize(300, 200), QColor(9, 9, 9));
    if (fromDocumentSource) {
        request.documentSourceEntryId = existing->id;
    }

    QStringList steps;
    SnapTray::HistoryStore::setWriteFaultForTests([&](const QString& step) {
        steps.append(step);
        return step == failingStep;
    });
    QVERIFY(!SnapTray::HistoryStore::writeCaptureSession(request));
    QVERIFY(steps.contains(failingStep));
    SnapTray::HistoryStore::setWriteFaultForTests({});

    // Nothing new is visible and nothing is left behind, staging included.
    QCOMPARE(entriesRootListing(), listingBefore);
    QCOMPARE(SnapTray::HistoryStore::loadEntries().size(), 1);
    // The existing work still has its own picture and its own document.
    QCOMPARE(fileBytes(manifestPath(*existing)), manifestBefore);
    QCOMPARE(fileBytes(existing->resultPath), resultBefore);

    // The same request goes through once the fault is gone.
    const auto retried = SnapTray::HistoryStore::writeCaptureSession(request);
    QVERIFY(retried);
    QCOMPARE(retried->resultSize, QSize(300, 200));
    QVERIFY(retried->beautify);
}

void tst_HistoryStore::testInterruptedWriteIsIgnoredAndCleared()
{
    const auto complete = SnapTray::HistoryStore::writeCaptureSession(sampleRequest());
    QVERIFY(complete);

    // A write cut off just before its commit: every file is there, manifest
    // included, but under the staging name.
    const QDir root(SnapTray::HistoryStore::entriesRootPath());
    const QString stagingName = QStringLiteral(".staging-capture_interrupted");
    QVERIFY(root.mkpath(stagingName));
    const QDir staging(root.filePath(stagingName));
    for (const QString& name : QDir(complete->entryDirectory).entryList(QDir::Files)) {
        QVERIFY(QFile::copy(QDir(complete->entryDirectory).filePath(name), staging.filePath(name)));
    }
    QVERIFY(QFileInfo::exists(staging.filePath(QStringLiteral("manifest.json"))));

    QCOMPARE(SnapTray::HistoryStore::loadEntries().size(), 1);
    QVERIFY(!SnapTray::HistoryStore::loadEntry(stagingName));

    // The next write clears it.
    QVERIFY(SnapTray::HistoryStore::writeCaptureSession(sampleRequest(1)));
    QVERIFY(!root.exists(stagingName));
    QCOMPARE(SnapTray::HistoryStore::loadEntries().size(), 2);
}

QTEST_MAIN(tst_HistoryStore)
#include "tst_HistoryStore.moc"
