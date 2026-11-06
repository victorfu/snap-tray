#include <QtTest/QtTest>

#include "qml/SettingsBackend.h"
#include "settings/Settings.h"
#include "settings/RecordingSettingsManager.h"
#include "settings/PinWindowSettingsManager.h"
#include "settings/RegionCaptureSettingsManager.h"
#include <QJSValue>
#include <QQmlExpression>
#include <QQuickView>

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
    void sidebarArrowKeysCrossTheDivider();
    void resetSettingRestoresManagerDefault();
    void resetButtonShowsOnlyWhenChanged();
    void searchFindsSettingsSectionsAndHotkeys();
    void searchResultRevealsTheRow();
    void longLabelWordWidensTheLabelColumn();
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

    // The stack must show the selected page's loader, not just load it:
    // stackIndexForKey depends on the loaders following pageKeys.
    const QStringList allKeys = root->property("pageKeys").toStringList();
    QCOMPARE(allKeys.size(), 9);
    const auto onlyVisible = [&loader, &allKeys](const QString& key) {
        for (const QString& other : allKeys) {
            if (loader(other)->property("visible").toBool() != (other == key))
                return false;
        }
        return true;
    };
    QStringList offered = {QStringLiteral("general"), QStringLiteral("capture"), QStringLiteral("pins"),
                           QStringLiteral("saving"), QStringLiteral("hotkeys"), QStringLiteral("updates"),
                           QStringLiteral("about")};
    if (backend.recordingSupported())
        offered << QStringLiteral("recording");
    if (backend.ocrSettingsVisible())
        offered << QStringLiteral("ocr");
    for (const QString& key : offered) {
        QVariant opened;
        QVERIFY(QMetaObject::invokeMethod(root.get(), "selectPage", Q_RETURN_ARG(QVariant, opened),
                                          Q_ARG(QVariant, key)));
        QVERIFY2(opened.toBool(), qPrintable(key));
        QTRY_VERIFY2(loader(key)->property("item").value<QObject*>() != nullptr, qPrintable(key));
        QTRY_VERIFY2(onlyVisible(key), qPrintable(key));
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

    // Text Recognition is hidden on Linux beta; it must not load there either.
    QVERIFY(QMetaObject::invokeMethod(root.get(), "selectPage", Q_RETURN_ARG(QVariant, opened),
                                      Q_ARG(QVariant, QStringLiteral("ocr"))));
    QCOMPARE(opened.toBool(), backend.ocrSettingsVisible());
    QCOMPARE(loader(QStringLiteral("ocr"))->property("active").toBool(), backend.ocrSettingsVisible());
}

namespace {

QQuickItem* findItem(QQuickItem* node, const QString& name)
{
    if (!node)
        return nullptr;
    if (node->objectName() == name)
        return node;
    for (QQuickItem* child : node->childItems()) {
        if (QQuickItem* found = findItem(child, name))
            return found;
    }
    return nullptr;
}

} // namespace

