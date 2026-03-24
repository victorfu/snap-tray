#include <QtTest>

#include "settings/AutoLaunchSyncPolicy.h"

using SnapTray::AutoLaunchSyncPlan;
using SnapTray::AutoLaunchSyncState;

class tst_AutoLaunchSyncPolicy : public QObject
{
    Q_OBJECT

private slots:
    void windowsClassification_recognizesCanonicalCurrentEntry();
    void windowsClassification_marksLegacyArgumentAsCurrentLegacy();
    void windowsClassification_keepsOtherExecutableSeparate();
    void startupSync_withoutPreference_onlyNormalizesCurrentLegacyEntry();
    void startupSync_enabledPreference_doesNotRebindOtherExecutable();
    void startupSync_disabledPreference_onlyDisablesCurrentEntry();
};

void tst_AutoLaunchSyncPolicy::windowsClassification_recognizesCanonicalCurrentEntry()
{
    const QString appPath = QStringLiteral("C:\\Program Files\\SnapTray\\SnapTray.exe");
    const QString command = QStringLiteral("\"%1\"").arg(appPath);

    QCOMPARE(
        SnapTray::classifyWindowsAutoLaunchEntry(true, command, appPath),
        AutoLaunchSyncState::EnabledCurrentCanonical);
}

void tst_AutoLaunchSyncPolicy::windowsClassification_marksLegacyArgumentAsCurrentLegacy()
{
    const QString appPath = QStringLiteral("C:\\Program Files\\SnapTray\\SnapTray.exe");
    const QString command = QStringLiteral("\"%1\" --minimized").arg(appPath);

    QCOMPARE(
        SnapTray::classifyWindowsAutoLaunchEntry(true, command, appPath),
        AutoLaunchSyncState::EnabledCurrentLegacy);
}

void tst_AutoLaunchSyncPolicy::windowsClassification_keepsOtherExecutableSeparate()
{
    const QString currentPath = QStringLiteral("D:\\Portable\\SnapTray.exe");
    const QString otherCommand =
        QStringLiteral("\"C:\\Program Files\\SnapTray\\SnapTray.exe\"");

    QCOMPARE(
        SnapTray::classifyWindowsAutoLaunchEntry(true, otherCommand, currentPath),
        AutoLaunchSyncState::EnabledOther);
}

void tst_AutoLaunchSyncPolicy::startupSync_withoutPreference_onlyNormalizesCurrentLegacyEntry()
{
    const AutoLaunchSyncPlan legacyPlan =
        SnapTray::buildAutoLaunchStartupSyncPlan(
            std::nullopt, AutoLaunchSyncState::EnabledCurrentLegacy);
    QVERIFY(legacyPlan.shouldApplyChange);
    QVERIFY(legacyPlan.targetEnabled);
    QVERIFY(legacyPlan.effectiveEnabled);

    const AutoLaunchSyncPlan otherPlan =
        SnapTray::buildAutoLaunchStartupSyncPlan(
            std::nullopt, AutoLaunchSyncState::EnabledOther);
    QVERIFY(!otherPlan.shouldApplyChange);
    QVERIFY(otherPlan.targetEnabled);
    QVERIFY(!otherPlan.effectiveEnabled);
}

void tst_AutoLaunchSyncPolicy::startupSync_enabledPreference_doesNotRebindOtherExecutable()
{
    const AutoLaunchSyncPlan plan =
        SnapTray::buildAutoLaunchStartupSyncPlan(
            true, AutoLaunchSyncState::EnabledOther);

    QVERIFY(!plan.shouldApplyChange);
    QVERIFY(plan.targetEnabled);
    QVERIFY(!plan.effectiveEnabled);
}

void tst_AutoLaunchSyncPolicy::startupSync_disabledPreference_onlyDisablesCurrentEntry()
{
    const AutoLaunchSyncPlan currentPlan =
        SnapTray::buildAutoLaunchStartupSyncPlan(
            false, AutoLaunchSyncState::EnabledCurrentCanonical);
    QVERIFY(currentPlan.shouldApplyChange);
    QVERIFY(!currentPlan.targetEnabled);
    QVERIFY(!currentPlan.effectiveEnabled);

    const AutoLaunchSyncPlan otherPlan =
        SnapTray::buildAutoLaunchStartupSyncPlan(
            false, AutoLaunchSyncState::EnabledOther);
    QVERIFY(!otherPlan.shouldApplyChange);
    QVERIFY(!otherPlan.targetEnabled);
    QVERIFY(!otherPlan.effectiveEnabled);
}

QTEST_MAIN(tst_AutoLaunchSyncPolicy)
#include "tst_AutoLaunchSyncPolicy.moc"
