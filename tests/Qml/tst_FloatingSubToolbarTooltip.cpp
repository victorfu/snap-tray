#include <QtTest/QtTest>

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QWindow>

#include "qml/PinToolOptionsViewModel.h"
#include "qml/QmlFloatingSubToolbar.h"

namespace {

QRect testToolbarRect()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return {};
    }

    const QRect available = screen->availableGeometry().isValid()
        ? screen->availableGeometry()
        : screen->geometry();
    return QRect(available.topLeft() + QPoint(80, 80), QSize(260, 36));
}

const QString kAutoBlurButton = QStringLiteral("autoBlurButton");
const QString kMarkerTextSnapToggle = QStringLiteral("markerTextSnapToggle");
const QString kColorPickButton = QStringLiteral("colorPickButton");

} // namespace

// The sub-toolbar draws hover tooltips for its controls (the Auto Blur hint,
// the eyedropper #305, and the marker's Snap to text toggle #304) in one
// separate input-transparent window.
class TestQmlFloatingSubToolbarTooltip : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testAutoBlurHintLifecycle();
    void testMarkerTextSnapTooltip();
    void testEyedropperTooltipLifecycle();
    void testControlsShareOneTooltip();

private:
    static bool tooltipVisible(SnapTray::QmlFloatingSubToolbar& subToolbar);
    static QString tooltipText(SnapTray::QmlFloatingSubToolbar& subToolbar);
    static bool autoBlurEmphasized(SnapTray::QmlFloatingSubToolbar& subToolbar);
};

bool TestQmlFloatingSubToolbarTooltip::tooltipVisible(SnapTray::QmlFloatingSubToolbar& subToolbar)
{
    return subToolbar.m_tooltip.window() && subToolbar.m_tooltip.window()->isVisible();
}

QString TestQmlFloatingSubToolbarTooltip::tooltipText(SnapTray::QmlFloatingSubToolbar& subToolbar)
{
    auto* view = qobject_cast<QQuickView*>(subToolbar.m_tooltip.window());
    return view && view->rootObject()
        ? view->rootObject()->property("tooltipText").toString()
        : QString();
}

bool TestQmlFloatingSubToolbarTooltip::autoBlurEmphasized(SnapTray::QmlFloatingSubToolbar& subToolbar)
{
    return subToolbar.m_rootItem->property("autoBlurHintActive").toBool();
}

void TestQmlFloatingSubToolbarTooltip::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("Sub-toolbar tooltip tests require a screen");
    }
}

void TestQmlFloatingSubToolbarTooltip::testAutoBlurHintLifecycle()
{
    PinToolOptionsViewModel viewModel;
    SnapTray::QmlFloatingSubToolbar subToolbar(&viewModel);
    const QRect toolbarRect = testToolbarRect();
    QVERIFY(toolbarRect.isValid());

    subToolbar.showForTool(static_cast<int>(ToolId::Mosaic));
    QTRY_VERIFY(subToolbar.isVisible());
    subToolbar.positionBelow(toolbarRect);

    QVERIFY(subToolbar.controlGlobalRect(kAutoBlurButton).isValid());
    subToolbar.onControlTooltipRequested(kAutoBlurButton, viewModel.autoBlurHintText());

    QCOMPARE(subToolbar.m_tooltipControl ? subToolbar.m_tooltipControl->objectName() : QString(),
             kAutoBlurButton);
    QVERIFY(autoBlurEmphasized(subToolbar));
    QTRY_VERIFY(tooltipVisible(subToolbar));
    QCOMPARE(tooltipText(subToolbar), viewModel.autoBlurHintText());
    QVERIFY(subToolbar.m_tooltip.window()->flags().testFlag(Qt::WindowTransparentForInput));
    QVERIFY(subToolbar.m_tooltip.window()->flags().testFlag(Qt::WindowDoesNotAcceptFocus));

    subToolbar.positionBelow(toolbarRect.translated(10, 10));
    QVERIFY(subToolbar.m_tooltipControl);

    // Switching tools hides the button under a still pointer: the hint goes too.
    viewModel.showLaserPointerOptions();
    QVERIFY(!subToolbar.m_tooltipControl);
    QVERIFY(!autoBlurEmphasized(subToolbar));
    QVERIFY(!subToolbar.m_tooltip.window()->isVisible());

    subToolbar.showForTool(static_cast<int>(ToolId::Mosaic));
    subToolbar.positionBelow(toolbarRect);
    subToolbar.onControlTooltipRequested(kAutoBlurButton, viewModel.autoBlurHintText());
    QVERIFY(subToolbar.m_tooltipControl);

    subToolbar.onControlTooltipDismissed(kAutoBlurButton);
    QVERIFY(!subToolbar.m_tooltipControl);
    QVERIFY(!autoBlurEmphasized(subToolbar));

    // A hidden control cannot ask for a tooltip.
    subToolbar.showForTool(static_cast<int>(ToolId::Pencil));
    subToolbar.onControlTooltipRequested(kAutoBlurButton, viewModel.autoBlurHintText());
    QVERIFY(!subToolbar.m_tooltipControl);

    subToolbar.close();
}