void tst_SettingsWindowFeatureGating::resetSettingRestoresManagerDefault()
{
    // #227: every key in the reset table names a writable, notifying property,
    // and resetting writes the manager default through that property.
    SnapTray::SettingsBackend backend;
    const int savedZoom = backend.pinZoomStep();
    const bool savedHints = backend.shortcutHintsEnabled();
    const int savedCleanup = backend.recordingProjectCleanupDays();
    auto restore = qScopeGuard([&] {
        backend.setPinZoomStep(savedZoom);
        backend.setShortcutHintsEnabled(savedHints);
        backend.setRecordingProjectCleanupDays(savedCleanup);
    });

    for (const QString& key : {QStringLiteral("pinZoomStep"), QStringLiteral("shortcutHintsEnabled"),
                               QStringLiteral("recordingProjectCleanupDays"), QStringLiteral("filenameTemplate"),
                               QStringLiteral("watermarkOpacity"), QStringLiteral("checkFrequencyHours")}) {
        QVERIFY2(backend.canResetSetting(key), qPrintable(key));
    }
    QVERIFY(!backend.canResetSetting(QStringLiteral("language"))); // applies after restart: no reset
    QVERIFY(!backend.canResetSetting(QStringLiteral("noSuchSetting")));

    QSignalSpy changed(&backend, &SnapTray::SettingsBackend::changedSettingsChanged);
    backend.setPinZoomStep(PinWindowSettingsManager::kDefaultZoomStep * 100 + 7);
    QVERIFY(backend.changedSettings().contains(QStringLiteral("pinZoomStep")));
    QVERIFY(changed.count() >= 1);
    backend.resetSetting(QStringLiteral("pinZoomStep"));
    QCOMPARE(backend.pinZoomStep(), qRound(PinWindowSettingsManager::kDefaultZoomStep * 100));
    QVERIFY(!backend.changedSettings().contains(QStringLiteral("pinZoomStep")));
    QCOMPARE(qRound(PinWindowSettingsManager::instance().loadZoomStep() * 100), backend.pinZoomStep());

    backend.setShortcutHintsEnabled(!RegionCaptureSettingsManager::kDefaultShortcutHintsEnabled);
    QVERIFY(backend.changedSettings().contains(QStringLiteral("shortcutHintsEnabled")));
    backend.resetSetting(QStringLiteral("shortcutHintsEnabled"));
    QCOMPARE(backend.shortcutHintsEnabled(), RegionCaptureSettingsManager::kDefaultShortcutHintsEnabled);

    backend.setRecordingProjectCleanupDays(30);
    backend.resetSetting(QStringLiteral("recordingProjectCleanupDays"));
    QCOMPARE(backend.recordingProjectCleanupDays(), RecordingSettingsManager::kDefaultProjectCleanupDays);
}

void tst_SettingsWindowFeatureGating::resetButtonShowsOnlyWhenChanged()
{
    QQmlEngine engine;
    SnapTray::SettingsBackend backend;
    const int saved = backend.pinOpacityStep();
    auto restore = qScopeGuard([&backend, saved] { backend.setPinOpacityStep(saved); });
    backend.resetSetting(QStringLiteral("pinOpacityStep"));
    engine.rootContext()->setContextProperty(QStringLiteral("settingsBackend"), &backend);
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/SnapTrayQml/settings/PinWindowSettings.qml")));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root != nullptr, qPrintable(component.errorString()));
    auto* row = root->findChild<QQuickItem*>(QStringLiteral("pinOpacityStepSlider"));
    QVERIFY(row);
    auto* reset = findItem(row, QStringLiteral("settingsResetButton"));
    QVERIFY(reset);
    QVERIFY(!reset->isVisible());

    backend.setPinOpacityStep(17);
    QTRY_VERIFY(reset->isVisible());
    QVERIFY(QMetaObject::invokeMethod(reset, [reset] {
        QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(reset);
        QVERIFY(iface && iface->actionInterface());
        iface->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    }));
    QTRY_COMPARE(backend.pinOpacityStep(), qRound(PinWindowSettingsManager::kDefaultOpacityStep * 100));
    QTRY_VERIFY(!reset->isVisible());
}

