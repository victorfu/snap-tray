#include <QtTest/QtTest>

#include <QGuiApplication>
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
        m_settings.remove(QStringLiteral("mosaicBrushAdjustmentLearned"));
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

class TestQmlFloatingSubToolbarMosaicHint : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testTemporaryHideDoesNotReplayCoachmark();
    void testTimeoutLearningAndHoverReminderLifecycle();
};

void TestQmlFloatingSubToolbarMosaicHint::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("Mosaic hint tests require a screen");
    }
}

void TestQmlFloatingSubToolbarMosaicHint::testTemporaryHideDoesNotReplayCoachmark()
{
    ScopedMosaicHintSetting restoreSetting;
    PinToolOptionsViewModel viewModel;
    SnapTray::QmlFloatingSubToolbar subToolbar(&viewModel);
    const QRect toolbarRect = testToolbarRect();
    QVERIFY(toolbarRect.isValid());

    subToolbar.showForTool(static_cast<int>(ToolId::Mosaic));
    QTRY_VERIFY(subToolbar.isVisible());
    subToolbar.positionBelow(toolbarRect);

    QTRY_COMPARE(
        static_cast<int>(subToolbar.m_mosaicHintDisplay),
        static_cast<int>(SnapTray::QmlFloatingSubToolbar::MosaicHintDisplay::Coachmark));
    QVERIFY(subToolbar.m_mosaicHintActivationActive);
    QVERIFY(!subToolbar.m_mosaicCoachmarkPending);
    QVERIFY(subToolbar.m_mosaicCoachmarkTimer.isActive());
    QTRY_VERIFY(subToolbar.m_tooltip.window());
    QTRY_VERIFY(subToolbar.m_tooltip.window()->isVisible());
    QVERIFY(subToolbar.m_tooltip.window()->flags().testFlag(Qt::WindowTransparentForInput));
    QVERIFY(subToolbar.m_tooltip.window()->flags().testFlag(Qt::WindowDoesNotAcceptFocus));

    subToolbar.hide();
    QVERIFY(!subToolbar.isVisible());
    QVERIFY(subToolbar.m_mosaicHintActivationActive);
    QVERIFY(!subToolbar.m_mosaicCoachmarkTimer.isActive());
    QCOMPARE(
        static_cast<int>(subToolbar.m_mosaicHintDisplay),
        static_cast<int>(SnapTray::QmlFloatingSubToolbar::MosaicHintDisplay::None));

    subToolbar.showForTool(static_cast<int>(ToolId::Mosaic));
    QTRY_VERIFY(subToolbar.isVisible());
    subToolbar.positionBelow(toolbarRect);
    QCoreApplication::processEvents();
    QCOMPARE(
        static_cast<int>(subToolbar.m_mosaicHintDisplay),
        static_cast<int>(SnapTray::QmlFloatingSubToolbar::MosaicHintDisplay::None));

    subToolbar.showForTool(static_cast<int>(ToolId::Pencil));
    QVERIFY(!subToolbar.m_mosaicHintActivationActive);
    subToolbar.showForTool(static_cast<int>(ToolId::Mosaic));
    subToolbar.positionBelow(toolbarRect);
    QTRY_COMPARE(
        static_cast<int>(subToolbar.m_mosaicHintDisplay),
        static_cast<int>(SnapTray::QmlFloatingSubToolbar::MosaicHintDisplay::Coachmark));

    subToolbar.close();
}

void TestQmlFloatingSubToolbarMosaicHint::testTimeoutLearningAndHoverReminderLifecycle()
{
    ScopedMosaicHintSetting restoreSetting;
    auto& settings = AnnotationSettingsManager::instance();
    PinToolOptionsViewModel viewModel;
    SnapTray::QmlFloatingSubToolbar subToolbar(&viewModel);
    const QRect toolbarRect = testToolbarRect();
    QVERIFY(toolbarRect.isValid());

    subToolbar.showForTool(static_cast<int>(ToolId::Mosaic));
    QTRY_VERIFY(subToolbar.isVisible());
    subToolbar.positionBelow(toolbarRect);
    QTRY_COMPARE(
        static_cast<int>(subToolbar.m_mosaicHintDisplay),
        static_cast<int>(SnapTray::QmlFloatingSubToolbar::MosaicHintDisplay::Coachmark));

    subToolbar.m_mosaicCoachmarkTimer.setInterval(1);
    QTRY_COMPARE(
        static_cast<int>(subToolbar.m_mosaicHintDisplay),
        static_cast<int>(SnapTray::QmlFloatingSubToolbar::MosaicHintDisplay::None));
    QVERIFY(!settings.loadMosaicBrushAdjustmentLearned());

    const QRect previewRect = subToolbar.mosaicPreviewGlobalRect();
    QVERIFY(previewRect.isValid());
    subToolbar.onMosaicBrushPreviewHovered(previewRect.x(),
                                           previewRect.y(),
                                           previewRect.width(),
                                           previewRect.height());
    QCOMPARE(
        static_cast<int>(subToolbar.m_mosaicHintDisplay),
        static_cast<int>(SnapTray::QmlFloatingSubToolbar::MosaicHintDisplay::HoverReminder));
    subToolbar.onMosaicBrushPreviewHoverExited();

    QVERIFY(viewModel.handleWidthWheelDelta(120));
    QVERIFY(settings.loadMosaicBrushAdjustmentLearned());
    QCOMPARE(
        static_cast<int>(subToolbar.m_mosaicHintDisplay),
        static_cast<int>(SnapTray::QmlFloatingSubToolbar::MosaicHintDisplay::None));

    subToolbar.showForTool(static_cast<int>(ToolId::Pencil));
    subToolbar.showForTool(static_cast<int>(ToolId::Mosaic));
    subToolbar.positionBelow(toolbarRect);
    QCoreApplication::processEvents();
    QCOMPARE(
        static_cast<int>(subToolbar.m_mosaicHintDisplay),
        static_cast<int>(SnapTray::QmlFloatingSubToolbar::MosaicHintDisplay::None));

    const QRect learnedPreviewRect = subToolbar.mosaicPreviewGlobalRect();
    subToolbar.onMosaicBrushPreviewHovered(learnedPreviewRect.x(),
                                           learnedPreviewRect.y(),
                                           learnedPreviewRect.width(),
                                           learnedPreviewRect.height());
    QCOMPARE(
        static_cast<int>(subToolbar.m_mosaicHintDisplay),
        static_cast<int>(SnapTray::QmlFloatingSubToolbar::MosaicHintDisplay::HoverReminder));

    subToolbar.showForTool(static_cast<int>(ToolId::Pencil));
    subToolbar.onMosaicBrushPreviewHovered(learnedPreviewRect.x(),
                                           learnedPreviewRect.y(),
                                           learnedPreviewRect.width(),
                                           learnedPreviewRect.height());
    QCOMPARE(
        static_cast<int>(subToolbar.m_mosaicHintDisplay),
        static_cast<int>(SnapTray::QmlFloatingSubToolbar::MosaicHintDisplay::None));

    subToolbar.close();
}

QTEST_MAIN(TestQmlFloatingSubToolbarMosaicHint)
#include "tst_FloatingSubToolbarMosaicHint.moc"
