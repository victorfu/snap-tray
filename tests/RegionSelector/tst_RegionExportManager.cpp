#include <QtTest/QtTest>

#include "region/RegionExportManager.h"
#include "settings/Settings.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

namespace {

QPixmap makeHighDpiPixmap()
{
    QImage image(QSize(8, 8), QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            image.setPixelColor(x, y, QColor(10 + x * 20, 15 + y * 17, 30 + (x + y) * 9));
        }
    }

    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(2.0);
    return pixmap;
}

RegionExportManager::PreparedExport makePreparedExport()
{
    RegionExportManager::PreparedExport prepared;
    prepared.image = QImage(QSize(8, 8), QImage::Format_ARGB32_Premultiplied);
    prepared.image.fill(QColor(40, 80, 120));
    prepared.pixmap = QPixmap::fromImage(prepared.image);
    return prepared;
}

constexpr auto kLastSaveDirectoryKey = "files/lastScreenshotSaveDirectory";

} // namespace

class tst_RegionExportManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void testPrepareExport_NormalizesHighDpiCrop();
    void testSavePreparedExportAsync_RemembersDirectoryAfterSuccess();
    void testSavePreparedExportAsync_DoesNotRememberWhenRequestDisablesIt();
    void testSavePreparedExportAsync_DoesNotRememberAfterFailure();

private:
    bool m_hadLastSaveDirectory = false;
    QVariant m_previousLastSaveDirectory;
};

void tst_RegionExportManager::init()
{
    auto settings = SnapTray::getSettings();
    settings.sync();
    m_hadLastSaveDirectory = settings.contains(kLastSaveDirectoryKey);
    m_previousLastSaveDirectory = settings.value(kLastSaveDirectoryKey);
}

void tst_RegionExportManager::cleanup()
{
    auto settings = SnapTray::getSettings();
    if (m_hadLastSaveDirectory) {
        settings.setValue(kLastSaveDirectoryKey, m_previousLastSaveDirectory);
    } else {
        settings.remove(kLastSaveDirectoryKey);
    }
    settings.sync();
}

void tst_RegionExportManager::testPrepareExport_NormalizesHighDpiCrop()
{
    const QPixmap background = makeHighDpiPixmap();
    const QImage sourceImage = background.toImage();

    RegionExportManager manager;
    manager.setBackgroundPixmap(background);
    manager.setDevicePixelRatio(2.0);

    const RegionExportManager::PreparedExport prepared = manager.prepareExport(QRect(1, 1, 2, 2), 0);
    QVERIFY(prepared.isValid());
    QCOMPARE(prepared.pixmap.devicePixelRatio(), 2.0);
    QCOMPARE(prepared.image.devicePixelRatio(), 1.0);
    QCOMPARE(prepared.image.size(), QSize(4, 4));

    for (int y = 0; y < prepared.image.height(); ++y) {
        for (int x = 0; x < prepared.image.width(); ++x) {
            QCOMPARE(prepared.image.pixelColor(x, y), sourceImage.pixelColor(x + 2, y + 2));
        }
    }
}

void tst_RegionExportManager::testSavePreparedExportAsync_RemembersDirectoryAfterSuccess()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString outputDirectory = tempDir.filePath(QStringLiteral("chosen"));
    QVERIFY(QDir().mkpath(outputDirectory));
    const QString filePath = outputDirectory + QStringLiteral("/screenshot.png");

    auto settings = SnapTray::getSettings();
    settings.setValue(kLastSaveDirectoryKey, QStringLiteral("previous-directory"));
    settings.sync();

    RegionExportManager manager;
    QSignalSpy completedSpy(&manager, &RegionExportManager::saveCompleted);
    QSignalSpy failedSpy(&manager, &RegionExportManager::saveFailed);
    RegionExportManager::SaveRequest request;
    request.filePath = filePath;
    request.rememberDirectoryOnSuccess = true;

    manager.savePreparedExportAsync(makePreparedExport(), request);

    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 5000);
    QCOMPARE(failedSpy.count(), 0);
    QVERIFY(QFileInfo::exists(filePath));
    QCOMPARE(SnapTray::getSettings().value(kLastSaveDirectoryKey).toString(),
             QFileInfo(filePath).absolutePath());
}

void tst_RegionExportManager::testSavePreparedExportAsync_DoesNotRememberWhenRequestDisablesIt()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString filePath = tempDir.filePath(QStringLiteral("screenshot.png"));
    const QString previousDirectory = QStringLiteral("previous-directory");

    auto settings = SnapTray::getSettings();
    settings.setValue(kLastSaveDirectoryKey, previousDirectory);
    settings.sync();

    RegionExportManager manager;
    QSignalSpy completedSpy(&manager, &RegionExportManager::saveCompleted);
    QSignalSpy failedSpy(&manager, &RegionExportManager::saveFailed);
    RegionExportManager::SaveRequest request;
    request.filePath = filePath;
    request.rememberDirectoryOnSuccess = false;

    manager.savePreparedExportAsync(makePreparedExport(), request);

    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 5000);
    QCOMPARE(failedSpy.count(), 0);
    QVERIFY(QFileInfo::exists(filePath));
    QCOMPARE(SnapTray::getSettings().value(kLastSaveDirectoryKey).toString(), previousDirectory);
}

void tst_RegionExportManager::testSavePreparedExportAsync_DoesNotRememberAfterFailure()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString filePath = tempDir.filePath(QStringLiteral("missing/screenshot.png"));
    const QString previousDirectory = QStringLiteral("previous-directory");

    auto settings = SnapTray::getSettings();
    settings.setValue(kLastSaveDirectoryKey, previousDirectory);
    settings.sync();

    RegionExportManager manager;
    QSignalSpy completedSpy(&manager, &RegionExportManager::saveCompleted);
    QSignalSpy failedSpy(&manager, &RegionExportManager::saveFailed);
    RegionExportManager::SaveRequest request;
    request.filePath = filePath;
    request.rememberDirectoryOnSuccess = true;

    manager.savePreparedExportAsync(makePreparedExport(), request);

    QTRY_COMPARE_WITH_TIMEOUT(failedSpy.count(), 1, 5000);
    QCOMPARE(completedSpy.count(), 0);
    QVERIFY(!QFileInfo::exists(filePath));
    QCOMPARE(SnapTray::getSettings().value(kLastSaveDirectoryKey).toString(), previousDirectory);
}

QTEST_MAIN(tst_RegionExportManager)
#include "tst_RegionExportManager.moc"