void tst_SettingsWindowFeatureGating::searchFindsSettingsSectionsAndHotkeys()
{
    QQmlEngine engine;
    SnapTray::SettingsBackend backend;
    engine.rootContext()->setContextProperty(QStringLiteral("settingsBackend"), &backend);
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/SnapTrayQml/settings/SettingsSidebar.qml")));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root != nullptr, qPrintable(component.errorString()));
    auto* input = root->findChild<QObject*>(QStringLiteral("settingsSearchInput"));
    QVERIFY(input);

    const auto results = [&](const QString& query) {
        input->setProperty("text", query);
        QList<QVariantMap> out;
        // Read the var property in its QML context; QObject::property() gives
        // an engine-less copy of the JS array.
        QQmlExpression expression(engine.contextForObject(root.get()), root.get(), QStringLiteral("searchResults"));
        const QVariantList list = expression.evaluate().value<QJSValue>().toVariant().toList();
        for (const QVariant& v : list)
            out << v.toMap();
        return out;
    };
    const auto hasTarget = [](const QList<QVariantMap>& list, const QString& page, const QString& target) {
        for (const auto& r : list) {
            if (r.value(QStringLiteral("page")) == page && r.value(QStringLiteral("target")) == target)
                return true;
        }
        return false;
    };

    // By label, any case. The first query after an empty field must already
    // give results (the "searching" binding can lag behind onQueryChanged).
    QVERIFY(hasTarget(results(QStringLiteral("zoom STEP")), QStringLiteral("pins"), QStringLiteral("pinZoomStep")));
    // By section: every watermark setting, under Saving.
    const auto watermark = results(QStringLiteral("watermark"));
    QVERIFY(hasTarget(watermark, QStringLiteral("saving"), QStringLiteral("watermarkEnabled")));
    QVERIFY(hasTarget(watermark, QStringLiteral("saving"), QStringLiteral("watermarkPosition")));
    // A moved setting by its new name, and pages by name.
    QVERIFY(hasTarget(results(QStringLiteral("kept in library")), QStringLiteral("saving"), QStringLiteral("pinMaxCacheFiles")));
    QVERIFY(hasTarget(results(QStringLiteral("pin windows")), QStringLiteral("pins"), QString()));
    // Hotkeys by action name (HotkeyManager is not initialised in this test
    // unless another suite did; the app always has them).
    const QVariantList captureHotkeys = backend.hotkeysForCategory(0);
    if (!captureHotkeys.isEmpty()) {
        const QString name = captureHotkeys.first().toMap().value(QStringLiteral("name")).toString();
        QVERIFY(hasTarget(results(name), QStringLiteral("hotkeys"), QString()));
    }
    // Unsupported pages never appear.
    if (!backend.recordingSupported())
        QVERIFY(!hasTarget(results(QStringLiteral("frame rate")), QStringLiteral("recording"), QStringLiteral("recordingFrameRate")));
    // Nothing matches: empty list and a message.
    QVERIFY(results(QStringLiteral("zzzz no such thing")).isEmpty());
    auto* empty = findItem(qobject_cast<QQuickItem*>(root.get()), QStringLiteral("settingsSearchEmpty"));
    QVERIFY(empty && empty->isVisible());

    // Opening a result asks for the page and row.
    QSignalSpy requested(root.get(), SIGNAL(settingRequested(QString,QString)));
    results(QStringLiteral("blur intensity"));
    QVERIFY(QMetaObject::invokeMethod(root.get(), "openResult", Q_ARG(QVariant, 0)));
    QCOMPARE(requested.count(), 1);
    QCOMPARE(requested.at(0).at(0).toString(), QStringLiteral("capture"));
    QCOMPARE(requested.at(0).at(1).toString(), QStringLiteral("blurIntensity"));
    QCOMPARE(root->property("currentKey").toString(), QStringLiteral("capture"));
}

void tst_SettingsWindowFeatureGating::searchResultRevealsTheRow()
{
    SnapTray::SettingsBackend backend;
    QQuickView view;
    view.rootContext()->setContextProperty(QStringLiteral("settingsBackend"), &backend);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setSource(QUrl(QStringLiteral("qrc:/SnapTrayQml/settings/SettingsWindow.qml")));
    QCOMPARE(view.status(), QQuickView::Ready);
    view.resize(900, 560);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QObject* root = view.rootObject();

    QVariant ok;
    QVERIFY(QMetaObject::invokeMethod(root, "revealSetting", Q_RETURN_ARG(QVariant, ok),
                                      Q_ARG(QVariant, QStringLiteral("saving")),
                                      Q_ARG(QVariant, QStringLiteral("pinMaxCacheFiles"))));
    QVERIFY(ok.toBool());
    auto* loader = root->findChild<QObject*>(QStringLiteral("settingsPageLoader_saving"));
    QVERIFY(loader);
    QObject* flick = nullptr;
    QTRY_VERIFY((flick = loader->property("item").value<QObject*>()) != nullptr);
    QTRY_VERIFY(flick->property("contentY").toReal() > 0); // the Library row is far down
    auto* row = flick->findChild<QQuickItem*>(QStringLiteral("libraryMaxScreenshotsSlider"));
    QVERIFY(row);
    QTRY_VERIFY(row->property("searchHighlight").toBool());
    QTRY_VERIFY_WITH_TIMEOUT(!row->property("searchHighlight").toBool(), 4000);
    // The row is inside the visible part of the page.
    const qreal y = row->mapToItem(qobject_cast<QQuickItem*>(flick), QPointF(0, 0)).y();
    QVERIFY(y >= 0 && y + row->height() <= flick->property("height").toReal());

    QVERIFY(QMetaObject::invokeMethod(root, "revealSetting", Q_RETURN_ARG(QVariant, ok),
                                      Q_ARG(QVariant, QStringLiteral("advanced")), Q_ARG(QVariant, QString())));
    QVERIFY(!ok.toBool());
}

