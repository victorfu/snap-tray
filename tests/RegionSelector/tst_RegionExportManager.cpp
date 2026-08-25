#include <QtTest/QtTest>

#include "region/RegionExportManager.h"
#include "settings/Settings.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
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

void configureSaveSettings(const QString& screenshotPath,
                           bool autoSave,
                           bool useLastSaveLocation,
                           const QString& rememberedDirectory)
{
    auto settings = SnapTray::getSettings();
    settings.setValue(QStringLiteral("files/autoSaveScreenshots"), autoSave);
    settings.setValue(QStringLiteral("files/filenameTemplate"),
                      QStringLiteral("capture.{ext}"));
    settings.setValue(QStringLiteral("files/screenshotPath"), screenshotPath);
    settings.setValue(QStringLiteral("files/useLastScreenshotSaveLocation"),
                      useLastSaveLocation);
    settings.setValue(QString::fromLatin1(kLastSaveDirectoryKey), rememberedDirectory);
    settings.sync();
}

struct SettingSnapshot
{
    bool existed = false;
    QVariant value;
};

} // namespace

class tst_RegionExportManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void testPrepareExport_NormalizesHighDpiCrop();
    void testCreateSaveRequest_ManualUsesRememberedDirectory();
    void testCreateSaveRequest_ManualUsesScreenshotDirectoryWhenDisabled();
    void testCreateSaveRequest_CancelPreservesRememberedDirectory();
    void testCreateSaveRequest_AutoSaveUsesScreenshotDirectoryAndSkipsDialog();
    void testSavePreparedExportAsync_RemembersDirectoryAfterSuccess();
    void testSavePreparedExportAsync_DoesNotRememberWhenRequestDisablesIt();
    void testSavePreparedExportAsync_DoesNotRememberAfterFailure();

private:
    QHash<QString, SettingSnapshot> m_settingSnapshots;
};

void tst_RegionExportManager::init()
{
    auto settings = SnapTray::getSettings();
    const QStringList keys = {
        QStringLiteral("files/autoSaveScreenshots"),
        QStringLiteral("files/filenameTemplate"),
        QStringLiteral("files/lastScreenshotSaveDirectory"),
        QStringLiteral("files/screenshotPath"),
        QStringLiteral("files/useLastScreenshotSaveLocation"),
    };

    m_settingSnapshots.clear();
    for (const QString& key : keys) {
        m_settingSnapshots.insert(key, {settings.contains(key), settings.value(key)});
        settings.remove(key);
    }
    settings.sync();
}

