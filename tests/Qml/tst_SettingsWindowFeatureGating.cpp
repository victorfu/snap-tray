#include <QtTest/QtTest>

#include "qml/SettingsBackend.h"
#include "settings/Settings.h"

#include <QAccessible>
#include <QHash>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

namespace {

struct SettingSnapshot
{
    bool existed = false;
    QVariant value;
};

} // namespace

class tst_SettingsWindowFeatureGating : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void sidebarModelHidesUnsupportedPages();
    void filesPageShowsRememberLastFolderOnAllPlatforms();

private:
    QHash<QString, SettingSnapshot> m_settingSnapshots;
};

void tst_SettingsWindowFeatureGating::initTestCase()
{
    auto settings = SnapTray::getSettings();
    const QStringList keys = {
        QStringLiteral("files/filenameTemplate"),
        QStringLiteral("files/useLastScreenshotSaveLocation"),
    };

    for (const QString& key : keys) {
        m_settingSnapshots.insert(key, {settings.contains(key), settings.value(key)});
    }
}

void tst_SettingsWindowFeatureGating::cleanupTestCase()
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
}

void tst_SettingsWindowFeatureGating::init()
{
    auto settings = SnapTray::getSettings();
    settings.remove(QStringLiteral("files/filenameTemplate"));
    settings.remove(QStringLiteral("files/useLastScreenshotSaveLocation"));
    settings.sync();
}

void tst_SettingsWindowFeatureGating::cleanup()
{
    init();
}

void tst_SettingsWindowFeatureGating::sidebarModelHidesUnsupportedPages()
{
    QQmlEngine engine;
    SnapTray::SettingsBackend backend;
    engine.rootContext()->setContextProperty(QStringLiteral("settingsBackend"), &backend);

    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/SnapTrayQml/settings/SettingsSidebar.qml")));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root != nullptr, qPrintable(component.errorString()));

    const QVariant pages = root->property("pages");
    QVERIFY(pages.isValid());
    const QVariantList pageList = pages.toList();

    QStringList keys;
    for (const QVariant& item : pageList) {
        keys.append(item.toMap().value(QStringLiteral("key")).toString());
    }

#if defined(Q_OS_LINUX)
    QVERIFY(!keys.contains(QStringLiteral("ocr")));
    QVERIFY(!keys.contains(QStringLiteral("recording")));
#else
    QVERIFY(keys.contains(QStringLiteral("ocr")));
    QVERIFY(keys.contains(QStringLiteral("recording")));
#endif
}

void tst_SettingsWindowFeatureGating::filesPageShowsRememberLastFolderOnAllPlatforms()
{
    QQmlEngine engine;
    SnapTray::SettingsBackend backend;
    backend.setUseLastScreenshotSaveLocation(true);
    engine.rootContext()->setContextProperty(QStringLiteral("settingsBackend"), &backend);

    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/SnapTrayQml/settings/FilesSettings.qml")));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root != nullptr, qPrintable(component.errorString()));

    auto* toggle = root->findChild<QQuickItem*>(QStringLiteral("rememberLastFolderToggle"));
    QVERIFY(toggle != nullptr);
    QVERIFY(toggle->property("visible").toBool());
    QCOMPARE(toggle->property("label").toString(), QStringLiteral("Remember last folder"));

    const QString expectedDescription = QStringLiteral(
        "When saving an image manually, the dialog opens in the folder of the last successful save. "
        "Auto-save still uses the Screenshots folder above.");
    QCOMPARE(toggle->property("description").toString(), expectedDescription);
    QVERIFY(toggle->property("checked").toBool());

    backend.setUseLastScreenshotSaveLocation(false);
    QTRY_VERIFY(!toggle->property("checked").toBool());

    QAccessibleInterface* accessibleToggle = nullptr;
    const auto descendants = toggle->findChildren<QQuickItem*>();
    for (QQuickItem* item : descendants) {
        QAccessibleInterface* accessibleInterface = QAccessible::queryAccessibleInterface(item);
        if (!accessibleInterface || accessibleInterface->role() != QAccessible::CheckBox
            || accessibleInterface->text(QAccessible::Name) != QStringLiteral("Remember last folder")) {
            continue;
        }

        QCOMPARE(accessibleInterface->text(QAccessible::Description), expectedDescription);
        accessibleToggle = accessibleInterface;
        break;
    }
    QVERIFY(accessibleToggle != nullptr);

    QAccessibleActionInterface* actionInterface = accessibleToggle->actionInterface();
    QVERIFY(actionInterface != nullptr);
    QVERIFY(actionInterface->actionNames().contains(QAccessibleActionInterface::pressAction()));
    QVERIFY(actionInterface->actionNames().contains(QAccessibleActionInterface::toggleAction()));
    auto* accessibleItem = qobject_cast<QQuickItem*>(accessibleToggle->object());
    QVERIFY(accessibleItem != nullptr);
    QVERIFY(accessibleItem->activeFocusOnTab());

    QSignalSpy changedSpy(&backend, &SnapTray::SettingsBackend::useLastScreenshotSaveLocationChanged);
    actionInterface->doAction(QAccessibleActionInterface::toggleAction());
    QTRY_VERIFY(backend.useLastScreenshotSaveLocation());
    QTRY_VERIFY(toggle->property("checked").toBool());
    QTRY_COMPARE(changedSpy.count(), 1);
    QVERIFY(accessibleToggle->state().checked);

    actionInterface->doAction(QAccessibleActionInterface::pressAction());
    QTRY_VERIFY(!backend.useLastScreenshotSaveLocation());
    QTRY_VERIFY(!toggle->property("checked").toBool());
    QTRY_COMPARE(changedSpy.count(), 2);
    QVERIFY(!accessibleToggle->state().checked);

    actionInterface->doAction(QAccessibleActionInterface::toggleAction());
    QTRY_VERIFY(backend.useLastScreenshotSaveLocation());
    QTRY_VERIFY(toggle->property("checked").toBool());
    QTRY_COMPARE(changedSpy.count(), 3);

    backend.setUseLastScreenshotSaveLocation(false);
    QTRY_VERIFY(!toggle->property("checked").toBool());
    backend.setUseLastScreenshotSaveLocation(true);
    QTRY_VERIFY(toggle->property("checked").toBool());
    QTRY_COMPARE(changedSpy.count(), 5);
    QCOMPARE(SnapTray::getSettings()
                 .value(QStringLiteral("files/useLastScreenshotSaveLocation"))
                 .toBool(),
             true);
}

QTEST_MAIN(tst_SettingsWindowFeatureGating)
#include "tst_SettingsWindowFeatureGating.moc"
