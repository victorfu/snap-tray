#include <QtTest/QtTest>

#include "qml/PinHistoryModel.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>

namespace {

bool createImageFile(const QString& path, const QSize& size, const QColor& color)
{
    QImage image(size, QImage::Format_ARGB32);
    image.fill(color);
    return image.save(path);
}

} // namespace

class tst_PinHistoryModel : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testRefresh_LoadsNewestFirstWithExpectedRoles();
    void testRefresh_AfterRemovalUpdatesCount();

private:
    QTemporaryDir m_tempDir;
};

void tst_PinHistoryModel::init()
{
    QVERIFY(m_tempDir.isValid());
    qputenv("SNAPTRAY_PIN_HISTORY_DIR", m_tempDir.path().toLocal8Bit());
}

void tst_PinHistoryModel::cleanup()
{
    qunsetenv("SNAPTRAY_PIN_HISTORY_DIR");
    QDir dir(m_tempDir.path());
    const QFileInfoList entries = dir.entryInfoList(QDir::Files);
    for (const QFileInfo& entry : entries) {
        QFile::remove(entry.filePath());
    }
}

void tst_PinHistoryModel::testRefresh_LoadsNewestFirstWithExpectedRoles()
{
    const QString folder = m_tempDir.path();
    const QString oldFile = QDir(folder).filePath(QStringLiteral("cache_20260101_120000_000.png"));
    const QString newFile = QDir(folder).filePath(QStringLiteral("cache_20260101_120001_000.png"));
    const QString newDprFile = QDir(folder).filePath(QStringLiteral("cache_20260101_120001_000.snpd"));

    QVERIFY(createImageFile(oldFile, QSize(200, 120), Qt::red));
    QVERIFY(createImageFile(newFile, QSize(320, 180), Qt::green));

    QFile dprFile(newDprFile);
    QVERIFY(dprFile.open(QIODevice::WriteOnly | QIODevice::Text));
    dprFile.write("2.0");
    dprFile.close();

    SnapTray::PinHistoryModel model;

    QCOMPARE(model.rowCount(), 2);

    const QModelIndex first = model.index(0, 0);
    QVERIFY(first.isValid());
    QCOMPARE(model.data(first, SnapTray::PinHistoryModel::ImagePathRole).toString(), newFile);
    QCOMPARE(model.data(first, SnapTray::PinHistoryModel::ThumbnailUrlRole).toUrl(), QUrl::fromLocalFile(newFile));
    QCOMPARE(model.data(first, SnapTray::PinHistoryModel::MetadataPathRole).toString(),
             QDir(folder).filePath(QStringLiteral("cache_20260101_120001_000.snpr")));
    QCOMPARE(model.data(first, SnapTray::PinHistoryModel::DprMetadataPathRole).toString(), newDprFile);
    QCOMPARE(model.data(first, SnapTray::PinHistoryModel::ImageSizeRole).toSize(), QSize(320, 180));
    QCOMPARE(model.data(first, SnapTray::PinHistoryModel::SizeTextRole).toString(), QStringLiteral("320 x 180"));
    QCOMPARE(model.data(first, SnapTray::PinHistoryModel::DevicePixelRatioRole).toDouble(), 2.0);
    QVERIFY(model.data(first, SnapTray::PinHistoryModel::TooltipTextRole).toString().contains(QStringLiteral("320 x 180")));

    const QModelIndex second = model.index(1, 0);
    QVERIFY(second.isValid());
    QCOMPARE(model.data(second, SnapTray::PinHistoryModel::ImagePathRole).toString(), oldFile);
}

void tst_PinHistoryModel::testRefresh_AfterRemovalUpdatesCount()
{
    const QString imageFile = QDir(m_tempDir.path()).filePath(QStringLiteral("cache_20260101_120002_000.png"));
    QVERIFY(createImageFile(imageFile, QSize(100, 60), Qt::blue));

    SnapTray::PinHistoryModel model;
    QCOMPARE(model.rowCount(), 1);

    QSignalSpy countSpy(&model, &SnapTray::PinHistoryModel::countChanged);
    QVERIFY(QFile::remove(imageFile));

    model.refresh();

    QCOMPARE(model.rowCount(), 0);
    QVERIFY(countSpy.count() >= 1);
}

QTEST_MAIN(tst_PinHistoryModel)
#include "tst_PinHistoryModel.moc"
