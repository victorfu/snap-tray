#include <QtTest>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QtQml/qqmlextensionplugin.h>
#include "qml/PinToolbarViewModel.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

namespace {
QQuickItem* mouseArea(QObject* parent)
{
    for (QQuickItem* item : parent->findChildren<QQuickItem*>()) {
        if (item->metaObject()->indexOfProperty("cursorShape") >= 0) return item;
    }
    return nullptr;
}
}

class TestCursorTokens : public QObject
{
    Q_OBJECT
private slots:
    void moduleDeclaresSingleton_data();
    void moduleDeclaresSingleton();
    void settingsButtonCursorTracksEnabledState();
    void toolbarDragCursorTracksPress();
};

void TestCursorTokens::moduleDeclaresSingleton_data()
{
    QTest::addColumn<QByteArray>("importStatement");
    QTest::newRow("unversioned") << QByteArray("import SnapTrayQml");
    QTest::newRow("versioned") << QByteArray("import SnapTrayQml 1.0");
}

void TestCursorTokens::moduleDeclaresSingleton()
{
    QFETCH(QByteArray, importStatement);
    QFile qmldir(":/SnapTrayQml/qmldir");
    QVERIFY(qmldir.open(QIODevice::ReadOnly));
    QVERIFY(qmldir.readAll().contains("singleton CursorTokens 1.0 tokens/CursorTokens.qml"));
    QQmlEngine engine;
    engine.addImportPath("qrc:/"); // Match QmlOverlayManager's module resource root.
    QQmlComponent component(&engine);
    component.setData("import QtQuick\n" + importStatement + "\nQtObject { property QtObject tokens: CursorTokens }",
                      QUrl("qrc:/cursor-token-test.qml"));
    QScopedPointer<QObject> first(component.create());
    QScopedPointer<QObject> second(component.create());
    QVERIFY2(first && second, qPrintable(component.errorString()));
    QObject* tokens = first->property("tokens").value<QObject*>();
    QVERIFY(tokens);
    QCOMPARE(tokens, second->property("tokens").value<QObject*>());
    QCOMPARE(tokens->property("clickable").toInt(), int(Qt::PointingHandCursor));
    QCOMPARE(tokens->property("panelDragActive").toInt(), int(Qt::ClosedHandCursor));
}

void TestCursorTokens::settingsButtonCursorTracksEnabledState()
{
    QQmlEngine engine;
    engine.addImportPath("qrc:/");
    // Load the actual resource component so its generated QML cache is exercised.
    QQmlComponent component(&engine, QUrl("qrc:/SnapTrayQml/controls/SettingsButton.qml"));
    QScopedPointer<QObject> button(component.create());
    QVERIFY2(button, qPrintable(component.errorString()));
    QQuickItem* area = mouseArea(button.get());
    QVERIFY(area);
    QCOMPARE(area->property("cursorShape").toInt(), int(Qt::PointingHandCursor));
    button->setProperty("enabled", false);
    QCOMPARE(area->property("cursorShape").toInt(), int(Qt::ArrowCursor));
    button->setProperty("enabled", true);
    QCOMPARE(area->property("cursorShape").toInt(), int(Qt::PointingHandCursor));
}

void TestCursorTokens::toolbarDragCursorTracksPress()
{
    PinToolbarViewModel model;
    QQuickView view;
    view.engine()->addImportPath("qrc:/");
    view.setInitialProperties({{"viewModel", QVariant::fromValue(&model)}});
    view.setSource(QUrl("qrc:/SnapTrayQml/toolbar/FloatingToolbar.qml"));
    QCOMPARE(view.status(), QQuickView::Ready);
    view.show();
    QQuickItem* handle = view.rootObject()->findChild<QQuickItem*>("toolbarDragHandle");
    QVERIFY(handle);
    QQuickItem* area = mouseArea(handle);
    QVERIFY(area);
    QCOMPARE(area->property("cursorShape").toInt(), int(Qt::ArrowCursor));
    const QPoint point = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2)).toPoint();
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, point);
    QTRY_VERIFY(area->property("pressed").toBool());
    QCOMPARE(area->property("cursorShape").toInt(), int(Qt::ClosedHandCursor));
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, point);
    QCOMPARE(area->property("cursorShape").toInt(), int(Qt::ArrowCursor));
}

QTEST_MAIN(TestCursorTokens)
#include "tst_CursorTokens.moc"
