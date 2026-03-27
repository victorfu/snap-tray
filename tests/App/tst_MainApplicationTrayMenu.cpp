#define private public
#include "MainApplication.h"
#undef private

#include "hotkey/HotkeyManager.h"
#include "settings/Settings.h"

#include <QAction>
#include <QSettings>
#include <QtTest>

class tst_MainApplicationTrayMenu : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void cleanupTestCase();

    void updateTrayMenuHotkeyText_updatesPasteAction();

private:
    void clearAllTestSettings();
    void clearSetting(const char* key);
    QString registrationOrderKey(const char* key) const;

    SnapTray::HotkeyManager& manager() { return SnapTray::HotkeyManager::instance(); }
};

void tst_MainApplicationTrayMenu::init()
{
    manager().shutdown();
    clearAllTestSettings();
    manager().initialize();
}

void tst_MainApplicationTrayMenu::cleanup()
{
    manager().shutdown();
    clearAllTestSettings();
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

QTEST_MAIN(tst_MainApplicationTrayMenu)
#include "tst_MainApplicationTrayMenu.moc"
