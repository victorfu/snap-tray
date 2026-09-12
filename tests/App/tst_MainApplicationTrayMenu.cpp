#include "MainApplication.h"
#include "ImageColorSpaceHelper.h"
#include <QClipboard>
#include <QCursor>

#include "cli/IPCProtocol.h"
#include "hotkey/HotkeyManager.h"
#include "qml/QmlDialog.h"
#include "qml/ScreenPickerViewModel.h"
#include "qml/QmlSettingsWindow.h"
#include "RecordingManager.h"
#include "CaptureManager.h"
#include "PinWindowManager.h"
#include "PinWindow.h"
#include "pinwindow/PinWindowPlacement.h"
#include "settings/Settings.h"
#include "update/IUpdateService.h"
#include "update/InstallSourceDetector.h"
#include "update/UpdateCoordinator.h"

#include <QAction>
#include <QDir>
#include <QMenu>
#include <QSettings>
#include <QTranslator>
#include <QBuffer>
#include <QImageReader>
#include <QTemporaryDir>
#include <QScreen>
#include <QPainter>
#include <QtTest>

namespace {

QString translationFilePath()
{
    return QDir(QString::fromUtf8(SNAPTRAY_TEST_TRANSLATION_DIR))
        .filePath(QStringLiteral("snaptray_zh_TW.qm"));
}

class FakeUpdateService final : public IUpdateService
{
public:
    UpdateServiceKind kind() const override
    {
        return m_externallyManaged ? UpdateServiceKind::ExternalManaged
                                   : UpdateServiceKind::Unsupported;
    }

    InstallSource installSource() const override { return m_installSource; }
    bool isExternallyManaged() const override { return m_externallyManaged; }

    QString managementMessage() const override
    {
        return m_externallyManaged
            ? updateManagementMessageForSource(m_installSource)
            : QString();
    }

    bool initialize(QString* errorMessage) override
    {
        initializeCalled = true;
        if (errorMessage) {
            *errorMessage = QString();
        }
        return true;
    }

    void startAutomaticChecks() override
    {
        ++startAutomaticChecksCount;
    }

    void syncSettings(bool autoCheckEnabled, int checkIntervalHours) override
    {
        lastAutoCheckEnabled = autoCheckEnabled;
        lastCheckIntervalHours = checkIntervalHours;
    }

    UpdateCheckResult checkForUpdatesInteractive() override
    {
        ++interactiveCheckCount;
        return interactiveCheckResult;
    }

    InstallSource m_installSource = InstallSource::DirectDownload;
    bool m_externallyManaged = false;
    UpdateCheckResult interactiveCheckResult = UpdateCheckResult::started();
    bool initializeCalled = false;
    int startAutomaticChecksCount = 0;
    int interactiveCheckCount = 0;
    bool lastAutoCheckEnabled = true;
    int lastCheckIntervalHours = 24;
};

FakeUpdateService* g_fakeUpdateService = nullptr;

class DummyScreenPickerDialog final : public SnapTray::QmlDialog
{
public:
    explicit DummyScreenPickerDialog(QObject* viewModel, QObject* parent = nullptr)
        : SnapTray::QmlDialog(QUrl(), viewModel, QStringLiteral("viewModel"), parent)
    {
    }

    void showCenteredOnScreen(QScreen* screen) override
    {
        Q_UNUSED(screen);
    }

    void close() override
    {
        emit closed();
        deleteLater();
    }

    void simulateExternalClose()
    {
        emit closed();
    }
};

} // namespace

class tst_MainApplicationTrayMenu : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void cleanupTestCase();

    void updateTrayMenuHotkeyText_updatesPasteAction();
    void updateTrayMenuHotkeyText_marksFailedHotkeyUnavailable();
    void updateTrayMenuHotkeyText_usesTranslatedPasteLabel();
    void initialize_directDownload_addsEnabledCheckForUpdatesActionBeforeSettings();
    void initialize_hidesRecordingActionWhenUnsupported();
    void onCheckForUpdates_usesSharedSettingsWindowFlowWithoutShowingSettings();
    void initialize_externalManaged_disablesCheckForUpdatesAction();
    void handleCLICommand_removedRecordCommandIsIgnored();
    void screenPickerClosed_deletesWrapperAndViewModel();
    void queuedHistoryEntryRechecksCaptureMode();
    void cliClipboardPinPosition_data();
    void cliClipboardPinPosition();
    void cliFilePinUsesImageLoader_data();
    void cliFilePinUsesImageLoader();

