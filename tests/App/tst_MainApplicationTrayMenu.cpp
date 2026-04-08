#include "MainApplication.h"

#include "cli/IPCProtocol.h"
#include "hotkey/HotkeyManager.h"
#include "qml/QmlDialog.h"
#include "qml/ScreenPickerViewModel.h"
#include "qml/QmlSettingsWindow.h"
#include "RecordingManager.h"
#include "settings/Settings.h"
#include "update/IUpdateService.h"
#include "update/InstallSourceDetector.h"
#include "update/UpdateCoordinator.h"

#include <QAction>
#include <QDir>
#include <QMenu>
#include <QSettings>
#include <QTranslator>
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
    void updateTrayMenuHotkeyText_usesTranslatedPasteLabel();
    void initialize_directDownload_addsEnabledCheckForUpdatesActionBeforeSettings();
    void onCheckForUpdates_usesSharedSettingsWindowFlowWithoutShowingSettings();
    void initialize_externalManaged_disablesCheckForUpdatesAction();
    void handleCLICommand_recordToggle_closesScreenPicker();
    void handleCLICommand_recordStart_doesNothingWhilePickerOpen();
    void screenPickerClosed_deletesWrapperAndViewModel();

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
    clearAllTestSettings();
    InstallSourceDetector::clearDetectedSourceForTests();
    UpdateCoordinator::resetForTests();
    g_fakeUpdateService = nullptr;
    manager().initialize();
}

void tst_MainApplicationTrayMenu::cleanup()
{
    manager().shutdown();
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

void tst_MainApplicationTrayMenu::handleCLICommand_recordToggle_closesScreenPicker()
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

    QVERIFY(application.m_screenPickerDialog.isNull());
    QCOMPARE(application.m_recordingManager->state(), RecordingManager::State::Idle);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void tst_MainApplicationTrayMenu::handleCLICommand_recordStart_doesNothingWhilePickerOpen()
{
    using namespace SnapTray::CLI;

    installFakeUpdateService(InstallSource::DirectDownload, false);

    MainApplication application;
    application.initialize();
    application.m_screenPickerDialog = new DummyScreenPickerDialog(new QObject(), &application);

    IPCMessage message;
    message.command = QStringLiteral("record");
    message.options = QJsonObject{{QStringLiteral("action"), QStringLiteral("start")}};

    application.handleCLICommand(message.toJson());

    QVERIFY(application.m_screenPickerDialog != nullptr);
    QCOMPARE(application.m_recordingManager->state(), RecordingManager::State::Idle);

    application.closeScreenPicker();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
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

QTEST_MAIN(tst_MainApplicationTrayMenu)
#include "tst_MainApplicationTrayMenu.moc"
