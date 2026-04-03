#include <QtTest>

#include <QGuiApplication>
#include <QScreen>
#include <QtQml/qqmlextensionplugin.h>

#include "RegionSelector.h"
#include "RegionSelectorTestAccess.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

namespace {

constexpr int kAttachmentGap = 8;

QRect toGlobalRect(const QWidget& widget, const QRect& localRect)
{
    return localRect.isValid() && !localRect.isEmpty()
        ? QRect(widget.mapToGlobal(localRect.topLeft()), localRect.size())
        : QRect();
}

QRect rectInWindowGlobalCoords(const QRect& windowGeometry, const QRect& windowLocalRect)
{
    return windowGeometry.isValid() &&
        !windowGeometry.isEmpty() &&
        windowLocalRect.isValid() &&
        !windowLocalRect.isEmpty()
        ? windowLocalRect.translated(windowGeometry.topLeft())
        : QRect();
}

void prepareCompletedSelection(RegionSelector& selector,
                               const QRect& hostGeometry,
                               const QRect& selectionRect)
{
    selector.setGeometry(hostGeometry);
    selector.show();
    QCoreApplication::processEvents();
    RegionSelectorTestAccess::markInitialRevealRevealed(selector);
    RegionSelectorTestAccess::setSelectionRect(selector, selectionRect);
    RegionSelectorTestAccess::setCurrentTool(selector, ToolId::Arrow);

    RegionSelectorTestAccess::invokePaint(selector, QRegion(selector.rect()));
    QCoreApplication::processEvents();
    RegionSelectorTestAccess::invokePaint(selector, QRegion(selector.rect()));
    QCoreApplication::processEvents();
}

} // namespace

class tst_RegionSelectorAttachmentLayout : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testRegionControlPanelMovesInsideSelectionWhenTopToolbarOverlaps();
    void testRegionControlPanelStaysBesideDimensionLabelWhenOnlyPopupWidthWouldOverlap();
    void testWideShortSelectionKeepsRegionControlPanelVisible();
    void testTallNarrowSelectionHidesRegionControlPanelButKeepsStandardSizeWidgetPlacement();
    void testSmallRegionHidesRegionControlPanelAndPlacesDimensionLabelLeftOfSelection();
    void testSmallRegionClampsDimensionLabelToViewportLeftWithoutInsideFallback();
};

void tst_RegionSelectorAttachmentLayout::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for RegionSelector attachment layout tests.");
    }
}

void tst_RegionSelectorAttachmentLayout::testRegionControlPanelMovesInsideSelectionWhenTopToolbarOverlaps()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);

    const QRect hostGeometry = screen->availableGeometry().adjusted(40, 40, -40, -120);
    if (hostGeometry.width() < 980 || hostGeometry.height() < 520) {
        QSKIP("Primary screen is too small for toolbar/control overlap verification.");
    }

    const QRect selectionRect(80, hostGeometry.height() - 120, 760, 100);

    RegionSelector selector;
    prepareCompletedSelection(selector, hostGeometry, selectionRect);

    const QRect toolbarGeometry = RegionSelectorTestAccess::toolbarGeometry(selector);
    const QRect regionControlGeometry = RegionSelectorTestAccess::regionControlGeometry(selector);
    const QRect regionControlAnchorRect = RegionSelectorTestAccess::regionControlAnchorRect(selector);
    const QRect dimensionInfoGeometry =
        toGlobalRect(selector, RegionSelectorTestAccess::dimensionInfoRect(selector));
    const QRect selectionGlobalRect = toGlobalRect(selector, selectionRect);
    const QRect regionControlMainGeometry =
        rectInWindowGlobalCoords(regionControlGeometry, regionControlAnchorRect);

    if (!toolbarGeometry.isValid() || toolbarGeometry.isEmpty() ||
        !regionControlGeometry.isValid() || regionControlGeometry.isEmpty() ||
        !regionControlAnchorRect.isValid() || regionControlAnchorRect.isEmpty() ||
        !dimensionInfoGeometry.isValid() || dimensionInfoGeometry.isEmpty()) {
        QSKIP("Attachment geometry did not stabilize in this environment.");
    }

    const QRect defaultExternalRect(
        QPoint(dimensionInfoGeometry.right() + 1 + kAttachmentGap,
               dimensionInfoGeometry.top() +
                   (dimensionInfoGeometry.height() - regionControlAnchorRect.height()) / 2),
        regionControlAnchorRect.size());

    QVERIFY(toolbarGeometry.bottom() < selectionGlobalRect.top());
    QVERIFY(defaultExternalRect.intersects(toolbarGeometry));
    QVERIFY(selectionGlobalRect.contains(regionControlMainGeometry));
    QVERIFY(!regionControlMainGeometry.intersects(toolbarGeometry));
}

