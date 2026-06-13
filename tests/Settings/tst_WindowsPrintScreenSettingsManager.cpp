#include <QtTest>
#include <QSettings>

#include "settings/WindowsPrintScreenSettingsManager.h"

class tst_WindowsPrintScreenSettingsManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testDisableSnippingShortcut_WritesZeroToConfiguredRegistryKey();

private:
    QString testRegistryPath() const;
};

void tst_WindowsPrintScreenSettingsManager::init()
{
#ifdef Q_OS_WIN
    QSettings settings(testRegistryPath(), QSettings::NativeFormat);
    settings.clear();
    settings.sync();
    SnapTray::WindowsPrintScreenSettingsManager::instance().setRegistryPathForTests(
        testRegistryPath());
#endif
}

void tst_WindowsPrintScreenSettingsManager::cleanup()
{
#ifdef Q_OS_WIN
    QSettings settings(testRegistryPath(), QSettings::NativeFormat);
    settings.clear();
    settings.sync();
    SnapTray::WindowsPrintScreenSettingsManager::instance().clearRegistryPathForTests();
#endif
}

QString tst_WindowsPrintScreenSettingsManager::testRegistryPath() const
{
    return QStringLiteral(
        "HKEY_CURRENT_USER\\Software\\SnapTray\\Tests\\PrintScreenSnipping");
}

void tst_WindowsPrintScreenSettingsManager::
    testDisableSnippingShortcut_WritesZeroToConfiguredRegistryKey()
{
#ifndef Q_OS_WIN
    QSKIP("Windows Print Screen registry setting is only available on Windows.");
#else
    QSettings settings(testRegistryPath(), QSettings::NativeFormat);
    settings.setValue(QStringLiteral("PrintScreenKeyForSnippingEnabled"), 1);
    settings.sync();

    auto& manager = SnapTray::WindowsPrintScreenSettingsManager::instance();
    QVERIFY(manager.isSnippingShortcutEnabled());
    QVERIFY(manager.disableSnippingShortcut());
    QVERIFY(!manager.isSnippingShortcutEnabled());

    QCOMPARE(settings.value(QStringLiteral("PrintScreenKeyForSnippingEnabled")).toInt(), 0);
#endif
}

QTEST_MAIN(tst_WindowsPrintScreenSettingsManager)
#include "tst_WindowsPrintScreenSettingsManager.moc"
