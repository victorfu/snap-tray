#include <QtTest/QtTest>

#include "history/AnnotationSerializer.h"
#include "history/HistoryStore.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/ShapeAnnotation.h"

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
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

} // namespace

class tst_HistoryStore : public QObject
{
    Q_OBJECT

private slots:
    void testWriteAndLoadCapture();
    void testIgnoresNonCaptureManifest();
    void testRetentionTrimsOldestEntries();
};

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

QTEST_MAIN(tst_HistoryStore)
#include "tst_HistoryStore.moc"
