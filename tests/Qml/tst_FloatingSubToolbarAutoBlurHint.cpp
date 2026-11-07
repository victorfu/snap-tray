#include <QtTest/QtTest>

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QWindow>

#include "qml/PinToolOptionsViewModel.h"
#include "qml/QmlFloatingSubToolbar.h"

namespace {

QString shownTooltipText(QWindow* tooltipWindow)
{
    auto* view = qobject_cast<QQuickView*>(tooltipWindow);
    return view && view->rootObject()
        ? view->rootObject()->property("tooltipText").toString()
        : QString();
}

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

} // namespace

class TestQmlFloatingSubToolbarAutoBlurHint : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testHoverReminderLifecycle();
    void testEyedropperTooltipLifecycle();
    void testControlTooltipAndAutoBlurHintShareTheTooltip();
};

void TestQmlFloatingSubToolbarAutoBlurHint::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("Auto-blur hint tests require a screen");
    }
}

void TestQmlFloatingSubToolbarAutoBlurHint::testHoverReminderLifecycle()
{
    PinToolOptionsViewModel viewModel;
    SnapTray::QmlFloatingSubToolbar subToolbar(&viewModel);
    const QRect toolbarRect = testToolbarRect();
    QVERIFY(toolbarRect.isValid());

    subToolbar.showForTool(static_cast<int>(ToolId::Mosaic));
    QTRY_VERIFY(subToolbar.isVisible());
    subToolbar.positionBelow(toolbarRect);

    const QRect buttonRect = subToolbar.autoBlurButtonGlobalRect();
    QVERIFY(buttonRect.isValid());
    subToolbar.onAutoBlurButtonHovered(buttonRect.x(),
                                       buttonRect.y(),
                                       buttonRect.width(),
                                       buttonRect.height());

    QVERIFY(subToolbar.m_autoBlurHintVisible);
    QCOMPARE(subToolbar.m_rootItem->property("autoBlurHintActive").toBool(), true);
    QTRY_VERIFY(subToolbar.m_tooltip.window());
    QTRY_VERIFY(subToolbar.m_tooltip.window()->isVisible());
    QVERIFY(subToolbar.m_tooltip.window()->flags().testFlag(Qt::WindowTransparentForInput));
    QVERIFY(subToolbar.m_tooltip.window()->flags().testFlag(Qt::WindowDoesNotAcceptFocus));

    subToolbar.positionBelow(toolbarRect.translated(10, 10));
    QVERIFY(subToolbar.m_autoBlurHintVisible);

    viewModel.showLaserPointerOptions();
    QVERIFY(!subToolbar.m_autoBlurHintVisible);
    QCOMPARE(subToolbar.m_rootItem->property("autoBlurHintActive").toBool(), false);
    QVERIFY(!subToolbar.m_tooltip.window()->isVisible());

    subToolbar.showForTool(static_cast<int>(ToolId::Mosaic));
    subToolbar.positionBelow(toolbarRect);
    subToolbar.onAutoBlurButtonHovered(buttonRect.x(),
                                       buttonRect.y(),
                                       buttonRect.width(),
                                       buttonRect.height());
    QVERIFY(subToolbar.m_autoBlurHintVisible);

    subToolbar.onAutoBlurButtonHoverExited();
    QVERIFY(!subToolbar.m_autoBlurHintVisible);
    QCOMPARE(subToolbar.m_rootItem->property("autoBlurHintActive").toBool(), false);

    subToolbar.showForTool(static_cast<int>(ToolId::Pencil));
    subToolbar.onAutoBlurButtonHovered(buttonRect.x(),
                                       buttonRect.y(),
                                       buttonRect.width(),
                                       buttonRect.height());
    QVERIFY(!subToolbar.m_autoBlurHintVisible);

    subToolbar.close();
}

