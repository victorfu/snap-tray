#include <QtTest/QtTest>

#include <QAccessible>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QtQml/qqmlextensionplugin.h>

#include "annotations/AnnotationStyleLabels.h"
#include "annotations/AnnotationZOrder.h"
#include "annotations/ArrowAnnotation.h"
#include "qml/PinToolOptionsViewModel.h"
#include "tools/ToolId.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

// #248: the style strip under the capture toolbar (sizes, colors, width, arrow
// and line styles, shape, text formatting, Auto Blur) exposed no names or
// states, so VoiceOver could neither tell what a control does nor which one is
// chosen.
class tst_ToolOptionsAccessibility : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void everyStyleControlIsNamed_data();
    void everyStyleControlIsNamed();
    void stepBadgeSizesAreNamedChoices();
    void colorsAreNamedChoices();
    void paletteNamesStayWithTheirColors();
    void widthIsAnAdjustableSlider();
    void styleMenusReadTheCurrentStyle();
    void textAndShapeTogglesAreCheckboxes();
    void arrangeButtonsAreNamedAndRequestMoves();

private:
    std::unique_ptr<QQuickView> load(PinToolOptionsViewModel& viewModel);
    static QList<QAccessibleInterface*> accessibles(QQuickItem* root);
    static QAccessibleInterface* named(QQuickItem* root, const QString& name);
};

void tst_ToolOptionsAccessibility::initTestCase()
{
    if (QGuiApplication::screens().isEmpty())
        QSKIP("The style strip needs a screen");
}

std::unique_ptr<QQuickView> tst_ToolOptionsAccessibility::load(PinToolOptionsViewModel& viewModel)
{
    auto view = std::make_unique<QQuickView>();
    view->rootContext()->setContextProperty(QStringLiteral("pinToolOptionsViewModel"), &viewModel);
    view->setResizeMode(QQuickView::SizeViewToRootObject);
    view->setSource(QUrl(QStringLiteral("qrc:/SnapTrayQml/toolbar/ToolOptionsStrip.qml")));
    if (view->status() != QQuickView::Ready) return nullptr;
    view->show();
    if (!QTest::qWaitForWindowExposed(view.get())) return nullptr;
    return view;
}

QList<QAccessibleInterface*> tst_ToolOptionsAccessibility::accessibles(QQuickItem* root)
{
    QList<QAccessibleInterface*> result;
    QList<QQuickItem*> pending{root};
    while (!pending.isEmpty()) {
        QQuickItem* node = pending.takeLast();
        if (!node->isVisible()) continue;
        pending.append(node->childItems());
        if (QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(node))
            result << accessible;
    }
    return result;
}

QAccessibleInterface* tst_ToolOptionsAccessibility::named(QQuickItem* root, const QString& name)
{
    for (QAccessibleInterface* accessible : accessibles(root)) {
        if (accessible->text(QAccessible::Name) == name) return accessible;
    }
    return nullptr;
}

void tst_ToolOptionsAccessibility::everyStyleControlIsNamed_data()
{
    QTest::addColumn<int>("tool");
    for (ToolId tool : {ToolId::Arrow, ToolId::Polyline, ToolId::Pencil, ToolId::Marker, ToolId::Shape,
                        ToolId::Text, ToolId::Mosaic, ToolId::StepBadge})
        QTest::newRow(qPrintable(QString::number(int(tool)))) << int(tool);
}

// Every clickable control in the strip (each has a MouseArea for the left
// button) is an accessible element with a role and a name.
void tst_ToolOptionsAccessibility::everyStyleControlIsNamed()
{
    QFETCH(int, tool);
    PinToolOptionsViewModel viewModel;
    viewModel.setAutoBlurEnabled(true);
    viewModel.showForTool(tool);
    auto view = load(viewModel);
    QVERIFY(view);

    QStringList unnamed;
    int controls = 0;
    QList<QQuickItem*> pending{view->rootObject()};
    while (!pending.isEmpty()) {
        QQuickItem* node = pending.takeLast();
        if (!node->isVisible()) continue;
        pending.append(node->childItems());
        if (QString::fromLatin1(node->metaObject()->className()) != QLatin1String("QQuickMouseArea"))
            continue;
        const auto buttons = Qt::MouseButtons(node->property("acceptedButtons").toInt());
        if (!(buttons & Qt::LeftButton) || !node->isEnabled()) continue;
        ++controls;
        QQuickItem* control = node->parentItem();
        QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(control);
        const QString name = accessible ? accessible->text(QAccessible::Name).trimmed() : QString();
        if (!accessible || accessible->role() == QAccessible::Client || name.isEmpty())
            unnamed << QStringLiteral("%1 (%2)").arg(control->objectName(),
                                                     QString::fromLatin1(control->metaObject()->className()));
    }
    QVERIFY(controls > 0);
    QVERIFY2(unnamed.isEmpty(), qPrintable(unnamed.join(QLatin1String(", "))));
}