void tst_RegionSelectorAttachmentLayout::testRegionControlPanelStaysBesideDimensionLabelWhenOnlyPopupWidthWouldOverlap()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);

    const QRect hostGeometry = screen->availableGeometry().adjusted(40, 40, -40, -120);
    if (hostGeometry.width() < 760 || hostGeometry.height() < 520) {
        QSKIP("Primary screen is too small for popup-width overlap verification.");
    }

    RegionSelector selector;
    prepareCompletedSelection(selector, hostGeometry, QRect(80, hostGeometry.height() - 120, 420, 100));

    const QRect seedToolbarGeometry = RegionSelectorTestAccess::toolbarGeometry(selector);
    const QRect seedRegionControlGeometry = RegionSelectorTestAccess::regionControlGeometry(selector);
    const QRect seedRegionControlAnchorRect = RegionSelectorTestAccess::regionControlAnchorRect(selector);
    const QRect seedDimensionInfoGeometry =
        toGlobalRect(selector, RegionSelectorTestAccess::dimensionInfoRect(selector));

    if (!seedToolbarGeometry.isValid() || seedToolbarGeometry.isEmpty() ||
        !seedRegionControlGeometry.isValid() || seedRegionControlGeometry.isEmpty() ||
        !seedRegionControlAnchorRect.isValid() || seedRegionControlAnchorRect.isEmpty() ||
        !seedDimensionInfoGeometry.isValid() || seedDimensionInfoGeometry.isEmpty()) {
        QSKIP("Seed attachment geometry did not stabilize in this environment.");
    }

    const int targetWidth =
        seedDimensionInfoGeometry.width() +
        kAttachmentGap +
        seedToolbarGeometry.width() +
        (seedRegionControlAnchorRect.width() + seedRegionControlGeometry.width()) / 2;
    if (targetWidth <= 0 || targetWidth > hostGeometry.width() - 120) {
        QSKIP("Could not derive a stable popup-width-only overlap scenario.");
    }

    const QRect selectionRect(80, hostGeometry.height() - 120, targetWidth, 100);
    RegionSelectorTestAccess::setSelectionRect(selector, selectionRect);
    RegionSelectorTestAccess::setCurrentTool(selector, ToolId::Arrow);
    RegionSelectorTestAccess::invokePaint(selector, QRegion(selector.rect()));
    QCoreApplication::processEvents();
    RegionSelectorTestAccess::invokePaint(selector, QRegion(selector.rect()));
    QCoreApplication::processEvents();

    const QRect toolbarGeometry = RegionSelectorTestAccess::toolbarGeometry(selector);
    const QRect regionControlGeometry = RegionSelectorTestAccess::regionControlGeometry(selector);
    const QRect regionControlAnchorRect = RegionSelectorTestAccess::regionControlAnchorRect(selector);
    const QRect dimensionInfoGeometry =
        toGlobalRect(selector, RegionSelectorTestAccess::dimensionInfoRect(selector));
    const QRect selectionGlobalRect = toGlobalRect(selector, selectionRect);
    const QRect regionControlAnchorGeometry =
        rectInWindowGlobalCoords(regionControlGeometry, regionControlAnchorRect);

    if (!toolbarGeometry.isValid() || toolbarGeometry.isEmpty() ||
        !regionControlGeometry.isValid() || regionControlGeometry.isEmpty() ||
        !regionControlAnchorRect.isValid() || regionControlAnchorRect.isEmpty() ||
        !dimensionInfoGeometry.isValid() || dimensionInfoGeometry.isEmpty()) {
        QSKIP("Attachment geometry did not stabilize after popup-width scenario adjustment.");
    }

    const QRect defaultExternalAnchorRect(
        QPoint(dimensionInfoGeometry.right() + 1 + kAttachmentGap,
               dimensionInfoGeometry.top() +
                   (dimensionInfoGeometry.height() - regionControlAnchorRect.height()) / 2),
        regionControlAnchorRect.size());
    const QRect defaultExternalWindowRect(
        defaultExternalAnchorRect.topLeft() - regionControlAnchorRect.topLeft(),
        regionControlGeometry.size());

    QVERIFY(toolbarGeometry.bottom() < selectionGlobalRect.top());
    QVERIFY(defaultExternalWindowRect.intersects(toolbarGeometry));
    QVERIFY(!defaultExternalAnchorRect.intersects(toolbarGeometry));
    QCOMPARE(regionControlAnchorGeometry.topLeft(), defaultExternalAnchorRect.topLeft());
    QVERIFY(regionControlAnchorGeometry.bottom() < selectionGlobalRect.top());
}

