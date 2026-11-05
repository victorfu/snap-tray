#include <QtTest>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickView>
#include <QtQml/qqmlextensionplugin.h>

#include "qml/PinHoverBarViewModel.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

// #225: the pin hover bar and its value panel.
class tst_PinHoverBarQml : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(QRegularExpression(".*(ReferenceError|TypeError).*")); }
    void barShowsValuesAndNames();
    void compactHidesStatusAndAnnotate();
    void buttonsRaiseRequests();
    void zoomPanelPresetAndReset();
    void opacityPanelRangeAndReset();
    void panelControlsAreNamed();

private:
    static std::unique_ptr<QQuickView> load(const char* file, PinHoverBarViewModel* vm);
    static QQuickItem* find(QQuickView* view, const QString& name);
    static void click(QQuickView* view, QQuickItem* item);
};

std::unique_ptr<QQuickView> tst_PinHoverBarQml::load(const char* file, PinHoverBarViewModel* vm)
{
    auto view = std::make_unique<QQuickView>();
    view->setInitialProperties({{"viewModel", QVariant::fromValue(static_cast<QObject*>(vm))}});
    view->setResizeMode(QQuickView::SizeViewToRootObject);
    view->setSource(QUrl(QStringLiteral("qrc:/SnapTrayQml/pin/%1").arg(QLatin1String(file))));
    if (view->status() != QQuickView::Ready) {
        qWarning() << view->errors();
        return nullptr;
    }
    view->show();
    if (!QTest::qWaitForWindowExposed(view.get())) return nullptr;
    return view;
}

QQuickItem* tst_PinHoverBarQml::find(QQuickView* view, const QString& name)
{
    std::function<QQuickItem*(QQuickItem*)> walk = [&](QQuickItem* node) -> QQuickItem* {
        if (node->objectName() == name) return node;
        for (QQuickItem* child : node->childItems())
            if (QQuickItem* found = walk(child)) return found;
        return nullptr;
    };
    return walk(view->contentItem());
}

void tst_PinHoverBarQml::click(QQuickView* view, QQuickItem* item)
{
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier,
                      item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
}

void tst_PinHoverBarQml::barShowsValuesAndNames()
{
    PinHoverBarViewModel vm;
    vm.setValues(1.5, 0.1, 0.8);
    auto view = load("PinHoverBar.qml", &vm);
    QVERIFY(view);
    QQuickItem* zoom = find(view.get(), "pinHoverZoom");
    QQuickItem* opacity = find(view.get(), "pinHoverOpacity");
    QVERIFY(zoom && opacity);
    QCOMPARE(zoom->property("label").toString(), QStringLiteral("Zoom 150%"));
    QCOMPARE(opacity->property("label").toString(), QStringLiteral("Opacity 80%"));
    QVERIFY(find(view.get(), "pinHoverMore")->isVisible());
    QVERIFY(!find(view.get(), "pinHoverStatus")->isVisible());   // no Live
}

void tst_PinHoverBarQml::compactHidesStatusAndAnnotate()
{
    PinHoverBarViewModel vm;
    vm.setStatus(PinHoverBarViewModel::Live);
    auto view = load("PinHoverBar.qml", &vm);
    QVERIFY(view);
    QVERIFY(find(view.get(), "pinHoverStatus")->isVisible());
    QVERIFY(find(view.get(), "pinHoverAnnotate")->isVisible());
    vm.setAvailableWidth(PinHoverBarViewModel::kCompactWidth - 1);
    QTRY_VERIFY(!find(view.get(), "pinHoverStatus")->isVisible());
    QVERIFY(!find(view.get(), "pinHoverAnnotate")->isVisible());
    QVERIFY(find(view.get(), "pinHoverZoom")->isVisible());
    QVERIFY(find(view.get(), "pinHoverMore")->isVisible());
}

void tst_PinHoverBarQml::buttonsRaiseRequests()
{
    PinHoverBarViewModel vm;
    auto view = load("PinHoverBar.qml", &vm);
    QVERIFY(view);
    QSignalSpy more(&vm, &PinHoverBarViewModel::moreRequested);
    QSignalSpy annotate(&vm, &PinHoverBarViewModel::annotateRequested);
    click(view.get(), find(view.get(), "pinHoverZoom"));
    QCOMPARE(vm.panel(), int(PinHoverBarViewModel::ZoomPanel));
    click(view.get(), find(view.get(), "pinHoverAnnotate"));
    click(view.get(), find(view.get(), "pinHoverMore"));
    QCOMPARE(annotate.count(), 1);
    QCOMPARE(more.count(), 1);
}

void tst_PinHoverBarQml::zoomPanelPresetAndReset()
{
    PinHoverBarViewModel vm;
    vm.setValues(1.5, 0.1, 1.0);
    vm.openPanel(PinHoverBarViewModel::ZoomPanel);
    auto view = load("PinValuePanel.qml", &vm);
    QVERIFY(view);
    QSignalSpy zoom(&vm, &PinHoverBarViewModel::zoomRequested);
    // Every preset lies inside the panel.
    const QRectF panelRect(0, 0, view->width(), view->height());
    for (int p : {33, 50, 100, 200}) {
        QQuickItem* preset = find(view.get(), QStringLiteral("pinValuePreset_%1").arg(p));
        QVERIFY(panelRect.contains(preset->mapRectToScene(QRectF(0, 0, preset->width(), preset->height()))));
    }
    click(view.get(), find(view.get(), "pinValuePreset_200"));
    click(view.get(), find(view.get(), "pinValueReset"));
    QCOMPARE(zoom.count(), 2);
    QCOMPARE(zoom.at(0).at(0).toReal(), 2.0);
    QCOMPARE(zoom.at(1).at(0).toReal(), 1.0);
    QCOMPARE(find(view.get(), "pinValueSlider")->property("to").toReal(), 500.0);
}

void tst_PinHoverBarQml::opacityPanelRangeAndReset()
{
    PinHoverBarViewModel vm;
    vm.setValues(1.0, 0.1, 0.4);
    vm.openPanel(PinHoverBarViewModel::OpacityPanel);
    auto view = load("PinValuePanel.qml", &vm);
    QVERIFY(view);
    QQuickItem* slider = find(view.get(), "pinValueSlider");
    QCOMPARE(slider->property("from").toReal(), 10.0);
    QCOMPARE(slider->property("to").toReal(), 100.0);
    QCOMPARE(slider->property("value").toReal(), 40.0);
    QSignalSpy opacity(&vm, &PinHoverBarViewModel::opacityRequested);
    click(view.get(), find(view.get(), "pinValuePreset_50"));
    QCOMPARE(opacity.count(), 1);
    QCOMPARE(opacity.at(0).at(0).toReal(), 0.5);
    QVERIFY(!find(view.get(), "pinValuePreset_200"));
}

void tst_PinHoverBarQml::panelControlsAreNamed()
{
    PinHoverBarViewModel vm;
    vm.openPanel(PinHoverBarViewModel::ZoomPanel);
    auto view = load("PinValuePanel.qml", &vm);
    QVERIFY(view);
    for (const QString& name : {QStringLiteral("pinValueSlider"), QStringLiteral("pinValuePreset_33"),
                                QStringLiteral("pinValueReset")}) {
        QQuickItem* item = find(view.get(), name);
        QVERIFY2(item, qPrintable(name));
        const QString accessible = QQmlProperty(item, QStringLiteral("Accessible.name"), qmlContext(item)).read().toString();
        QVERIFY2(!accessible.isEmpty(), qPrintable(name));
    }
}

QTEST_MAIN(tst_PinHoverBarQml)
#include "tst_PinHoverBarQml.moc"
