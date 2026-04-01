#include <QtTest/QtTest>

#include "history/HistoryStore.h"
#include "qml/HistoryModel.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

namespace {

QImage makeImage(const QSize& size, int seed)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < size.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            line[x] = qRgba((x + seed) % 256,
                            (y * 3 + seed) % 256,
                            (x + y + seed * 7) % 256,
                            255);
        }
    }
    return image;
}

SnapTray::HistoryEntry writeEntry(const QSize& canvasSize,
                                  const QSize& resultSize,
                                  const QDateTime& createdAt,
                                  int seed)
{
    SnapTray::CaptureSessionWriteRequest request;
    request.canvasImage = makeImage(canvasSize, seed);
    request.resultImage = makeImage(resultSize, seed + 11);
    request.selectionRect = QRect(0, 0, resultSize.width(), resultSize.height());
    request.annotationsJson = QByteArrayLiteral("{\"items\":[]}");
    request.devicePixelRatio = 2.0;
    request.canvasLogicalSize = resultSize;
    request.createdAt = createdAt;

    auto entry = SnapTray::HistoryStore::writeCaptureSession(request);
    if (!entry.has_value()) {
        qFatal("Failed to write history entry");
    }
    return entry.value();
}

void appendBytes(const QString& filePath, qint64 byteCount)
{
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::Append));
    QVERIFY(file.write(QByteArray(static_cast<int>(byteCount), '@')) == byteCount);
}

void setReplayAvailable(const SnapTray::HistoryEntry& entry, bool replayAvailable)
{
    QFile file(entry.entryDirectory + QStringLiteral("/manifest.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));

    const QJsonObject manifest = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    QJsonObject updated = manifest;
    updated.insert(QStringLiteral("replayAvailable"), replayAvailable);

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(updated).toJson(QJsonDocument::Indented));
    file.close();
}

} // namespace

class tst_HistoryModel : public QObject
{
    Q_OBJECT

private slots:
    void testRefreshAndRoles();
    void testSearchFiltersAndCounts();
    void testSortingModes();
};

