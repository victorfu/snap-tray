#include <QtTest/QtTest>

#include "qml/SettingsBackend.h"
#include "settings/Settings.h"
#include "settings/RecordingSettingsManager.h"

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

    void cliOperationCompletionRestoresControls();
    void sidebarModelHidesUnsupportedPages();
    void sidebarGroupsPagesByTask();
    void taskPagesHoldTheFormerSettings_data();
    void taskPagesHoldTheFormerSettings();
    void libraryLimitSliderFollowsTheBackend();
    void settingsWindowOpensPagesByKey();
    void filesPageShowsRememberLastFolderOnAllPlatforms();
    void recordingModeFollowsTheSetup();

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

    QCOMPARE(keys.contains(QStringLiteral("ocr")), backend.ocrSettingsVisible());
    QCOMPARE(keys.contains(QStringLiteral("recording")), backend.recordingSupported());
}

void tst_SettingsWindowFeatureGating::sidebarGroupsPagesByTask()
{
    // #227: task pages first, then Updates and About after a divider. The
    // former Advanced, Files and Watermark pages are gone.
    QQmlEngine engine;
    SnapTray::SettingsBackend backend;
    engine.rootContext()->setContextProperty(QStringLiteral("settingsBackend"), &backend);
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/SnapTrayQml/settings/SettingsSidebar.qml")));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root != nullptr, qPrintable(component.errorString()));

    QStringList expected = {QStringLiteral("general"), QStringLiteral("capture"), QStringLiteral("pins")};
    if (backend.recordingSupported()) expected << QStringLiteral("recording");
    expected << QStringLiteral("saving");
    if (backend.ocrSettingsVisible()) expected << QStringLiteral("ocr");
    expected << QStringLiteral("hotkeys") << QStringLiteral("updates") << QStringLiteral("about");

    QStringList keys;
    QStringList labels;
    QList<int> groups;
    for (const QVariant& item : root->property("pages").toList()) {
        const QVariantMap page = item.toMap();
        keys << page.value(QStringLiteral("key")).toString();
        labels << page.value(QStringLiteral("label")).toString();
        groups << page.value(QStringLiteral("group")).toInt();
    }
    QCOMPARE(keys, expected);
    for (int i = 0; i < keys.size(); ++i) {
        const bool appGroup = keys.at(i) == QStringLiteral("updates") || keys.at(i) == QStringLiteral("about");
        QCOMPARE(groups.at(i), appGroup ? 1 : 0);
    }
    if (backend.ocrSettingsVisible()) {
        QCOMPARE(labels.at(keys.indexOf(QStringLiteral("ocr"))), QStringLiteral("Text Recognition"));
    }
    QCOMPARE(labels.at(keys.indexOf(QStringLiteral("pins"))), QStringLiteral("Pin Windows"));
}

void tst_SettingsWindowFeatureGating::taskPagesHoldTheFormerSettings_data()
{
    QTest::addColumn<QString>("page");
    QTest::addColumn<QStringList>("controls");
    QTest::newRow("capture") << QStringLiteral("CaptureSettings")
                             << QStringList{QStringLiteral("fullScreenCaptureActionCombo"),
                                            QStringLiteral("cursorCompanionCombo"),
                                            QStringLiteral("shortcutHintsToggle"),
                                            QStringLiteral("blurTypeCombo"),
                                            QStringLiteral("blurIntensitySlider")};
    QTest::newRow("pins") << QStringLiteral("PinWindowSettings")
                          << QStringList{QStringLiteral("pinDefaultOpacitySlider"),
                                         QStringLiteral("pinHoverControlsToggle"),
                                         QStringLiteral("pinOpacityStepSlider"),
                                         QStringLiteral("pinZoomStepSlider")};
    QTest::newRow("saving") << QStringLiteral("SavingSettings")
                            << QStringList{QStringLiteral("screenshotPathPicker"),
                                           QStringLiteral("rememberLastFolderToggle"),
                                           QStringLiteral("watermarkImagesToggle"),
                                           QStringLiteral("libraryMaxScreenshotsSlider")};
}

void tst_SettingsWindowFeatureGating::taskPagesHoldTheFormerSettings()
{
    QFETCH(QString, page);
    QFETCH(QStringList, controls);
    QQmlEngine engine;
    SnapTray::SettingsBackend backend;
    engine.rootContext()->setContextProperty(QStringLiteral("settingsBackend"), &backend);
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/SnapTrayQml/settings/%1.qml").arg(page)));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root != nullptr, qPrintable(component.errorString()));
    auto* title = root->findChild<QObject*>(QStringLiteral("settingsPageTitle"));
    QVERIFY2(title && !title->property("text").toString().isEmpty(), "every task page starts with a title");
    for (const QString& name : controls) {
        auto* control = root->findChild<QQuickItem*>(name);
        QVERIFY2(control != nullptr, qPrintable(name));
        QVERIFY2(control->isVisible(), qPrintable(name));
    }
}