void tst_ToolOptionsAccessibility::stepBadgeSizesAreNamedChoices()
{
    PinToolOptionsViewModel viewModel;
    viewModel.showForTool(int(ToolId::StepBadge));
    auto view = load(viewModel);
    QVERIFY(view);
    QAccessibleInterface* small = named(view->rootObject(), QStringLiteral("Small"));
    QAccessibleInterface* medium = named(view->rootObject(), QStringLiteral("Medium"));
    QAccessibleInterface* large = named(view->rootObject(), QStringLiteral("Large"));
    QVERIFY(small && medium && large);
    QCOMPARE(large->role(), QAccessible::RadioButton);
    QVERIFY(medium->state().checked);
    QVERIFY(!large->state().checked);

    QSignalSpy selected(&viewModel, &PinToolOptionsViewModel::stepBadgeSizeSelected);
    large->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    QCOMPARE(selected.count(), 1);
    QCOMPARE(viewModel.stepBadgeSize(), 2);
    QTRY_VERIFY(large->state().checked);
    QVERIFY(!medium->state().checked);
}

void tst_ToolOptionsAccessibility::colorsAreNamedChoices()
{
    PinToolOptionsViewModel viewModel;
    viewModel.showForTool(int(ToolId::Pencil));
    viewModel.setCurrentColor(viewModel.colorPalette().at(2).value<QColor>());
    auto view = load(viewModel);
    QVERIFY(view);
    for (const QString& name : {QStringLiteral("Red"), QStringLiteral("Yellow"), QStringLiteral("Green"),
                                QStringLiteral("Purple"), QStringLiteral("Black"), QStringLiteral("White")})
        QVERIFY2(named(view->rootObject(), name), qPrintable(name));
    QAccessibleInterface* green = named(view->rootObject(), QStringLiteral("Green"));
    QAccessibleInterface* red = named(view->rootObject(), QStringLiteral("Red"));
    QCOMPARE(green->role(), QAccessible::RadioButton);
    QVERIFY(green->state().checked);
    QVERIFY(!red->state().checked);

    red->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    QTRY_VERIFY(red->state().checked);
    QVERIFY(!green->state().checked);

    // The custom color button names itself and the current color.
    QAccessibleInterface* custom = nullptr;
    for (QAccessibleInterface* accessible : accessibles(view->rootObject())) {
        if (accessible->text(QAccessible::Name).startsWith(QStringLiteral("Custom color, #"))) custom = accessible;
    }
    QVERIFY(custom);
    QSignalSpy customClicked(&viewModel, &PinToolOptionsViewModel::customColorPickerRequested);
    QVERIFY(customClicked.isValid());
    custom->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    QCOMPARE(customClicked.count(), 1);
}