private:
    void clearAllTestSettings();
    void clearSetting(const char* key);
    QString registrationOrderKey(const char* key) const;
    void installFakeUpdateService(InstallSource installSource,
                                  bool externallyManaged,
                                  UpdateCheckResult result = UpdateCheckResult::started());

    SnapTray::HotkeyManager& manager() { return SnapTray::HotkeyManager::instance(); }
};

void tst_MainApplicationTrayMenu::init()
{
    manager().shutdown();
    manager().m_registerHotkeyOverride = [](SnapTray::HotkeyAction, const QString&) {
        return true;
    };
    clearAllTestSettings();
    InstallSourceDetector::clearDetectedSourceForTests();
    UpdateCoordinator::resetForTests();
    g_fakeUpdateService = nullptr;
    manager().initialize();
}

void tst_MainApplicationTrayMenu::cleanup()
{
    manager().shutdown();
    manager().m_registerHotkeyOverride = {};
    clearAllTestSettings();
    InstallSourceDetector::clearDetectedSourceForTests();
    UpdateCoordinator::resetForTests();
    g_fakeUpdateService = nullptr;
}

void tst_MainApplicationTrayMenu::cleanupTestCase()
{
    manager().shutdown();
}

void tst_MainApplicationTrayMenu::updateTrayMenuHotkeyText_updatesPasteAction()
{
    MainApplication application;
    QAction pasteAction(&application);
    application.m_pasteAction = &pasteAction;

    application.updateTrayMenuHotkeyText();

    const QString baseName = MainApplication::tr("Paste");
    const QString displayHotkey = SnapTray::HotkeyManager::formatKeySequence(
        manager().getConfig(SnapTray::HotkeyAction::PasteFromClipboard).keySequence);

    QVERIFY(!displayHotkey.isEmpty());
    QCOMPARE(pasteAction.text(), MainApplication::tr("%1 (%2)").arg(baseName, displayHotkey));
}

void tst_MainApplicationTrayMenu::updateTrayMenuHotkeyText_marksFailedHotkeyUnavailable()
{
    using namespace SnapTray;

    manager().shutdown();
    clearAllTestSettings();
    manager().m_registerHotkeyOverride = [](HotkeyAction, const QString&) {
        return true;
    };
    manager().initialize();

    QVERIFY(manager().updateHotkey(HotkeyAction::PinFromImage, QStringLiteral("F9")));
    QVERIFY(!manager().updateHotkey(HotkeyAction::RegionCapture, QStringLiteral("F9")));
    QCOMPARE(manager().getConfig(HotkeyAction::RegionCapture).status, HotkeyStatus::Failed);

    MainApplication application;
    QAction regionAction(&application);
    application.m_regionCaptureAction = &regionAction;

    application.updateTrayMenuHotkeyText();

    QCOMPARE(regionAction.text(),
             MainApplication::tr("%1 (%2)")
                 .arg(MainApplication::tr("Region Capture"), QStringLiteral("F9 unavailable")));
}

void tst_MainApplicationTrayMenu::updateTrayMenuHotkeyText_usesTranslatedPasteLabel()
{
    QTranslator translator;
    QVERIFY2(translator.load(translationFilePath()),
             qPrintable(QStringLiteral("Failed to load translation file: %1")
                            .arg(translationFilePath())));
    QVERIFY(QCoreApplication::installTranslator(&translator));

    MainApplication application;
    QAction pasteAction(&application);
    application.m_pasteAction = &pasteAction;

    application.updateTrayMenuHotkeyText();

    const QString translatedBaseName = QCoreApplication::translate("MainApplication", "Paste");
    const QString displayHotkey = SnapTray::HotkeyManager::formatKeySequence(
        manager().getConfig(SnapTray::HotkeyAction::PasteFromClipboard).keySequence);

    QCOMPARE(translatedBaseName, QString::fromUtf8("貼上"));
    QVERIFY(!displayHotkey.isEmpty());
    QCOMPARE(pasteAction.text(),
             QCoreApplication::translate("MainApplication", "%1 (%2)")
                 .arg(translatedBaseName, displayHotkey));

    QCoreApplication::removeTranslator(&translator);
}