void tst_HistoryModel::testRefreshAndRoles()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    qputenv("SNAPTRAY_HISTORY_DIR", tempDir.path().toLocal8Bit());

    const QDateTime now = QDateTime::currentDateTime();
    const auto newestEntry = writeEntry(QSize(640, 360), QSize(320, 180), now, 1);
    const auto olderEntry = writeEntry(QSize(400, 240), QSize(200, 100), now.addDays(-1), 2);

    SnapTray::HistoryModel model;
    QCOMPARE(model.totalCount(), 2);
    QCOMPARE(model.rowCount(), 2);

    const QModelIndex first = model.index(0, 0);
    QVERIFY(first.isValid());
    QCOMPARE(model.data(first, SnapTray::HistoryModel::IdRole).toString(), newestEntry.id);
    QCOMPARE(model.data(first, SnapTray::HistoryModel::CanEditRole).toBool(), true);
    QCOMPARE(model.data(first, SnapTray::HistoryModel::ReplayAvailableRole).toBool(), true);
    QCOMPARE(model.data(first, SnapTray::HistoryModel::ImageSizeRole).toSize(), QSize(320, 180));
    QCOMPARE(model.data(first, SnapTray::HistoryModel::SizeTextRole).toString(), QStringLiteral("320 x 180"));
    QCOMPARE(model.data(first, SnapTray::HistoryModel::ResolutionTextRole).toString(), QStringLiteral("320 x 180"));
    QCOMPARE(model.data(first, SnapTray::HistoryModel::DisplayTitleRole).toString(),
             QStringLiteral("Screenshot %1").arg(now.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
    QCOMPARE(model.data(first, SnapTray::HistoryModel::RelativeDateTextRole).toString(),
             QStringLiteral("Today"));
    QVERIFY(model.data(first, SnapTray::HistoryModel::FileSizeBytesRole).toLongLong() > 0);
    QVERIFY(!model.data(first, SnapTray::HistoryModel::FileSizeTextRole).toString().isEmpty());
    QVERIFY(model.data(first, SnapTray::HistoryModel::TooltipTextRole).toString().contains(QStringLiteral("Time:")));
    QVERIFY(model.data(first, SnapTray::HistoryModel::TooltipTextRole).toString().contains(QStringLiteral("Resolution:")));
    QVERIFY(model.data(first, SnapTray::HistoryModel::TooltipTextRole).toString().contains(QStringLiteral("File size:")));

    const QModelIndex second = model.index(1, 0);
    QVERIFY(second.isValid());
    QCOMPARE(model.data(second, SnapTray::HistoryModel::IdRole).toString(), olderEntry.id);
    QCOMPARE(model.data(second, SnapTray::HistoryModel::RelativeDateTextRole).toString(),
             QStringLiteral("Yesterday"));

    QSignalSpy countSpy(&model, &SnapTray::HistoryModel::countChanged);
    QVERIFY(countSpy.isValid());
    model.refresh();
    QVERIFY(countSpy.count() >= 1);
    QCOMPARE(model.totalCount(), 2);
    QCOMPARE(model.rowCount(), 2);

    qunsetenv("SNAPTRAY_HISTORY_DIR");
}

void tst_HistoryModel::testSearchFiltersAndCounts()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    qputenv("SNAPTRAY_HISTORY_DIR", tempDir.path().toLocal8Bit());

    const QDateTime now = QDateTime::currentDateTime();
    const auto todayEntry = writeEntry(QSize(1280, 720), QSize(640, 360), now, 3);
    const auto recentEntry = writeEntry(QSize(800, 450), QSize(400, 225), now.addDays(-2), 4);
    const auto oldEntry = writeEntry(QSize(1920, 1080), QSize(960, 540), now.addDays(-10), 5);
    Q_UNUSED(todayEntry);

    appendBytes(oldEntry.resultPath, 2 * 1024 * 1024 + 512 * 1024);
    setReplayAvailable(recentEntry, false);

    SnapTray::HistoryModel model;
    model.refresh();

    const int revisionBeforeUpdate = model.filterCountsRevision();
    const auto newestEntry = writeEntry(QSize(1024, 640), QSize(512, 320), now.addSecs(30), 9);
    Q_UNUSED(newestEntry);
    model.refresh();

    QVERIFY(model.filterCountsRevision() > revisionBeforeUpdate);
    QCOMPARE(model.totalCount(), 4);
    QCOMPARE(model.countForFilter(SnapTray::HistoryModel::AllScreenshots), 4);
    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.countForFilter(SnapTray::HistoryModel::Last7Days), 3);
    QCOMPARE(model.countForFilter(SnapTray::HistoryModel::LargeFiles), 1);
    QCOMPARE(model.countForFilter(SnapTray::HistoryModel::Replayable), 3);

    model.setActiveFilter(SnapTray::HistoryModel::Last7Days);
    QCOMPARE(model.rowCount(), 3);

    model.setActiveFilter(SnapTray::HistoryModel::LargeFiles);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), SnapTray::HistoryModel::IdRole).toString(), oldEntry.id);

    qunsetenv("SNAPTRAY_HISTORY_DIR");
}

void tst_HistoryModel::testSortingModes()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    qputenv("SNAPTRAY_HISTORY_DIR", tempDir.path().toLocal8Bit());

    const QDateTime now = QDateTime::currentDateTime();
    const auto oldestEntry = writeEntry(QSize(500, 280), QSize(250, 140), now.addDays(-2), 6);
    const auto middleEntry = writeEntry(QSize(900, 500), QSize(450, 250), now.addDays(-1), 7);
    const auto newestEntry = writeEntry(QSize(1300, 720), QSize(650, 360), now, 8);

    appendBytes(oldestEntry.resultPath, 128 * 1024);
    appendBytes(middleEntry.resultPath, 256 * 1024);
    appendBytes(newestEntry.resultPath, 1024 * 1024);

    SnapTray::HistoryModel model;
    model.refresh();

    QCOMPARE(model.data(model.index(0, 0), SnapTray::HistoryModel::IdRole).toString(), newestEntry.id);

    model.setSortOrder(SnapTray::HistoryModel::OldestFirst);
    QCOMPARE(model.data(model.index(0, 0), SnapTray::HistoryModel::IdRole).toString(), oldestEntry.id);

    model.setSortOrder(SnapTray::HistoryModel::LargestFirst);
    QCOMPARE(model.data(model.index(0, 0), SnapTray::HistoryModel::IdRole).toString(), newestEntry.id);
    QCOMPARE(model.indexOfId(middleEntry.id), 1);
    QCOMPARE(model.idAt(2), oldestEntry.id);

    qunsetenv("SNAPTRAY_HISTORY_DIR");
}

QTEST_MAIN(tst_HistoryModel)
#include "tst_HistoryModel.moc"