// The swatch names come with their colors from the view model, so a palette
// reordered or changed in C++ cannot leave a swatch named for another color.
// Each swatch's drawn color is classified by hue and brightness rather than
// compared with a copy of the palette, so this fails if a name and its color
// get out of step, and pressing a swatch applies the color it shows.
void tst_ToolOptionsAccessibility::paletteNamesStayWithTheirColors()
{
    const auto colorFamily = [](const QColor& color) -> QString {
        const QColor hsv = color.toHsv();
        if (hsv.valueF() < 0.25f) return QStringLiteral("Black");
        if (hsv.hsvSaturationF() < 0.15f && hsv.valueF() > 0.85f) return QStringLiteral("White");
        const int hue = hsv.hsvHue();
        if (hue < 20 || hue >= 340) return QStringLiteral("Red");
        if (hue >= 40 && hue < 75) return QStringLiteral("Yellow");
        if (hue >= 90 && hue < 170) return QStringLiteral("Green");
        if (hue >= 230 && hue < 300) return QStringLiteral("Purple");
        return QString();
    };

    PinToolOptionsViewModel viewModel;
    viewModel.showForTool(int(ToolId::Pencil));
    const QVariantList options = viewModel.colorPaletteOptions();
    QCOMPARE(options.size(), viewModel.colorPalette().size());
    for (qsizetype i = 0; i < options.size(); ++i) {
        const QVariantMap option = options.at(i).toMap();
        const QColor color = option.value(QStringLiteral("color")).value<QColor>();
        QCOMPARE(color, viewModel.colorPalette().at(i).value<QColor>());
        QCOMPARE(option.value(QStringLiteral("name")).toString(), colorFamily(color));
    }

    auto view = load(viewModel);
    QVERIFY(view);
    int swatches = 0;
    for (QAccessibleInterface* accessible : accessibles(view->rootObject())) {
        QObject* swatch = accessible->object();
        if (!swatch || !swatch->objectName().startsWith(QStringLiteral("colorSwatch_"))) continue;
        ++swatches;
        const QColor shown = swatch->property("color").value<QColor>();
        const QString name = accessible->text(QAccessible::Name);
        QVERIFY2(!colorFamily(shown).isEmpty(), qPrintable(shown.name()));
        QVERIFY2(name == colorFamily(shown),
                 qPrintable(QStringLiteral("swatch %1 is named \"%2\"").arg(shown.name(), name)));
        accessible->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
        QCOMPARE(viewModel.currentColor().rgba(), shown.rgba());
    }
    QCOMPARE(swatches, options.size());
}

void tst_ToolOptionsAccessibility::widthIsAnAdjustableSlider()
{
    PinToolOptionsViewModel viewModel;
    viewModel.showForTool(int(ToolId::Pencil));
    viewModel.setCurrentWidth(5);
    auto view = load(viewModel);
    QVERIFY(view);
    QAccessibleInterface* width = named(view->rootObject(), QStringLiteral("Width, 5"));
    QVERIFY(width);
    QCOMPARE(width->role(), QAccessible::Slider);
    QVERIFY(width->valueInterface());
    QCOMPARE(width->valueInterface()->currentValue().toInt(), 5);
    width->actionInterface()->doAction(QAccessibleActionInterface::increaseAction());
    QCOMPARE(viewModel.currentWidth(), 6);
    width->actionInterface()->doAction(QAccessibleActionInterface::decreaseAction());
    width->actionInterface()->doAction(QAccessibleActionInterface::decreaseAction());
    QCOMPARE(viewModel.currentWidth(), 4);
    QTRY_COMPARE(width->text(QAccessible::Name), QStringLiteral("Width, 4"));

    // Mosaic offers width presets as choices instead.
    viewModel.showForTool(int(ToolId::Mosaic));
    viewModel.setCurrentWidth(18);
    QTRY_VERIFY(named(view->rootObject(), QStringLiteral("Width, 18")));
    QAccessibleInterface* preset = named(view->rootObject(), QStringLiteral("Width, 18"));
    QCOMPARE(preset->role(), QAccessible::RadioButton);
    QVERIFY(preset->state().checked);
}

void tst_ToolOptionsAccessibility::styleMenusReadTheCurrentStyle()
{
    PinToolOptionsViewModel viewModel;
    viewModel.showForTool(int(ToolId::Arrow));
    viewModel.setArrowStyle(int(LineEndStyle::BothArrow));
    auto view = load(viewModel);
    QVERIFY(view);
    const QString expected = QStringLiteral("Arrow style, ")
        + AnnotationStyleLabels::arrowStyle(LineEndStyle::BothArrow);
    QCOMPARE(expected, QStringLiteral("Arrow style, Both Ends"));
    QAccessibleInterface* arrow = named(view->rootObject(), expected);
    QVERIFY(arrow);
    QCOMPARE(arrow->role(), QAccessible::ButtonMenu);
    QSignalSpy dropdown(&viewModel, &PinToolOptionsViewModel::arrowStyleDropdownRequested);
    arrow->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    QCOMPARE(dropdown.count(), 1);

    viewModel.setLineStyle(1);
    QTRY_VERIFY(named(view->rootObject(), QStringLiteral("Line style, Dashed")));
}

