#include <QtTest>
#include <QAccessible>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

namespace {
QQuickItem* findByType(QQuickItem* item, const char* type)
{
    if (!item) return nullptr;
    if (item->inherits(type)) return item;
    for (QQuickItem* child : item->childItems())
        if (QQuickItem* found = findByType(child, type)) return found;
    return nullptr;
}

QList<QQuickItem*> delegatesOf(QQuickItem* list)
{
    QList<QQuickItem*> delegates;
    QQuickItem* content = list->property("contentItem").value<QQuickItem*>();
    if (!content) return delegates;
    for (QQuickItem* child : content->childItems())
        if (child->inherits("QQuickItemDelegate")) delegates.append(child);
    return delegates;
}
}

class TestSettingsCombo : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void everyOptionFitsWhateverIsSelected();
    void screenReadersHearValuesAndPermissions();
    void slidersReadTheirUnit();
};

void TestSettingsCombo::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) QSKIP("No screen available");
}

// The combo was sized to the selected text, and the popup to the combo, so
// with the shorter "Save right away" selected the popup clipped "Edit in the
// preview" to "Edit in the previe".
void TestSettingsCombo::everyOptionFitsWhateverIsSelected()
{
    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    QQmlComponent component(view.engine());
    component.setData(
        "import QtQuick\n"
        "import SnapTrayQml\n"
        "Item {\n"
        "  width: 600; height: 300\n"
        "  SettingsCombo {\n"
        "    objectName: 'combo'\n"
        "    width: parent.width\n"
        "    label: 'After recording'\n"
        "    model: [ { text: 'Edit in the preview', value: true },\n"
        "             { text: 'Save right away', value: false } ]\n"
        "    currentIndex: 1\n"
        "  }\n"
        "}\n", QUrl("qrc:/SnapTrayQml/controls/SettingsComboTestHost.qml"));
    QScopedPointer<QObject> created(component.create());
    QVERIFY2(created, qPrintable(component.errorString()));
    auto* root = qobject_cast<QQuickItem*>(created.data());
    root->setParentItem(view.contentItem());
    view.resize(600, 300);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QQuickItem* row = root->findChild<QQuickItem*>("combo");
    QVERIFY(row);
    QQuickItem* combo = findByType(row, "QQuickComboBox");
    QVERIFY(combo);

    // The closed combo shows each option without eliding.
    for (int index : {0, 1}) {
        row->setProperty("currentIndex", index);
        auto* text = combo->property("contentItem").value<QQuickItem*>();
        QVERIFY(text);
        QTRY_VERIFY2(text->implicitWidth() <= text->width(),
                     qPrintable(QStringLiteral("option %1: needs %2, has %3")
                                    .arg(index).arg(text->implicitWidth()).arg(text->width())));
    }

    // With the shorter option selected, the open popup still fits the longer one.
    row->setProperty("currentIndex", 1);
    QObject* popup = combo->property("popup").value<QObject*>();
    QVERIFY(popup);
    QMetaObject::invokeMethod(popup, "open");
    QTRY_VERIFY(popup->property("opened").toBool());
    QQuickItem* list = popup->property("contentItem").value<QQuickItem*>();
    QVERIFY(list);
    QList<QQuickItem*> delegates;
    QTRY_COMPARE((delegates = delegatesOf(list)).size(), 2);
    for (QQuickItem* delegate : delegates) {
        QVERIFY2(delegate->implicitWidth() <= list->width(),
                 qPrintable(QStringLiteral("delegate needs %1, list has %2")
                                .arg(delegate->implicitWidth()).arg(list->width())));
    }
    // #210: each option has its text as its accessible name (the font list
    // and every setting read as unnamed list items).
    QStringList names;
    for (QQuickItem* delegate : delegates) {
        QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(delegate);
        QVERIFY(accessible);
        names << accessible->text(QAccessible::Name);
    }
    names.sort();
    QCOMPARE(names, (QStringList{QStringLiteral("Edit in the preview"), QStringLiteral("Save right away")}));
}