void tst_SettingsWindowFeatureGating::libraryLimitSliderFollowsTheBackend()
{
    // "Max history entries" moved from Advanced to Saving > Library as
    // "Screenshots kept in Library": the same PinWindowSettingsManager value.
    QQmlEngine engine;
    SnapTray::SettingsBackend backend;
    const int saved = backend.pinMaxCacheFiles();
    auto restore = qScopeGuard([&backend, saved] { backend.setPinMaxCacheFiles(saved); });
    engine.rootContext()->setContextProperty(QStringLiteral("settingsBackend"), &backend);
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/SnapTrayQml/settings/SavingSettings.qml")));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root != nullptr, qPrintable(component.errorString()));
    auto* row = root->findChild<QObject*>(QStringLiteral("libraryMaxScreenshotsSlider"));
    QVERIFY(row);
    QCOMPARE(row->property("label").toString(), QStringLiteral("Screenshots kept in Library"));
    QCOMPARE(row->property("value").toInt(), saved);
    auto* slider = row->findChild<QObject*>(QStringLiteral("settingsSliderInput"));
    QVERIFY(slider);
    const int moved = saved == 42 ? 43 : 42;
    slider->setProperty("value", moved);
    QVERIFY(QMetaObject::invokeMethod(slider, "moved", Qt::DirectConnection));
    QCOMPARE(backend.pinMaxCacheFiles(), moved);
}

void tst_SettingsWindowFeatureGating::settingsWindowOpensPagesByKey()
{
    QQmlEngine engine;
    SnapTray::SettingsBackend backend;
    engine.rootContext()->setContextProperty(QStringLiteral("settingsBackend"), &backend);
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/SnapTrayQml/settings/SettingsWindow.qml")));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root != nullptr, qPrintable(component.errorString()));

    const auto loader = [&root](const QString& key) {
        return root->findChild<QObject*>(QStringLiteral("settingsPageLoader_") + key);
    };
    QVERIFY(loader(QStringLiteral("general")));
    QVERIFY(loader(QStringLiteral("general"))->property("active").toBool());
    QVERIFY(!loader(QStringLiteral("saving"))->property("active").toBool()); // loaded on first selection

    for (const QString& key : {QStringLiteral("capture"), QStringLiteral("pins"), QStringLiteral("saving"),
                               QStringLiteral("hotkeys")}) {
        QVariant opened;
        QVERIFY(QMetaObject::invokeMethod(root.get(), "selectPage", Q_RETURN_ARG(QVariant, opened),
                                          Q_ARG(QVariant, key)));
        QVERIFY2(opened.toBool(), qPrintable(key));
        QTRY_VERIFY2(loader(key)->property("item").value<QObject*>() != nullptr, qPrintable(key));
    }

    for (const QString& removed : {QStringLiteral("advanced"), QStringLiteral("files"), QStringLiteral("watermark")}) {
        QVariant opened;
        QVERIFY(QMetaObject::invokeMethod(root.get(), "selectPage", Q_RETURN_ARG(QVariant, opened),
                                          Q_ARG(QVariant, removed)));
        QVERIFY2(!opened.toBool(), qPrintable(removed));
    }

    QVariant opened;
    QVERIFY(QMetaObject::invokeMethod(root.get(), "selectPage", Q_RETURN_ARG(QVariant, opened),
                                      Q_ARG(QVariant, QStringLiteral("recording"))));
    QCOMPARE(opened.toBool(), backend.recordingSupported());
    QCOMPARE(loader(QStringLiteral("recording"))->property("active").toBool(), backend.recordingSupported());
}

void tst_SettingsWindowFeatureGating::filesPageShowsRememberLastFolderOnAllPlatforms()
{
    QQmlEngine engine;
    SnapTray::SettingsBackend backend;
    backend.setUseLastScreenshotSaveLocation(true);
    engine.rootContext()->setContextProperty(QStringLiteral("settingsBackend"), &backend);

    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/SnapTrayQml/settings/SavingSettings.qml")));
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

void tst_SettingsWindowFeatureGating::recordingModeFollowsTheSetup()
{
    // #193: the recording setup can switch the mode while the (kept)
    // settings window is closed; showing it again re-reads it.
    auto& recording = RecordingSettingsManager::instance();
    const bool before = recording.showPreview();
    auto restore = qScopeGuard([&recording, before] { recording.setShowPreview(before); });
    recording.setShowPreview(true);
    SnapTray::SettingsBackend backend;
    if (backend.recordingDirectMp4Only()) QSKIP("This platform records direct MP4 only");
    QVERIFY(backend.recordingShowPreview());
    QSignalSpy changed(&backend, &SnapTray::SettingsBackend::recordingShowPreviewChanged);
    recording.setShowPreview(false); // what the setup does
    backend.refreshRecordingMode();
    QCOMPARE(changed.count(), 1);
    QVERIFY(!backend.recordingShowPreview());
    backend.refreshRecordingMode(); // unchanged: no signal
    QCOMPARE(changed.count(), 1);
}

QTEST_MAIN(tst_SettingsWindowFeatureGating)
#include "tst_SettingsWindowFeatureGating.moc"

void tst_SettingsWindowFeatureGating::cliOperationCompletionRestoresControls()
{
    QQmlEngine engine;
    SnapTray::SettingsBackend backend;
    engine.rootContext()->setContextProperty(QStringLiteral("settingsBackend"), &backend);
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/SnapTrayQml/settings/GeneralSettings.qml")));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root != nullptr, qPrintable(component.errorString()));
    auto* row = root->findChild<QObject*>(QStringLiteral("cliInstallationRow"));
    auto* error = root->findChild<QObject*>(QStringLiteral("cliInstallationError"));
    QVERIFY(row && error);
    QVERIFY(row->setProperty("busy", true));
    emit backend.cliOperationFinished(false, QStringLiteral("Permission denied"));
    QVERIFY(!row->property("busy").toBool());
    QCOMPARE(error->property("text").toString(), QStringLiteral("Permission denied"));
    QVERIFY(row->setProperty("busy", true));
    emit backend.cliOperationFinished(true, QString());
    QVERIFY(!row->property("busy").toBool());
    QVERIFY(error->property("text").toString().isEmpty());
}
