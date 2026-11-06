#include <QtTest>
#include <QScopeGuard>
#include <QGuiApplication>
#include <QWheelEvent>
#include <QtQml/qqmlextensionplugin.h>

#include "RegionSelector.h"
#include "annotations/StepBadgeAnnotation.h"
#include "settings/AnnotationSettingsManager.h"
#include "qml/QmlFloatingSubToolbar.h"
#include "qml/PinToolOptionsViewModel.h"
#include "qml/RegionToolbarViewModel.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

class tst_RegionSelectorMultiRegionSubToolbar : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testToolbarToggleHidesSubToolbarInMultiRegionMode();
    void testBeginReplaceHidesStaleSubToolbar();
    void testCompletedSelectionReopensHiddenSubToolbar();
    void testWheelDoesNotAdjustWidthWithoutCompletedSelection();
    void testMosaicWheelDoesNotAdjustWidthWithCompletedSelection();
    void testStepBadgeSizeStartsSavedAndWheelStopsAtEnds();
};

void tst_RegionSelectorMultiRegionSubToolbar::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for RegionSelector floating UI tests in this environment.");
    }
}

void tst_RegionSelectorMultiRegionSubToolbar::testToolbarToggleHidesSubToolbarInMultiRegionMode()
{
    RegionSelector selector;

    selector.m_inputState.currentTool = ToolId::Arrow;
    selector.m_inputState.showSubToolbar = true;
    selector.m_toolOptionsViewModel->showForTool(static_cast<int>(ToolId::Arrow));
    QVERIFY(selector.m_toolOptionsViewModel->hasContent());

    selector.handleToolbarClick(ToolId::MultiRegion);

    QVERIFY(selector.m_inputState.multiRegionMode);
    QVERIFY(!selector.m_inputState.showSubToolbar);
    QVERIFY(!selector.m_toolOptionsViewModel->hasContent());

    const QVariantList buttons = selector.m_toolbarViewModel->buttons();
    QCOMPARE(buttons.size(), 2);
    QCOMPARE(buttons.at(0).toMap().value(QStringLiteral("id")).toInt(),
             static_cast<int>(ToolId::MultiRegionDone));
    QCOMPARE(buttons.at(1).toMap().value(QStringLiteral("id")).toInt(),
             static_cast<int>(ToolId::Cancel));
}

void tst_RegionSelectorMultiRegionSubToolbar::testBeginReplaceHidesStaleSubToolbar()
{
    RegionSelector selector;

    selector.setMultiRegionMode(true);
    selector.m_multiRegionManager->addRegion(QRect(20, 30, 120, 90));

    selector.m_inputState.currentTool = ToolId::Arrow;
    selector.m_inputState.showSubToolbar = true;
    selector.m_toolOptionsViewModel->showForTool(static_cast<int>(ToolId::Arrow));
    QVERIFY(selector.m_toolOptionsViewModel->hasContent());

    selector.beginMultiRegionReplace(0);

    QCOMPARE(selector.m_inputState.replaceTargetIndex, 0);
    QCOMPARE(selector.m_inputState.currentTool, ToolId::Selection);
    QVERIFY(!selector.m_inputState.showSubToolbar);
    QVERIFY(!selector.m_toolOptionsViewModel->hasContent());
}

void tst_RegionSelectorMultiRegionSubToolbar::testCompletedSelectionReopensHiddenSubToolbar()
{
    RegionSelector selector;

    selector.m_inputState.currentTool = ToolId::Arrow;
    selector.m_inputState.showSubToolbar = true;
    selector.m_qmlSubToolbar->showForTool(static_cast<int>(ToolId::Arrow));
    QVERIFY(selector.m_toolOptionsViewModel->hasContent());
    QVERIFY(selector.m_qmlSubToolbar->isVisible());

    selector.m_toolOptionsViewModel->clearSections();
    selector.m_qmlSubToolbar->hide();
    QVERIFY(!selector.m_toolOptionsViewModel->hasContent());
    QVERIFY(!selector.m_qmlSubToolbar->isVisible());

    selector.syncRegionSubToolbar(false);

    QVERIFY(selector.m_toolOptionsViewModel->hasContent());
    QVERIFY(selector.m_qmlSubToolbar->isVisible());
}

