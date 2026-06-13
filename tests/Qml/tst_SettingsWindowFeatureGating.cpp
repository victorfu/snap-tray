#include <QtTest/QtTest>

#include "qml/SettingsBackend.h"

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

class tst_SettingsWindowFeatureGating : public QObject
{
    Q_OBJECT

private slots:
    void sidebarModelHidesUnsupportedPages();
};

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

QTEST_MAIN(tst_SettingsWindowFeatureGating)
#include "tst_SettingsWindowFeatureGating.moc"
