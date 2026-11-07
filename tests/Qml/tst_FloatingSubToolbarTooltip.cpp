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

} // namespace

// The sub-toolbar draws hover tooltips for its controls (the auto-blur hint and
// the marker's Snap to text toggle, #304) in a separate input-transparent window.
class TestQmlFloatingSubToolbarTooltip : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testAutoBlurHintLifecycle();
    void testMarkerTextSnapTooltip();

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

QTEST_MAIN(TestQmlFloatingSubToolbarTooltip)
#include "tst_FloatingSubToolbarTooltip.moc"