void TestQmlFloatingSubToolbarTooltip::testMarkerTextSnapTooltip()
{
    PinToolOptionsViewModel viewModel;
    viewModel.setMarkerTextSnapAvailable(true);
    SnapTray::QmlFloatingSubToolbar subToolbar(&viewModel);
    const QRect toolbarRect = testToolbarRect();
    QVERIFY(toolbarRect.isValid());

    subToolbar.showForTool(static_cast<int>(ToolId::Marker));
    QTRY_VERIFY(subToolbar.isVisible());
    subToolbar.positionBelow(toolbarRect);

    // The toggle is a fixed 22x22 icon button whatever the locale.
    const QRect toggleRect = subToolbar.controlGlobalRect(kMarkerTextSnapToggle);
    QVERIFY(toggleRect.isValid());
    QCOMPARE(toggleRect.size(), QSize(22, 22));

    // The QML control sends its own text, so hover the real signal path.
    QQuickItem* toggle = subToolbar.m_rootItem->findChild<QQuickItem*>(kMarkerTextSnapToggle);
    QVERIFY(toggle);
    const QString text = toggle->property("tooltipText").toString();
    QCOMPARE(text, QStringLiteral("Snap to text: marker strokes align to detected text lines"));
    QVERIFY(QMetaObject::invokeMethod(subToolbar.m_rootItem, "controlTooltipRequested",
                                      Q_ARG(QString, kMarkerTextSnapToggle),
                                      Q_ARG(QString, text)));

    QCOMPARE(subToolbar.m_tooltipControl.data(), toggle);
    QVERIFY(!autoBlurEmphasized(subToolbar));
    QTRY_VERIFY(tooltipVisible(subToolbar));
    QCOMPARE(tooltipText(subToolbar), text);

    // Toggling keeps the tooltip; only the preference changes.
    QVERIFY(!viewModel.markerTextSnapEnabled());
    QVERIFY(QMetaObject::invokeMethod(toggle, "toggle"));
    QVERIFY(viewModel.markerTextSnapEnabled());
    QVERIFY(subToolbar.m_tooltipControl);

    // Dismissing another control leaves this tooltip alone.
    subToolbar.onControlTooltipDismissed(kAutoBlurButton);
    QVERIFY(subToolbar.m_tooltipControl);

    // Leaving Marker hides the toggle under a still pointer: the tooltip goes too.
    viewModel.showForTool(static_cast<int>(ToolId::Pencil));
    QVERIFY(!toggle->isVisible());
    QVERIFY(!subToolbar.m_tooltipControl);
    QVERIFY(!subToolbar.m_tooltip.window()->isVisible());

    subToolbar.close();
}

