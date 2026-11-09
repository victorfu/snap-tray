#include <QtTest>
#include <QScopeGuard>
#include <QGuiApplication>
#include <QWheelEvent>
#include <QScreen>
#include <QWindow>
#include <QtQml/qqmlextensionplugin.h>

#include "RegionSelector.h"
#include "detection/TextLineGeometryCache.h"
#include "region/SelectionStateManager.h"
#include "tools/ToolManager.h"
#include "annotations/StepBadgeAnnotation.h"
#include "settings/AnnotationSettingsManager.h"
#include "qml/QmlFloatingSubToolbar.h"
#include "qml/QmlFloatingToolbar.h"
#include "qml/PinToolOptionsViewModel.h"
#include "qml/RegionToolbarViewModel.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

class tst_RegionSelectorMultiRegionSubToolbar : public QObject
{
    Q_OBJECT

private slots:
    void testMarkerTextCacheIsSingleRegionOnly();
    void initTestCase();
    void testToolbarToggleHidesSubToolbarInMultiRegionMode();
    void testBeginReplaceHidesStaleSubToolbar();
    void testCompletedSelectionReopensHiddenSubToolbar();
    void testSubToolbarFollowsNativeToolbarMoveAtBottom();
    void testWheelDoesNotAdjustWidthWithoutCompletedSelection();
    void testMosaicWheelDoesNotAdjustWidthWithCompletedSelection();
    void testStepBadgeSizeStartsSavedAndWheelStopsAtEnds();
};

void tst_RegionSelectorMultiRegionSubToolbar::testSubToolbarFollowsNativeToolbarMoveAtBottom()
{
    RegionSelector selector;
    const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
    selector.m_qmlToolbar->positionForSelection(
        QRect(80, 80, 300, 200), available.width(), available.height());
    selector.m_qmlToolbar->show();
    selector.m_qmlToolbar->window()->setPosition(
        available.left() + 20,
        available.bottom() - selector.m_qmlToolbar->height() - 10);
    selector.m_inputState.currentTool = ToolId::Arrow;
    selector.m_inputState.showSubToolbar = true;
    selector.syncRegionSubToolbar(true);
    QTRY_VERIFY(selector.m_qmlSubToolbar->isVisible());
    QTRY_VERIFY(selector.m_qmlSubToolbar->geometry().bottom()
                < selector.m_qmlToolbar->geometry().top());

    // Model a window manager moving the native toolbar up to clear a bottom
    // panel after the strip has already been positioned. No capture repaint
    // or user drag signal accompanies this move.
    const QRect oldStrip = selector.m_qmlSubToolbar->geometry();
    selector.m_qmlToolbar->window()->setPosition(oldStrip.topLeft());
    QTRY_VERIFY(!selector.m_qmlSubToolbar->geometry().intersects(
        selector.m_qmlToolbar->geometry()));
    QVERIFY(available.contains(selector.m_qmlSubToolbar->geometry()));
}

void tst_RegionSelectorMultiRegionSubToolbar::testMarkerTextCacheIsSingleRegionOnly()
{
    AnnotationSettingsManager::instance().saveMarkerTextSnap(false);
    const auto cleanup = qScopeGuard([] { AnnotationSettingsManager::instance().saveMarkerTextSnap(false); });
    RegionSelector selector;
    delete selector.m_markerTextCache;
    int requests = 0;
    OCRCallback callback;
    selector.m_markerTextCache = new TextLineGeometryCache(
        [&](const QPixmap&, const QStringList&, const OCRCallback& cb) { ++requests; callback = cb; }, &selector);
    selector.m_markerTextSnapSupported = true;
    selector.m_backgroundPixmap = QPixmap(400,200);
    selector.m_backgroundPixmap.fill(Qt::white);
    selector.m_devicePixelRatio = 1;
    selector.m_selectionManager->setSelectionRect({0,0,400,200});
    selector.syncMarkerTextSnap();
    QVERIFY(selector.m_toolOptionsViewModel->markerTextSnapAvailable());
    QCOMPARE(requests,0);
    selector.m_toolOptionsViewModel->setMarkerTextSnapEnabled(true);
    QCOMPARE(requests,1);
    selector.syncMarkerTextSnap(); QCOMPARE(requests,1);
    OCRResult result; result.success = true; result.blocks = {{"text",{.1,.2,.5,.1},-1}};
    callback(result);
    QVERIFY(selector.m_toolManager->context()->markerTextLines());
    selector.m_inputState.multiRegionMode = true;
    selector.syncMarkerTextSnap();
    QVERIFY(!selector.m_toolOptionsViewModel->markerTextSnapAvailable());
    QVERIFY(!selector.m_toolManager->context()->markerTextLines());
    selector.m_inputState.multiRegionMode = false;
    selector.m_markerTextSnapSupported = false; // Linux / unavailable native OCR
    selector.syncMarkerTextSnap();
    QVERIFY(!selector.m_toolOptionsViewModel->markerTextSnapAvailable());
    QCOMPARE(requests,1);
}

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
