#include <QtTest/QtTest>

#include <QGuiApplication>
#include <QQuickItem>
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

} // namespace

class TestQmlFloatingSubToolbarAutoBlurHint : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testHoverReminderLifecycle();
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

QTEST_MAIN(TestQmlFloatingSubToolbarAutoBlurHint)
#include "tst_FloatingSubToolbarAutoBlurHint.moc"