void tst_MainApplicationTrayMenu::initialize_directDownload_addsEnabledCheckForUpdatesActionBeforeSettings()
{
    installFakeUpdateService(InstallSource::DirectDownload, false);

    MainApplication application;
    application.initialize();

    QVERIFY(g_fakeUpdateService != nullptr);
    QVERIFY(g_fakeUpdateService->initializeCalled);
    QVERIFY(application.m_checkForUpdatesAction != nullptr);
    QVERIFY(application.m_checkForUpdatesAction->isEnabled());
    QCOMPARE(application.m_checkForUpdatesAction->text(),
             MainApplication::tr("Check for Updates"));

    const QList<QAction*> actions = application.m_trayMenu->actions();
    const int checkIndex = actions.indexOf(application.m_checkForUpdatesAction);
    int settingsIndex = -1;
    for (int i = 0; i < actions.size(); ++i) {
        if (actions.at(i) && actions.at(i)->text() == MainApplication::tr("Settings")) {
            settingsIndex = i;
            break;
        }
    }

    QVERIFY(checkIndex >= 0);
    QVERIFY(settingsIndex >= 0);
    QCOMPARE(checkIndex + 1, settingsIndex);
}

void tst_MainApplicationTrayMenu::initialize_hidesRecordingActionWhenUnsupported()
{
    installFakeUpdateService(InstallSource::DirectDownload, false);

    MainApplication application;
    application.initialize();

#ifdef Q_OS_LINUX
    QVERIFY(application.m_fullScreenRecordingAction == nullptr);

    const QList<QAction*> actions = application.m_trayMenu->actions();
    for (QAction* action : actions) {
        QVERIFY(!action || action->text() != MainApplication::tr("Record Screen"));
    }
#else
    QVERIFY(application.m_fullScreenRecordingAction != nullptr);
#endif
}

void tst_MainApplicationTrayMenu::onCheckForUpdates_usesSharedSettingsWindowFlowWithoutShowingSettings()
{
    installFakeUpdateService(InstallSource::DirectDownload, false);

    MainApplication application;
    application.initialize();

    QVERIFY(application.m_settingsWindow.isNull());

    application.onCheckForUpdates();

    QVERIFY(g_fakeUpdateService != nullptr);
    QCOMPARE(g_fakeUpdateService->interactiveCheckCount, 1);
    QVERIFY(!application.m_settingsWindow.isNull());
    QVERIFY(!application.m_settingsWindow->isVisible());
}

void tst_MainApplicationTrayMenu::initialize_externalManaged_disablesCheckForUpdatesAction()
{
    installFakeUpdateService(InstallSource::MicrosoftStore, true);

    MainApplication application;
    application.initialize();

    QVERIFY(application.m_checkForUpdatesAction != nullptr);
    QVERIFY(!application.m_checkForUpdatesAction->isEnabled());
}