void tst_RegionExportManager::cleanup()
{
    auto settings = SnapTray::getSettings();
    for (auto it = m_settingSnapshots.cbegin(); it != m_settingSnapshots.cend(); ++it) {
        if (it.value().existed) {
            settings.setValue(it.key(), it.value().value);
        } else {
            settings.remove(it.key());
        }
    }
    settings.sync();
    m_settingSnapshots.clear();
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

void tst_RegionExportManager::testCreateSaveRequest_ManualUsesRememberedDirectory()
{
    QTemporaryDir screenshotDirectory;
    QTemporaryDir rememberedDirectory;
    QTemporaryDir chosenDirectory;
    QVERIFY(screenshotDirectory.isValid());
    QVERIFY(rememberedDirectory.isValid());
    QVERIFY(chosenDirectory.isValid());

    configureSaveSettings(
        screenshotDirectory.path(), false, true, rememberedDirectory.path());

    const QString chosenPath = chosenDirectory.filePath(QStringLiteral("chosen.png"));
    QString observedDefaultPath;
    bool dialogCalled = false;
    RegionExportManager manager;
    manager.m_saveFileDialog = [&](QWidget*, const QString&, const QString& defaultPath,
                                   const QString&) {
        dialogCalled = true;
        observedDefaultPath = defaultPath;
        return chosenPath;
    };

    const auto request = manager.createSaveRequest(QRect(0, 0, 320, 180));

    QVERIFY(dialogCalled);
    QCOMPARE(QFileInfo(observedDefaultPath).absolutePath(), rememberedDirectory.path());
    QCOMPARE(request.filePath, chosenPath);
    QVERIFY(!request.autoSave);
    QVERIFY(request.rememberDirectoryOnSuccess);
    QVERIFY(!request.cancelled);
    QVERIFY(request.isValid());
}

void tst_RegionExportManager::testCreateSaveRequest_ManualUsesScreenshotDirectoryWhenDisabled()
{
    QTemporaryDir screenshotDirectory;
    QTemporaryDir rememberedDirectory;
    QTemporaryDir chosenDirectory;
    QVERIFY(screenshotDirectory.isValid());
    QVERIFY(rememberedDirectory.isValid());
    QVERIFY(chosenDirectory.isValid());

    configureSaveSettings(
        screenshotDirectory.path(), false, false, rememberedDirectory.path());

    const QString chosenPath = chosenDirectory.filePath(QStringLiteral("chosen.png"));
    QString observedDefaultPath;
    RegionExportManager manager;
    manager.m_saveFileDialog = [&](QWidget*, const QString&, const QString& defaultPath,
                                   const QString&) {
        observedDefaultPath = defaultPath;
        return chosenPath;
    };

    const auto request = manager.createSaveRequest(QRect(0, 0, 320, 180));

    QCOMPARE(QFileInfo(observedDefaultPath).absolutePath(), screenshotDirectory.path());
    QCOMPARE(request.filePath, chosenPath);
    QVERIFY(!request.autoSave);
    QVERIFY(!request.rememberDirectoryOnSuccess);
    QVERIFY(!request.cancelled);
    QVERIFY(request.isValid());
}

void tst_RegionExportManager::testCreateSaveRequest_CancelPreservesRememberedDirectory()
{
    QTemporaryDir screenshotDirectory;
    QTemporaryDir rememberedDirectory;
    QVERIFY(screenshotDirectory.isValid());
    QVERIFY(rememberedDirectory.isValid());

    configureSaveSettings(
        screenshotDirectory.path(), false, true, rememberedDirectory.path());

    QString observedDefaultPath;
    RegionExportManager manager;
    manager.m_saveFileDialog = [&](QWidget*, const QString&, const QString& defaultPath,
                                   const QString&) {
        observedDefaultPath = defaultPath;
        return QString();
    };

    const auto request = manager.createSaveRequest(QRect(0, 0, 320, 180));

    QCOMPARE(QFileInfo(observedDefaultPath).absolutePath(), rememberedDirectory.path());
    QVERIFY(request.filePath.isEmpty());
    QVERIFY(!request.autoSave);
    QVERIFY(request.rememberDirectoryOnSuccess);
    QVERIFY(request.cancelled);
    QVERIFY(!request.isValid());
    QCOMPARE(SnapTray::getSettings().value(kLastSaveDirectoryKey).toString(),
             rememberedDirectory.path());
}

void tst_RegionExportManager::testCreateSaveRequest_AutoSaveUsesScreenshotDirectoryAndSkipsDialog()
{
    QTemporaryDir screenshotDirectory;
    QTemporaryDir rememberedDirectory;
    QVERIFY(screenshotDirectory.isValid());
    QVERIFY(rememberedDirectory.isValid());

    configureSaveSettings(
        screenshotDirectory.path(), true, true, rememberedDirectory.path());

    bool dialogCalled = false;
    RegionExportManager manager;
    manager.m_saveFileDialog = [&](QWidget*, const QString&, const QString&, const QString&) {
        dialogCalled = true;
        return QString();
    };

    const auto request = manager.createSaveRequest(QRect(0, 0, 320, 180));

    QVERIFY(!dialogCalled);
    QCOMPARE(QFileInfo(request.filePath).absolutePath(), screenshotDirectory.path());
    QVERIFY(request.autoSave);
    QVERIFY(!request.rememberDirectoryOnSuccess);
    QVERIFY(!request.cancelled);
    QVERIFY(request.isValid());
    QCOMPARE(SnapTray::getSettings().value(kLastSaveDirectoryKey).toString(),
             rememberedDirectory.path());
}

void tst_RegionExportManager::testSavePreparedExportAsync_RemembersDirectoryAfterSuccess()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString outputDirectory = tempDir.filePath(QStringLiteral("chosen"));
    QVERIFY(QDir().mkpath(outputDirectory));
    const QString filePath = outputDirectory + QStringLiteral("/screenshot.png");
    const QString previousDirectory = tempDir.filePath(QStringLiteral("previous"));
    QVERIFY(QDir().mkpath(previousDirectory));

    configureSaveSettings(tempDir.path(), false, true, previousDirectory);

    RegionExportManager manager;
    manager.m_saveFileDialog = [filePath](QWidget*, const QString&, const QString&, const QString&) {
        return filePath;
    };
    const auto request = manager.createSaveRequest(QRect(0, 0, 320, 180));
    QCOMPARE(request.filePath, filePath);
    QVERIFY(request.rememberDirectoryOnSuccess);

    QSignalSpy completedSpy(&manager, &RegionExportManager::saveCompleted);
    QSignalSpy failedSpy(&manager, &RegionExportManager::saveFailed);

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
    const QString previousDirectory = tempDir.filePath(QStringLiteral("previous"));
    QVERIFY(QDir().mkpath(previousDirectory));

    configureSaveSettings(tempDir.path(), false, false, previousDirectory);

    RegionExportManager manager;
    manager.m_saveFileDialog = [filePath](QWidget*, const QString&, const QString&, const QString&) {
        return filePath;
    };
    const auto request = manager.createSaveRequest(QRect(0, 0, 320, 180));
    QCOMPARE(request.filePath, filePath);
    QVERIFY(!request.rememberDirectoryOnSuccess);

    QSignalSpy completedSpy(&manager, &RegionExportManager::saveCompleted);
    QSignalSpy failedSpy(&manager, &RegionExportManager::saveFailed);

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
    const QString previousDirectory = tempDir.filePath(QStringLiteral("previous"));
    QVERIFY(QDir().mkpath(previousDirectory));
    const QString filePath = tempDir.filePath(QStringLiteral("screenshot.png"));
    QVERIFY(QDir().mkpath(filePath));
    QVERIFY(QFileInfo(filePath).isDir());
    QVERIFY(QFileInfo(filePath).absoluteDir().exists());

    configureSaveSettings(tempDir.path(), false, true, previousDirectory);

    RegionExportManager manager;
    manager.m_saveFileDialog = [filePath](QWidget*, const QString&, const QString&, const QString&) {
        return filePath;
    };
    const auto request = manager.createSaveRequest(QRect(0, 0, 320, 180));
    QCOMPARE(request.filePath, filePath);
    QVERIFY(request.rememberDirectoryOnSuccess);

    QSignalSpy completedSpy(&manager, &RegionExportManager::saveCompleted);
    QSignalSpy failedSpy(&manager, &RegionExportManager::saveFailed);

    manager.savePreparedExportAsync(makePreparedExport(), request);

    QTRY_COMPARE_WITH_TIMEOUT(failedSpy.count(), 1, 5000);
    QCOMPARE(completedSpy.count(), 0);
    QVERIFY(QFileInfo(filePath).isDir());
    QCOMPARE(SnapTray::getSettings().value(kLastSaveDirectoryKey).toString(), previousDirectory);
}

QTEST_MAIN(tst_RegionExportManager)
#include "tst_RegionExportManager.moc"