void tst_RegionSelectorMultiRegionSubToolbar::testWheelDoesNotAdjustWidthWithoutCompletedSelection()
{
    RegionSelector selector;

    selector.m_inputState.currentTool = ToolId::Arrow;
    selector.m_inputState.showSubToolbar = true;
    selector.m_toolOptionsViewModel->showForTool(static_cast<int>(ToolId::Arrow));
    selector.m_toolOptionsViewModel->setCurrentWidth(5);
    QVERIFY(selector.m_toolOptionsViewModel->showWidthSection());
    QVERIFY(!selector.m_selectionManager->isComplete());

    QWheelEvent event(QPointF(12, 12),
                      QPointF(12, 12),
                      QPoint(),
                      QPoint(0, 120),
                      Qt::NoButton,
                      Qt::NoModifier,
                      Qt::NoScrollPhase,
                      false);

    selector.wheelEvent(&event);

    QCOMPARE(selector.m_toolOptionsViewModel->currentWidth(), 5);
}

void tst_RegionSelectorMultiRegionSubToolbar::testMosaicWheelDoesNotAdjustWidthWithCompletedSelection()
{
    RegionSelector selector;

    selector.m_selectionManager->setSelectionRect(QRect(20, 20, 120, 90));
    selector.m_inputState.currentTool = ToolId::Mosaic;
    selector.m_inputState.showSubToolbar = true;
    selector.m_toolOptionsViewModel->showForTool(static_cast<int>(ToolId::Mosaic));
    selector.m_toolOptionsViewModel->setCurrentWidth(18);
    QVERIFY(selector.m_selectionManager->isComplete());

    QWheelEvent event(QPointF(40, 40),
                      QPointF(40, 40),
                      QPoint(),
                      QPoint(0, 120),
                      Qt::NoButton,
                      Qt::NoModifier,
                      Qt::NoScrollPhase,
                      false);

    selector.wheelEvent(&event);

    QCOMPARE(selector.m_toolOptionsViewModel->currentWidth(), 18);
    QVERIFY(!event.isAccepted());
}

void tst_RegionSelectorMultiRegionSubToolbar::testStepBadgeSizeStartsSavedAndWheelStopsAtEnds()
{
    auto& annotationSettings = AnnotationSettingsManager::instance();
    const StepBadgeSize previous = annotationSettings.loadStepBadgeSize();
    const auto restore = qScopeGuard([&] { annotationSettings.saveStepBadgeSize(previous); });
    annotationSettings.saveStepBadgeSize(StepBadgeSize::Large);

    RegionSelector selector;
    // The S/M/L strip shows the saved size, not the default Medium.
    QCOMPARE(selector.m_toolOptionsViewModel->stepBadgeSize(), static_cast<int>(StepBadgeSize::Large));

    selector.m_inputState.currentTool = ToolId::StepBadge;
    auto wheel = [&selector](int deltaY) {
        QWheelEvent event(QPointF(40, 40), QPointF(40, 40), QPoint(), QPoint(0, deltaY),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        selector.wheelEvent(&event);
        return event.isAccepted();
    };

    // Scrolling up at Large stays at Large instead of wrapping to Small.
    QVERIFY(wheel(120));
    QCOMPARE(selector.m_stepBadgeSize, StepBadgeSize::Large);
    QCOMPARE(selector.m_toolOptionsViewModel->stepBadgeSize(), static_cast<int>(StepBadgeSize::Large));

    QVERIFY(wheel(-120));
    QCOMPARE(selector.m_stepBadgeSize, StepBadgeSize::Medium);
    QCOMPARE(selector.m_toolOptionsViewModel->stepBadgeSize(), static_cast<int>(StepBadgeSize::Medium));
    QCOMPARE(annotationSettings.loadStepBadgeSize(), StepBadgeSize::Medium);
}

QTEST_MAIN(tst_RegionSelectorMultiRegionSubToolbar)
#include "tst_MultiRegionSubToolbar.moc"