void tst_SettingsWindowFeatureGating::longLabelWordWidensTheLabelColumn()
{
    // #227: a single long word (German compounds) widens the label column
    // instead of breaking mid-word, up to a share of the row.
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import SnapTrayQml
        Column {
            width: 700
            property alias shortRow: shortRow
            property alias longRow: longRow
            property alias hugeRow: hugeRow
            SettingsRow { id: shortRow; label: "Zoom step" }
            SettingsRow { id: longRow; label: "Weichzeichnungsintensität" }
            SettingsRow { id: hugeRow; label: "Donaudampfschifffahrtsgesellschaftskapitänswitwenrentenversicherung" }
        }
    )", QUrl(QStringLiteral("qrc:/SnapTrayQml/controls/LabelWidthTest.qml")));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root != nullptr, qPrintable(component.errorString()));
    auto* shortRow = root->property("shortRow").value<QObject*>();
    auto* longRow = root->property("longRow").value<QObject*>();
    auto* hugeRow = root->property("hugeRow").value<QObject*>();
    QCOMPARE(shortRow->property("effectiveLabelWidth").toInt(), 140);
    QVERIFY(longRow->property("effectiveLabelWidth").toInt() > 140);
    QVERIFY(longRow->property("effectiveLabelWidth").toInt() < 700 * 0.45);
    QCOMPARE(hugeRow->property("effectiveLabelWidth").toInt(), int(700 * 0.45)); // capped; then it wraps
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

void tst_SettingsWindowFeatureGating::sidebarArrowKeysCrossTheDivider()
{
    // Down from Hotkeys opens Updates past the divider; Up comes back.
    SnapTray::SettingsBackend backend;
    QQuickView view;
    view.rootContext()->setContextProperty(QStringLiteral("settingsBackend"), &backend);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setSource(QUrl(QStringLiteral("qrc:/SnapTrayQml/settings/SettingsWindow.qml")));
    QCOMPARE(view.status(), QQuickView::Ready);
    view.resize(900, 560);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    view.requestActivate();
    QTest::qWaitForWindowActive(&view);
    QQuickItem* root = view.rootObject();

    const auto pageShown = [root](const QString& key) {
        auto* loader = root->findChild<QObject*>(QStringLiteral("settingsPageLoader_") + key);
        return loader && loader->property("visible").toBool();
    };
    QVariant opened;
    QVERIFY(QMetaObject::invokeMethod(root, "selectPage", Q_RETURN_ARG(QVariant, opened),
                                      Q_ARG(QVariant, QStringLiteral("hotkeys"))));
    QVERIFY(opened.toBool());
    QQuickItem* hotkeys = nullptr;
    QTRY_VERIFY((hotkeys = findItem(root, QStringLiteral("settingsSidebarItem_hotkeys"))) != nullptr);
    hotkeys->forceActiveFocus();
    QTRY_VERIFY(hotkeys->hasActiveFocus());

    QTest::keyClick(&view, Qt::Key_Down);
    QTRY_VERIFY(pageShown(QStringLiteral("updates")));
    QQuickItem* updates = findItem(root, QStringLiteral("settingsSidebarItem_updates"));
    QVERIFY(updates);
    QTRY_VERIFY(updates->hasActiveFocus());

    QTest::keyClick(&view, Qt::Key_Up);
    QTRY_VERIFY(pageShown(QStringLiteral("hotkeys")));
    QTRY_VERIFY(hotkeys->hasActiveFocus());
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