// #305: hovering the eyedropper names it and its shortcut through the same
// tooltip as the Auto Blur hint, anchored on the eyedropper itself.
void TestQmlFloatingSubToolbarAutoBlurHint::testEyedropperTooltipLifecycle()
{
    PinToolOptionsViewModel viewModel;
    SnapTray::QmlFloatingSubToolbar subToolbar(&viewModel);
    const QRect toolbarRect = testToolbarRect();
    QVERIFY(toolbarRect.isValid());

    subToolbar.showForTool(static_cast<int>(ToolId::Pencil));
    QTRY_VERIFY(subToolbar.isVisible());
    subToolbar.positionBelow(toolbarRect);

    auto* pick = subToolbar.m_rootItem->findChild<QQuickItem*>(QStringLiteral("colorPickButton"));
    QVERIFY(pick);
    const auto pickCenterX = [pick]() {
        return qRound(pick->mapToGlobal(QPointF(pick->width() / 2.0, 0.0)).x());
    };
    const auto tooltipVisible = [&subToolbar]() {
        QWindow* window = subToolbar.m_tooltip.window();
        return window && window->isVisible();
    };
    const auto tooltipOverPick = [&]() {
        return tooltipVisible()
            && qAbs(subToolbar.m_tooltip.window()->geometry().center().x() - pickCenterX()) <= 2;
    };
    const QString text = QStringLiteral("Pick color from screen (I)");
    const auto requestTooltip = [&]() {
        // Emitting the strip's QML signal also proves the sub-toolbar listens to it.
        return QMetaObject::invokeMethod(subToolbar.m_rootItem, "controlTooltipRequested",
                                         Q_ARG(QString, QStringLiteral("colorPickButton")),
                                         Q_ARG(QString, text));
    };

    QVERIFY(requestTooltip());
    QTRY_VERIFY(tooltipOverPick());
    QCOMPARE(shownTooltipText(subToolbar.m_tooltip.window()), text);
    QCOMPARE(subToolbar.m_rootItem->property("autoBlurHintActive").toBool(), false);

    // Moving the strip carries the tooltip along with the eyedropper.
    subToolbar.positionBelow(toolbarRect.translated(40, 10));
    QTRY_VERIFY(tooltipOverPick());

    QVERIFY(QMetaObject::invokeMethod(subToolbar.m_rootItem, "controlTooltipDismissed"));
    QTRY_VERIFY(!tooltipVisible());
    // A later move must not bring a dismissed tooltip back.
    subToolbar.positionBelow(toolbarRect);
    QTest::qWait(50);
    QVERIFY(!tooltipVisible());

    // Switching to a tool without colors hides it, and a move keeps it hidden.
    QVERIFY(requestTooltip());
    QTRY_VERIFY(tooltipOverPick());
    subToolbar.showForTool(static_cast<int>(ToolId::Mosaic));
    QTRY_VERIFY(!tooltipVisible());
    subToolbar.positionBelow(toolbarRect);
    QTest::qWait(50);
    QVERIFY(!tooltipVisible());

    // Hiding the strip hides the tooltip.
    subToolbar.showForTool(static_cast<int>(ToolId::Pencil));
    subToolbar.positionBelow(toolbarRect);
    QVERIFY(requestTooltip());
    QTRY_VERIFY(tooltipOverPick());
    subToolbar.hide();
    QTRY_VERIFY(!tooltipVisible());

    subToolbar.close();
}

// The Auto Blur hint and control tooltips share one tooltip: whichever shows
// last owns it, and withdrawing one never closes the other.
void TestQmlFloatingSubToolbarAutoBlurHint::testControlTooltipAndAutoBlurHintShareTheTooltip()
{
    PinToolOptionsViewModel viewModel;
    SnapTray::QmlFloatingSubToolbar subToolbar(&viewModel);
    const QRect toolbarRect = testToolbarRect();
    QVERIFY(toolbarRect.isValid());

    subToolbar.showForTool(static_cast<int>(ToolId::Mosaic));
    QTRY_VERIFY(subToolbar.isVisible());
    subToolbar.positionBelow(toolbarRect);

    const QRect buttonRect = subToolbar.autoBlurButtonGlobalRect();
    QVERIFY(buttonRect.isValid());
    const auto hoverAutoBlur = [&]() {
        subToolbar.onAutoBlurButtonHovered(buttonRect.x(), buttonRect.y(),
                                           buttonRect.width(), buttonRect.height());
    };
    const auto tooltipVisible = [&subToolbar]() {
        QWindow* window = subToolbar.m_tooltip.window();
        return window && window->isVisible();
    };
    const auto shownText = [&subToolbar]() {
        return shownTooltipText(subToolbar.m_tooltip.window());
    };

    hoverAutoBlur();
    QVERIFY(subToolbar.m_autoBlurHintVisible);
    QTRY_VERIFY(tooltipVisible());
    const QString hintText = shownText();
    QVERIFY(!hintText.isEmpty());

    // Mosaic has no colors: a request for the hidden eyedropper leaves the hint up.
    subToolbar.onControlTooltipRequested(QStringLiteral("colorPickButton"),
                                         QStringLiteral("Pick color from screen (I)"));
    QVERIFY(subToolbar.m_autoBlurHintVisible);
    QVERIFY(tooltipVisible());
    QCOMPARE(shownText(), hintText);

    // A control tooltip (anchored here on the Auto Blur button itself) takes
    // over and drops the hint's emphasis...
    const QString controlText = QStringLiteral("Control tooltip");
    subToolbar.onControlTooltipRequested(QStringLiteral("autoBlurButton"), controlText);
    QVERIFY(!subToolbar.m_autoBlurHintVisible);
    QCOMPARE(subToolbar.m_rootItem->property("autoBlurHintActive").toBool(), false);
    QTRY_COMPARE(shownText(), controlText);
    // ...and the hint's hover exit leaves it alone.
    subToolbar.onAutoBlurButtonHoverExited();
    QVERIFY(tooltipVisible());
    QCOMPARE(shownText(), controlText);

    // The hint takes over again, and a late dismissal of the control leaves it up.
    hoverAutoBlur();
    QVERIFY(subToolbar.m_autoBlurHintVisible);
    QTRY_COMPARE(shownText(), hintText);
    subToolbar.onControlTooltipDismissed();
    QVERIFY(subToolbar.m_autoBlurHintVisible);
    QVERIFY(tooltipVisible());
    QCOMPARE(shownText(), hintText);

    subToolbar.close();
}

QTEST_MAIN(TestQmlFloatingSubToolbarAutoBlurHint)
#include "tst_FloatingSubToolbarAutoBlurHint.moc"