// #238: VoiceOver heard only "After recording, menu button" (the shown text is
// a custom contentItem), and every permission row's button was "Open Settings"
// with the status only in colored text.
void TestSettingsCombo::screenReadersHearValuesAndPermissions()
{
    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    QQmlComponent component(view.engine());
    component.setData(
        "import QtQuick\n"
        "import SnapTrayQml\n"
        "Column {\n"
        "  width: 600; height: 300\n"
        "  SettingsCombo {\n"
        "    objectName: 'combo'\n"
        "    width: parent.width\n"
        "    label: 'After recording'\n"
        "    model: [ { text: 'Edit in the preview', value: true },\n"
        "             { text: 'Save right away', value: false } ]\n"
        "    currentIndex: 1\n"
        "  }\n"
        "  SettingsPermissionRow {\n"
        "    objectName: 'permission'\n"
        "    label: 'Screen Recording'\n"
        "    granted: false\n"
        "  }\n"
        "}\n", QUrl("qrc:/SnapTrayQml/controls/SettingsComboAccessibilityHost.qml"));
    QScopedPointer<QObject> created(component.create());
    QVERIFY2(created, qPrintable(component.errorString()));
    auto* root = qobject_cast<QQuickItem*>(created.data());
    root->setParentItem(view.contentItem());
    view.resize(600, 300);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QQuickItem* combo = findByType(root->findChild<QQuickItem*>("combo"), "QQuickComboBox");
    QVERIFY(combo);
    QAccessibleInterface* comboAccessible = QAccessible::queryAccessibleInterface(combo);
    QVERIFY(comboAccessible);
    QCOMPARE(comboAccessible->text(QAccessible::Name), QStringLiteral("After recording, Save right away"));
    combo->setProperty("currentIndex", 0);
    QTRY_COMPARE(comboAccessible->text(QAccessible::Name),
                 QStringLiteral("After recording, Edit in the preview"));

    QQuickItem* permission = root->findChild<QQuickItem*>("permission");
    QVERIFY(permission);
    QAccessibleInterface* button = nullptr;
    QList<QQuickItem*> pending{permission};
    while (!pending.isEmpty() && !button) {
        QQuickItem* node = pending.takeLast();
        pending.append(node->childItems());
        QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(node);
        if (accessible && accessible->role() == QAccessible::Button) button = accessible;
    }
    QVERIFY(button);
    QCOMPARE(button->text(QAccessible::Name), QStringLiteral("Open Settings, Screen Recording, Not Granted"));
    permission->setProperty("granted", true);
    QTRY_COMPARE(button->text(QAccessible::Name), QStringLiteral("Open Settings, Screen Recording, Granted"));
}

// #248: sliders read a bare number ("100"), not the value with its unit.
void TestSettingsCombo::slidersReadTheirUnit()
{
    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    QQmlComponent component(view.engine());
    component.setData(
        "import QtQuick\n"
        "import SnapTrayQml\n"
        "Item {\n"
        "  width: 600; height: 100\n"
        "  SettingsSlider {\n"
        "    objectName: 'slider'\n"
        "    width: parent.width\n"
        "    label: 'Default opacity'\n"
        "    from: 10; to: 100; value: 40; suffix: '%'\n"
        "  }\n"
        "}\n", QUrl("qrc:/SnapTrayQml/controls/SettingsSliderAccessibilityHost.qml"));
    QScopedPointer<QObject> created(component.create());
    QVERIFY2(created, qPrintable(component.errorString()));
    auto* root = qobject_cast<QQuickItem*>(created.data());
    root->setParentItem(view.contentItem());
    view.resize(600, 100);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QQuickItem* row = root->findChild<QQuickItem*>("slider");
    QVERIFY(row);
    QQuickItem* input = row->findChild<QQuickItem*>("settingsSliderInput");
    QVERIFY(input);
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(input);
    QVERIFY(accessible);
    QCOMPARE(accessible->role(), QAccessible::Slider);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Default opacity, 40%"));
    row->setProperty("value", 75);
    QTRY_COMPARE(accessible->text(QAccessible::Name), QStringLiteral("Default opacity, 75%"));
}

QTEST_MAIN(TestSettingsCombo)
#include "tst_SettingsCombo.moc"