void tst_RegionSelectorAttachmentLayout::testWideShortSelectionKeepsRegionControlPanelVisible()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);

    const QRect hostGeometry = screen->availableGeometry().adjusted(40, 40, -40, -120);
    if (hostGeometry.width() < 920 || hostGeometry.height() < 260) {
        QSKIP("Primary screen is too small for wide-short selection verification.");
    }

    const QRect selectionRect(80, hostGeometry.height() - 120, 600, 30);

    RegionSelector selector;
    prepareCompletedSelection(selector, hostGeometry, selectionRect);

    const QRect dimensionInfoGeometry =
        toGlobalRect(selector, RegionSelectorTestAccess::dimensionInfoRect(selector));
    const QRect selectionGlobalRect = toGlobalRect(selector, selectionRect);

    if (!dimensionInfoGeometry.isValid() || dimensionInfoGeometry.isEmpty()) {
        QSKIP("Wide-short attachment geometry did not stabilize in this environment.");
    }

    QVERIFY(RegionSelectorTestAccess::regionControlVisible(selector));
    QVERIFY(dimensionInfoGeometry.bottom() < selectionGlobalRect.top());
    QVERIFY(dimensionInfoGeometry.left() >= selectionGlobalRect.left());
}

void tst_RegionSelectorAttachmentLayout::testTallNarrowSelectionHidesRegionControlPanelButKeepsStandardSizeWidgetPlacement()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);

    const QRect hostGeometry = screen->availableGeometry().adjusted(40, 40, -40, -120);
    if (hostGeometry.width() < 760 || hostGeometry.height() < 420) {
        QSKIP("Primary screen is too small for tall-narrow selection verification.");
    }

    RegionSelector selector;
    prepareCompletedSelection(selector, hostGeometry, QRect(80, hostGeometry.height() - 320, 420, 220));

    const QRect seedRegionControlAnchorRect = RegionSelectorTestAccess::regionControlAnchorRect(selector);
    const QRect seedDimensionInfoGeometry =
        toGlobalRect(selector, RegionSelectorTestAccess::dimensionInfoRect(selector));

    if (!seedRegionControlAnchorRect.isValid() || seedRegionControlAnchorRect.isEmpty() ||
        !seedDimensionInfoGeometry.isValid() || seedDimensionInfoGeometry.isEmpty()) {
        QSKIP("Seed geometry did not stabilize for tall-narrow selection verification.");
    }

    const int targetWidth =
        seedDimensionInfoGeometry.width() + kAttachmentGap + (seedRegionControlAnchorRect.width() / 2);
    if (targetWidth <= seedDimensionInfoGeometry.width() + 10 ||
        targetWidth >= seedDimensionInfoGeometry.width() + kAttachmentGap + seedRegionControlAnchorRect.width()) {
        QSKIP("Could not derive a stable narrow-width-for-control scenario.");
    }

    const QRect selectionRect(80, hostGeometry.height() - 320, targetWidth, 220);
    RegionSelectorTestAccess::setSelectionRect(selector, selectionRect);
    RegionSelectorTestAccess::setCurrentTool(selector, ToolId::Arrow);
    RegionSelectorTestAccess::invokePaint(selector, QRegion(selector.rect()));
    QCoreApplication::processEvents();
    RegionSelectorTestAccess::invokePaint(selector, QRegion(selector.rect()));
    QCoreApplication::processEvents();

    const QRect dimensionInfoGeometry =
        toGlobalRect(selector, RegionSelectorTestAccess::dimensionInfoRect(selector));
    const QRect selectionGlobalRect = toGlobalRect(selector, selectionRect);

    if (!dimensionInfoGeometry.isValid() || dimensionInfoGeometry.isEmpty()) {
        QSKIP("Tall-narrow attachment geometry did not stabilize in this environment.");
    }

    QVERIFY(!RegionSelectorTestAccess::regionControlVisible(selector));
    QCOMPARE(dimensionInfoGeometry.left(), selectionGlobalRect.left());
    QVERIFY(dimensionInfoGeometry.bottom() < selectionGlobalRect.top());
}