void tst_MainApplicationTrayMenu::handleCLICommand_removedRecordCommandIsIgnored()
{
    using namespace SnapTray::CLI;

    installFakeUpdateService(InstallSource::DirectDownload, false);

    MainApplication application;
    application.initialize();
    application.m_screenPickerDialog = new DummyScreenPickerDialog(new QObject(), &application);

    IPCMessage message;
    message.command = QStringLiteral("record");
    message.options = QJsonObject{{QStringLiteral("action"), QStringLiteral("toggle")}};

    application.handleCLICommand(message.toJson());

    QVERIFY(application.m_screenPickerDialog != nullptr);
    QCOMPARE(application.m_recordingManager->state(), RecordingManager::State::Idle);

    application.closeScreenPicker();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void tst_MainApplicationTrayMenu::queuedHistoryEntryRechecksCaptureMode()
{
    MainApplication application;
    application.m_captureManager = new CaptureManager(nullptr, &application);
    application.m_recordingManager = new RecordingManager(&application);
    int replayCalls = 0;
    application.m_historyReplayStarter = [&](const QString&) { ++replayCalls; return true; };
    QVERIFY(application.canStartRegionCapture());
    bool queuedResult = true;
    QMetaObject::invokeMethod(&application, [&] {
        queuedResult = application.startHistoryReplay("test-entry");
    }, Qt::QueuedConnection);
    auto* viewModel = new SnapTray::ScreenPickerViewModel();
    auto* picker = new DummyScreenPickerDialog(viewModel, &application);
    application.attachScreenPicker(picker, viewModel);
    QCoreApplication::processEvents();
    QVERIFY(!queuedResult);
    QCOMPARE(replayCalls, 0);
    QSignalSpy captureStarted(application.m_captureManager, &CaptureManager::captureStarted);
    application.onHotkeyAction(SnapTray::HotkeyAction::RegionCapture);
    QCOMPARE(captureStarted.count(), 0);
    application.closeScreenPicker();
    QVERIFY(application.startHistoryReplay("test-entry"));
    QCOMPARE(replayCalls, 1);
    using State = RecordingManager::State;
    for (State state : {State::Preparing, State::Countdown, State::Recording,
                        State::Paused, State::Encoding, State::Previewing}) {
        application.m_recordingManager->m_state = state;
        QVERIFY(!application.canStartRegionCapture());
        QVERIFY(!application.startHistoryReplay("test-entry"));
        application.onRegionCapture();
        QCOMPARE(captureStarted.count(), 0);
        QCOMPARE(replayCalls, 1);
    }
    application.m_recordingManager->m_state = State::Idle;
}

void tst_MainApplicationTrayMenu::screenPickerClosed_deletesWrapperAndViewModel()
{
    MainApplication application;
    auto* viewModel = new SnapTray::ScreenPickerViewModel();
    auto* dialog = new DummyScreenPickerDialog(viewModel, &application);

    QPointer<SnapTray::ScreenPickerViewModel> viewModelGuard(viewModel);
    QPointer<DummyScreenPickerDialog> dialogGuard(dialog);

    application.attachScreenPicker(dialog, viewModel);
    dialog->simulateExternalClose();

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    QVERIFY(application.m_screenPickerDialog.isNull());
    QVERIFY(application.m_screenPickerViewModel.isNull());
    QVERIFY(dialogGuard.isNull());
    QVERIFY(viewModelGuard.isNull());
}

void tst_MainApplicationTrayMenu::clearAllTestSettings()
{
    clearSetting(SnapTray::kSettingsKeyHotkey);
    clearSetting(SnapTray::kSettingsKeyScreenCanvasHotkey);
    clearSetting(SnapTray::kSettingsKeyPasteHotkey);
    clearSetting(SnapTray::kSettingsKeyQuickPinHotkey);
    clearSetting(SnapTray::kSettingsKeyPinFromImageHotkey);
    clearSetting(SnapTray::kSettingsKeyHistoryWindowHotkey);
    clearSetting(SnapTray::kSettingsKeyTogglePinsVisibilityHotkey);
    clearSetting(SnapTray::kSettingsKeyRecordFullScreenHotkey);

    auto settings = SnapTray::getSettings();
    settings.remove(QString::fromLatin1(SnapTray::kSettingsKeyHotkeyRegistrationCounter));
    settings.remove(QStringLiteral("update/autoCheck"));
    settings.remove(QStringLiteral("update/checkIntervalHours"));
    settings.remove(QStringLiteral("update/lastCheckTime"));
    settings.sync();
}

void tst_MainApplicationTrayMenu::clearSetting(const char* key)
{
    auto settings = SnapTray::getSettings();
    settings.remove(key);
    settings.remove(QString::fromUtf8(key) + QString::fromLatin1(SnapTray::kSettingsKeyHotkeyEnabledSuffix));
    settings.remove(registrationOrderKey(key));
}

QString tst_MainApplicationTrayMenu::registrationOrderKey(const char* key) const
{
    return QString::fromUtf8(key)
        + QString::fromLatin1(SnapTray::kSettingsKeyHotkeyRegistrationOrderSuffix);
}

void tst_MainApplicationTrayMenu::installFakeUpdateService(InstallSource installSource,
                                                           bool externallyManaged,
                                                           UpdateCheckResult result)
{
    InstallSourceDetector::setDetectedSourceForTests(installSource);
    UpdateCoordinator::setServiceFactoryForTests(
        [installSource, externallyManaged, result](UpdateServiceKind, InstallSource) {
            auto service = std::make_unique<FakeUpdateService>();
            service->m_installSource = installSource;
            service->m_externallyManaged = externallyManaged;
            service->interactiveCheckResult = result;
            g_fakeUpdateService = service.get();
            return service;
        });
}

void tst_MainApplicationTrayMenu::cliFilePinUsesImageLoader_data()
{
    QTest::addColumn<int>("orientation");
    QTest::addColumn<bool>("large");
    QTest::addColumn<bool>("positioned");
    QTest::newRow("rotate90") << 6 << false << false;
    QTest::newRow("rotate270") << 8 << false << false;
    QTest::newRow("mirror") << 2 << false << true;
    QTest::newRow("large") << 1 << true << false;
    QTest::newRow("large-positioned") << 1 << true << true;
}

void tst_MainApplicationTrayMenu::cliFilePinUsesImageLoader()
{
    QFETCH(int, orientation);
    QFETCH(bool, large);
    QFETCH(bool, positioned);
    installFakeUpdateService(InstallSource::DirectDownload, false);
    MainApplication application;
    application.initialize();
    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    const QSize size = large ? screen->availableGeometry().size() * 2 : QSize(120, 80);
    QImage input(size, QImage::Format_RGB32);
    input.fill(Qt::red);
    { QPainter painter(&input); painter.fillRect(QRect(0, 0, size.width()/2, size.height()/2), Qt::blue); }
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(input.save(&buffer, "JPEG", 100));
    QByteArray exif = QByteArray::fromHex("45786966000049492a0008000000010012010300010000000100000000000000");
    exif[24] = char(orientation);
    QByteArray segment = QByteArray::fromHex("ffe10022") + exif;
    jpeg.insert(2, segment);
    QTemporaryDir dir;
    const QString path = dir.filePath("oriented.jpg");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(jpeg), qint64(jpeg.size()));
    file.close();
    QImageReader reader(path);
    reader.setAutoTransform(false);
    QImage expected = reader.read();
    if (orientation == 6) expected = expected.transformed(QTransform().rotate(90));
    if (orientation == 8) expected = expected.transformed(QTransform().rotate(-90));
    if (orientation == 2) expected = expected.mirrored(true, false);
    expected = convertImageForDisplay(expected);
    const QPoint position = screen->availableGeometry().topLeft() + QPoint(37, 43);
    SnapTray::CLI::IPCMessage command;
    command.command = "pin";
    command.options = {{"file", path}};
    if (positioned) { command.options["x"] = position.x(); command.options["y"] = position.y(); }
    QSignalSpy created(application.m_pinWindowManager, &PinWindowManager::windowCreated);
    application.handleCLICommand(command.toJson());
    QTRY_COMPARE(created.count(), 1);
    PinWindow* pin = application.m_pinWindowManager->windows().first();
    const auto placement = computeInitialPinWindowPlacement(QPixmap::fromImage(expected), screen->availableGeometry());
    QCOMPARE(pin->zoomLevel(), placement.zoomLevel);
    QCOMPARE(pin->size(), placement.displaySize);
    QCOMPARE(pin->pos(), positioned ? position : placement.position);
    if (!large) {
        const QImage pixels = pin->exportPixmapForMerge().toImage();
        QCOMPARE(pixels.size(), expected.size());
        for (const QPoint point : {QPoint(10,10), QPoint(expected.width()-11,10),
                                   QPoint(10,expected.height()-11), QPoint(expected.width()-11,expected.height()-11)}) {
            QCOMPARE(pixels.pixelColor(point), expected.pixelColor(point));
        }
    }
    application.m_pinWindowManager->closeAllWindows();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void tst_MainApplicationTrayMenu::cliClipboardPinPosition_data()
{
    QTest::addColumn<bool>("text");
    QTest::addColumn<int>("coordinates");
    for (bool text : {false, true}) {
        for (int coordinates = 0; coordinates < 4; ++coordinates) {
            QTest::newRow(qPrintable(QString("%1-%2").arg(text ? "text" : "image").arg(coordinates)))
                << text << coordinates;
        }
    }
}

void tst_MainApplicationTrayMenu::cliClipboardPinPosition()
{
    QFETCH(bool, text);
    QFETCH(int, coordinates);
    installFakeUpdateService(InstallSource::DirectDownload, false);
    MainApplication application;
    application.initialize();
    auto* clipboard = QGuiApplication::clipboard();
    if (text) {
        clipboard->setText("Clipboard pin position");
    } else {
        QPixmap image(120, 80);
        image.fill(Qt::green);
        image.setDevicePixelRatio(2.0);
        clipboard->setPixmap(image);
    }
    SnapTray::CLI::IPCMessage command;
    command.command = "pin";
    command.options = {{"clipboard", true}};
    const QPoint requested(-240, -120);
    if (coordinates & 1) command.options["x"] = requested.x();
    if (coordinates & 2) command.options["y"] = requested.y();
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    QSignalSpy created(application.m_pinWindowManager, &PinWindowManager::windowCreated);
    application.handleCLICommand(command.toJson());
    QCOMPARE(created.count(), 1);
    auto* pin = application.m_pinWindowManager->windows().first();
    const QPoint centered = screen->geometry().center() - QPoint(pin->width()/2, pin->height()/2);
    QCOMPARE(pin->pos(), coordinates == 3 ? requested : centered);
    application.m_pinWindowManager->closeAllWindows();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    clipboard->clear();
}

QTEST_MAIN(tst_MainApplicationTrayMenu)
#include "tst_MainApplicationTrayMenu.moc"