void tst_ToolOptionsAccessibility::textAndShapeTogglesAreCheckboxes()
{
    PinToolOptionsViewModel viewModel;
    viewModel.showForTool(int(ToolId::Text));
    auto view = load(viewModel);
    QVERIFY(view);
    QAccessibleInterface* bold = named(view->rootObject(), QStringLiteral("Bold"));
    QVERIFY(bold && named(view->rootObject(), QStringLiteral("Italic"))
            && named(view->rootObject(), QStringLiteral("Underline")));
    QCOMPARE(bold->role(), QAccessible::CheckBox);
    const bool wasBold = viewModel.isBold();
    QCOMPARE(bold->state().checked, wasBold);
    bold->actionInterface()->doAction(QAccessibleActionInterface::toggleAction());
    QCOMPARE(viewModel.isBold(), !wasBold);
    // The B/I/U row is rebuilt when a toggle changes, so look the button up again.
    QTRY_VERIFY((bold = named(view->rootObject(), QStringLiteral("Bold"))) != nullptr
                && bool(bold->state().checked) == !wasBold);

    viewModel.showForTool(int(ToolId::Shape));
    viewModel.setShapeFillMode(0);
    QAccessibleInterface* filled = nullptr;
    QTRY_VERIFY((filled = named(view->rootObject(), QStringLiteral("Filled"))) != nullptr);
    QCOMPARE(filled->role(), QAccessible::CheckBox);
    QVERIFY(!filled->state().checked);
    filled->actionInterface()->doAction(QAccessibleActionInterface::toggleAction());
    QCOMPARE(viewModel.shapeFillMode(), 1);
    QTRY_VERIFY(filled->state().checked);
    QVERIFY(named(view->rootObject(), QStringLiteral("Rectangle")));
    QVERIFY(named(view->rootObject(), QStringLiteral("Ellipse")));
}

// #268: a selected annotation gets Arrange buttons named with their shortcut;
// a move that would change nothing does not fire.
void tst_ToolOptionsAccessibility::arrangeButtonsAreNamedAndRequestMoves()
{
    PinToolOptionsViewModel viewModel;
    viewModel.showForTool(int(ToolId::Arrow));
    auto view = load(viewModel);
    QVERIFY(view);
    const auto findPrefixed = [&view](const QString& prefix) -> QAccessibleInterface* {
        for (QAccessibleInterface* accessible : accessibles(view->rootObject())) {
            if (accessible->text(QAccessible::Name).startsWith(prefix)) return accessible;
        }
        return nullptr;
    };
    // New objects have no drawing order to change.
    QVERIFY(!findPrefixed(QStringLiteral("Bring Forward")));

    AnnotationStyleValues values;
    values.kind = StyleTargetKind::Arrow;
    values.color = Qt::red;
    values.width = 3;
    viewModel.showForSelection(values);
    viewModel.setZOrderAvailability(true, false);   // at the back

    QAccessibleInterface* forward = nullptr;
    QTRY_VERIFY((forward = findPrefixed(QStringLiteral("Bring Forward"))) != nullptr);
    QVERIFY(forward->text(QAccessible::Name).contains(
        QKeySequence(Qt::CTRL | Qt::Key_BracketRight).toString(QKeySequence::NativeText)));
    QCOMPARE(forward->role(), QAccessible::Button);
    QAccessibleInterface* backward = findPrefixed(QStringLiteral("Send Backward"));
    QVERIFY(backward && findPrefixed(QStringLiteral("Bring to Front"))
            && findPrefixed(QStringLiteral("Send to Back")));

    QSignalSpy requested(&viewModel, &PinToolOptionsViewModel::zOrderRequested);
    backward->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    QCOMPARE(requested.count(), 0);
    forward->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    QCOMPARE(requested.count(), 1);
    QCOMPARE(requested.first().first().toInt(), int(AnnotationZOrderMove::Forward));
}

QTEST_MAIN(tst_ToolOptionsAccessibility)
#include "tst_ToolOptionsAccessibility.moc"