void tst_RegionSelectorAttachmentLayout::testSmallRegionHidesRegionControlPanelAndPlacesDimensionLabelLeftOfSelection()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);

    const QRect hostGeometry = screen->availableGeometry().adjusted(40, 40, -40, -120);
    if (hostGeometry.width() < 520 || hostGeometry.height() < 260) {
        QSKIP("Primary screen is too small for compact attachment verification.");
    }

    const QRect selectionRect(80, hostGeometry.height() - 120, 120, 30);

    RegionSelector selector;
    prepareCompletedSelection(selector, hostGeometry, selectionRect);

    const QRect dimensionInfoGeometry =
        toGlobalRect(selector, RegionSelectorTestAccess::dimensionInfoRect(selector));
    const QRect selectionGlobalRect = toGlobalRect(selector, selectionRect);

    if (!dimensionInfoGeometry.isValid() || dimensionInfoGeometry.isEmpty()) {
        QSKIP("Compact attachment geometry did not stabilize in this environment.");
    }

    QVERIFY(!RegionSelectorTestAccess::regionControlVisible(selector));
    QCOMPARE(dimensionInfoGeometry.top(), selectionGlobalRect.top());
    QVERIFY(dimensionInfoGeometry.right() < selectionGlobalRect.left());
    QVERIFY(selectionGlobalRect.left() - dimensionInfoGeometry.right() >= 8);
}

void tst_RegionSelectorAttachmentLayout::testSmallRegionClampsDimensionLabelToViewportLeftWithoutInsideFallback()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);

    const QRect hostGeometry = screen->availableGeometry().adjusted(40, 40, -40, -120);
    if (hostGeometry.width() < 520 || hostGeometry.height() < 220) {
        QSKIP("Primary screen is too small for compact top-edge verification.");
    }

    const QRect selectionRect(6, 80, 120, 30);

    RegionSelector selector;
    prepareCompletedSelection(selector, hostGeometry, selectionRect);

    const QRect dimensionInfoGeometry =
        toGlobalRect(selector, RegionSelectorTestAccess::dimensionInfoRect(selector));
    const QRect selectionGlobalRect = toGlobalRect(selector, selectionRect);

    if (!dimensionInfoGeometry.isValid() || dimensionInfoGeometry.isEmpty()) {
        QSKIP("Compact top-edge attachment geometry did not stabilize in this environment.");
    }

    QVERIFY(!RegionSelectorTestAccess::regionControlVisible(selector));
    QCOMPARE(dimensionInfoGeometry.left(), hostGeometry.left() + 5);
    QCOMPARE(dimensionInfoGeometry.top(), selectionGlobalRect.top());
    QVERIFY(dimensionInfoGeometry.right() >= selectionGlobalRect.left());
    QVERIFY(dimensionInfoGeometry.top() != selectionGlobalRect.top() + 5);
}

QTEST_MAIN(tst_RegionSelectorAttachmentLayout)
#include "tst_AttachmentLayout.moc"