// #305: hovering the eyedropper names it and its shortcut through the same
// tooltip, anchored on the eyedropper itself.
void TestQmlFloatingSubToolbarTooltip::testEyedropperTooltipLifecycle()
{
    PinToolOptionsViewModel viewModel;
    SnapTray::QmlFloatingSubToolbar subToolbar(&viewModel);
    const QRect toolbarRect = testToolbarRect();
    QVERIFY(toolbarRect.isValid());

    subToolbar.showForTool(static_cast<int>(ToolId::Pencil));
    QTRY_VERIFY(subToolbar.isVisible());
    subToolbar.positionBelow(toolbarRect);

    auto* pick = subToolbar.m_rootItem->findChild<QQuickItem*>(kColorPickButton);
    QVERIFY(pick);
    const auto pickCenterX = [pick]() {
        return qRound(pick->mapToGlobal(QPointF(pick->width() / 2.0, 0.0)).x());
    };
    const auto tooltipOverPick = [&]() {
        return tooltipVisible(subToolbar)
            && qAbs(subToolbar.m_tooltip.window()->geometry().center().x() - pickCenterX()) <= 2;
    };
    const QString text = QStringLiteral("Pick color from screen (I)");
    const auto requestTooltip = [&]() {
        // Emitting the strip's QML signal also proves the sub-toolbar listens to it.
        return QMetaObject::invokeMethod(subToolbar.m_rootItem, "controlTooltipRequested",
                                         Q_ARG(QString, kColorPickButton),
                                         Q_ARG(QString, text));
    };

    QVERIFY(requestTooltip());
    QTRY_VERIFY(tooltipOverPick());
    QCOMPARE(tooltipText(subToolbar), text);
    QVERIFY(!autoBlurEmphasized(subToolbar));

    // Moving the strip carries the tooltip along with the eyedropper.
    subToolbar.positionBelow(toolbarRect.translated(40, 10));
    QTRY_VERIFY(tooltipOverPick());

    QVERIFY(QMetaObject::invokeMethod(subToolbar.m_rootItem, "controlTooltipDismissed",
                                      Q_ARG(QString, kColorPickButton)));
    QTRY_VERIFY(!tooltipVisible(subToolbar));
    // A later move must not bring a dismissed tooltip back.
    subToolbar.positionBelow(toolbarRect);
    QTest::qWait(50);
    QVERIFY(!tooltipVisible(subToolbar));

    // Switching to a tool without colors hides it, and a move keeps it hidden.
    QVERIFY(requestTooltip());
    QTRY_VERIFY(tooltipOverPick());
    subToolbar.showForTool(static_cast<int>(ToolId::Mosaic));
    QTRY_VERIFY(!tooltipVisible(subToolbar));
    subToolbar.positionBelow(toolbarRect);
    QTest::qWait(50);
    QVERIFY(!tooltipVisible(subToolbar));

    // Hiding the strip hides the tooltip.
    subToolbar.showForTool(static_cast<int>(ToolId::Pencil));
    subToolbar.positionBelow(toolbarRect);
    QVERIFY(requestTooltip());
    QTRY_VERIFY(tooltipOverPick());
    subToolbar.hide();
    QTRY_VERIFY(!tooltipVisible(subToolbar));

    subToolbar.close();
}

// Controls share one tooltip (#305): whichever asks last owns it, a hidden
// control cannot take it, and withdrawing one never closes another's.
void TestQmlFloatingSubToolbarTooltip::testControlsShareOneTooltip()
{
    PinToolOptionsViewModel viewModel;
    SnapTray::QmlFloatingSubToolbar subToolbar(&viewModel);
    const QRect toolbarRect = testToolbarRect();
    QVERIFY(toolbarRect.isValid());

    subToolbar.showForTool(static_cast<int>(ToolId::Mosaic));
    QTRY_VERIFY(subToolbar.isVisible());
    subToolbar.positionBelow(toolbarRect);

    const QString hintText = viewModel.autoBlurHintText();
    subToolbar.onControlTooltipRequested(kAutoBlurButton, hintText);
    QVERIFY(autoBlurEmphasized(subToolbar));
    QTRY_VERIFY(tooltipVisible(subToolbar));
    QCOMPARE(tooltipText(subToolbar), hintText);

    // Mosaic has no colors: a request for the hidden eyedropper leaves the hint up.
    subToolbar.onControlTooltipRequested(kColorPickButton,
                                         QStringLiteral("Pick color from screen (I)"));
    QCOMPARE(subToolbar.m_tooltipControl->objectName(), kAutoBlurButton);
    QVERIFY(autoBlurEmphasized(subToolbar));
    QVERIFY(tooltipVisible(subToolbar));
    QCOMPARE(tooltipText(subToolbar), hintText);

    // Another visible control takes over, without hiding the tooltip, and drops
    // the hint's emphasis...
    const QString presets = QStringLiteral("mosaicWidthPresetRow");
    const QString controlText = QStringLiteral("Control tooltip");
    subToolbar.onControlTooltipRequested(presets, controlText);
    QVERIFY(!autoBlurEmphasized(subToolbar));
    QVERIFY(tooltipVisible(subToolbar));
    QCOMPARE(tooltipText(subToolbar), controlText);
    // ...and the Auto Blur button's hover exit leaves it alone.
    subToolbar.onControlTooltipDismissed(kAutoBlurButton);
    QVERIFY(tooltipVisible(subToolbar));
    QCOMPARE(tooltipText(subToolbar), controlText);

    // The hint takes over again, and a late dismissal of the other control leaves it up.
    subToolbar.onControlTooltipRequested(kAutoBlurButton, hintText);
    QVERIFY(autoBlurEmphasized(subToolbar));
    QCOMPARE(tooltipText(subToolbar), hintText);
    subToolbar.onControlTooltipDismissed(presets);
    QVERIFY(autoBlurEmphasized(subToolbar));
    QVERIFY(tooltipVisible(subToolbar));
    QCOMPARE(tooltipText(subToolbar), hintText);

    subToolbar.close();
}

QTEST_MAIN(TestQmlFloatingSubToolbarTooltip)
#include "tst_FloatingSubToolbarTooltip.moc"
