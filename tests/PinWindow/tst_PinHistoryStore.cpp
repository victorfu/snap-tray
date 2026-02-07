#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include "pinwindow/PinHistoryStore.h"

class TestPinHistoryStore : public QObject
{
    Q_OBJECT

private:
    static bool createImageFile(const QString& path, const QSize& size, const QColor& color)
    {
        QImage image(size, QImage::Format_ARGB32);
        image.fill(color);
        return image.save(path);
    }

private slots:
    void testLoadEntriesFiltersAndSortsNewestFirst()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const QString folder = tempDir.path();
        const QString oldFile = QDir(folder).filePath("cache_20260101_120000_000.png");
        const QString newFile = QDir(folder).filePath("cache_20260101_120001_000.png");
        const QString newDprFile = QDir(folder).filePath("cache_20260101_120001_000.snpd");
        const QString ignoredFile = QDir(folder).filePath("random.png");

        QVERIFY(createImageFile(oldFile, QSize(200, 120), Qt::red));
        QVERIFY(createImageFile(newFile, QSize(320, 180), Qt::green));
        QVERIFY(createImageFile(ignoredFile, QSize(64, 64), Qt::blue));
        QFile dprFile(newDprFile);
        QVERIFY(dprFile.open(QIODevice::WriteOnly | QIODevice::Text));
        dprFile.write("2.0");
        dprFile.close();

        const QList<PinHistoryEntry> entries = PinHistoryStore::loadEntriesFromFolder(folder);
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries[0].imagePath, newFile);
        QCOMPARE(entries[1].imagePath, oldFile);
        QCOMPARE(entries[0].imageSize, QSize(320, 180));
        QCOMPARE(entries[1].imageSize, QSize(200, 120));
        QCOMPARE(entries[0].devicePixelRatio, 2.0);
        QCOMPARE(entries[1].devicePixelRatio, 1.0);
        QVERIFY(entries[0].capturedAt > entries[1].capturedAt);
    }

    void testDeleteEntryRemovesImageAndMetadata()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const QString folder = tempDir.path();
        const QString imageFile = QDir(folder).filePath("cache_20260101_120000_000.png");
        const QString metadataFile = QDir(folder).filePath("cache_20260101_120000_000.snpr");
        const QString dprFile = QDir(folder).filePath("cache_20260101_120000_000.snpd");

        QVERIFY(createImageFile(imageFile, QSize(160, 90), Qt::yellow));

        QFile metadata(metadataFile);
        QVERIFY(metadata.open(QIODevice::WriteOnly));
        metadata.write("region-data");
        metadata.close();
        QFile dpr(dprFile);
        QVERIFY(dpr.open(QIODevice::WriteOnly | QIODevice::Text));
        dpr.write("2.0");
        dpr.close();

        PinHistoryEntry entry;
        entry.imagePath = imageFile;
        entry.metadataPath = metadataFile;
        entry.dprMetadataPath = dprFile;

        QVERIFY(PinHistoryStore::deleteEntry(entry));
        QVERIFY(!QFile::exists(imageFile));
        QVERIFY(!QFile::exists(metadataFile));
        QVERIFY(!QFile::exists(dprFile));
    }

    void testSidecarPathsRemainInFolderWhenFolderContainsPngSubstring()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        QDir rootDir(tempDir.path());
        QVERIFY(rootDir.mkpath("history.png.dir"));
        const QString folder = rootDir.filePath("history.png.dir");

        const QString imageFile = QDir(folder).filePath("cache_20260101_120001_000.png");
        const QString expectedMetadata = QDir(folder).filePath("cache_20260101_120001_000.snpr");
        const QString expectedDpr = QDir(folder).filePath("cache_20260101_120001_000.snpd");
        QVERIFY(createImageFile(imageFile, QSize(100, 60), Qt::cyan));

        QFile metadata(expectedMetadata);
        QVERIFY(metadata.open(QIODevice::WriteOnly));
        metadata.write("region-data");
        metadata.close();

        QFile dpr(expectedDpr);
        QVERIFY(dpr.open(QIODevice::WriteOnly | QIODevice::Text));
        dpr.write("1.5");
        dpr.close();

        QCOMPARE(PinHistoryStore::regionMetadataPathForImage(imageFile), expectedMetadata);
        QCOMPARE(PinHistoryStore::dprMetadataPathForImage(imageFile), expectedDpr);

        const QList<PinHistoryEntry> entries = PinHistoryStore::loadEntriesFromFolder(folder);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries[0].metadataPath, expectedMetadata);
        QCOMPARE(entries[0].dprMetadataPath, expectedDpr);
        QCOMPARE(entries[0].devicePixelRatio, 1.5);
    }
};

QTEST_MAIN(TestPinHistoryStore)
#include "tst_PinHistoryStore.moc"
