#include <QtTest/QtTest>

#include <QGuiApplication>
#include <QQuickItem>
#include <QScreen>
#include <QSettings>
#include <QVariant>
#include <QWindow>

#include "qml/PinToolOptionsViewModel.h"
#include "qml/QmlFloatingSubToolbar.h"
#include "settings/AnnotationSettingsManager.h"
#include "settings/Settings.h"

namespace {

class ScopedMosaicHintSetting final
{
public:
    ScopedMosaicHintSetting()
        : m_settings(SnapTray::getSettings())
        , m_existed(m_settings.contains(QStringLiteral("mosaicBrushAdjustmentLearned")))
        , m_value(m_settings.value(QStringLiteral("mosaicBrushAdjustmentLearned")))
    {
        AnnotationSettingsManager::instance().saveMosaicBrushAdjustmentLearned(true);
        m_settings.sync();
    }

    ~ScopedMosaicHintSetting()
    {
        if (m_existed) {
            m_settings.setValue(QStringLiteral("mosaicBrushAdjustmentLearned"), m_value);
        } else {
            m_settings.remove(QStringLiteral("mosaicBrushAdjustmentLearned"));
        }
        m_settings.sync();
    }

private:
    QSettings m_settings;
    bool m_existed;
    QVariant m_value;
};

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
    ScopedMosaicHintSetting restoreSetting;
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

    QCOMPARE(
        static_cast<int>(subToolbar.m_mosaicHintDisplay),
        static_cast<int>(
            SnapTray::QmlFloatingSubToolbar::MosaicHintDisplay::AutoBlurHoverReminder));
    QCOMPARE(subToolbar.m_rootItem->property("autoBlurHintActive").toBool(), true);
    QCOMPARE(subToolbar.m_rootItem->property("mosaicBrushHintActive").toBool(), false);
    QTRY_VERIFY(subToolbar.m_tooltip.window());
    QTRY_VERIFY(subToolbar.m_tooltip.window()->isVisible());
    QVERIFY(subToolbar.m_tooltip.window()->flags().testFlag(Qt::WindowTransparentForInput));
    QVERIFY(subToolbar.m_tooltip.window()->flags().testFlag(Qt::WindowDoesNotAcceptFocus));

    subToolbar.positionBelow(toolbarRect.translated(10, 10));
    QCOMPARE(
        static_cast<int>(subToolbar.m_mosaicHintDisplay),
        static_cast<int>(
            SnapTray::QmlFloatingSubToolbar::MosaicHintDisplay::AutoBlurHoverReminder));

    subToolbar.onAutoBlurButtonHoverExited();
    QCOMPARE(
        static_cast<int>(subToolbar.m_mosaicHintDisplay),
        static_cast<int>(SnapTray::QmlFloatingSubToolbar::MosaicHintDisplay::None));
    QCOMPARE(subToolbar.m_rootItem->property("autoBlurHintActive").toBool(), false);

    subToolbar.showForTool(static_cast<int>(ToolId::Pencil));
    subToolbar.onAutoBlurButtonHovered(buttonRect.x(),
                                       buttonRect.y(),
                                       buttonRect.width(),
                                       buttonRect.height());
    QCOMPARE(
        static_cast<int>(subToolbar.m_mosaicHintDisplay),
        static_cast<int>(SnapTray::QmlFloatingSubToolbar::MosaicHintDisplay::None));

    subToolbar.close();
}

QTEST_MAIN(TestQmlFloatingSubToolbarAutoBlurHint)
#include "tst_FloatingSubToolbarAutoBlurHint.moc"
