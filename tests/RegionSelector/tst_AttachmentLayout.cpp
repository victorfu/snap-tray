#include <QtTest>

#include <QGuiApplication>
#include <QScreen>
#include <QtQml/qqmlextensionplugin.h>

#include "RegionSelector.h"
#include "RegionSelectorTestAccess.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

namespace {

constexpr int kControlMainYOffset = 32;
constexpr int kControlMainHeight = 28;
constexpr int kAttachmentGap = 8;

QRect toGlobalRect(const QWidget& widget, const QRect& localRect)
{
    return localRect.isValid() && !localRect.isEmpty()
        ? QRect(widget.mapToGlobal(localRect.topLeft()), localRect.size())
        : QRect();
}

QRect regionControlMainRect(const QRect& controlGeometry)
{
    return controlGeometry.isValid() && !controlGeometry.isEmpty()
        ? QRect(controlGeometry.left(),
                controlGeometry.top() + kControlMainYOffset,
                controlGeometry.width(),
                kControlMainHeight)
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
    const QRect dimensionInfoGeometry =
        toGlobalRect(selector, RegionSelectorTestAccess::dimensionInfoRect(selector));
    const QRect selectionGlobalRect = toGlobalRect(selector, selectionRect);
    const QRect regionControlMainGeometry = regionControlMainRect(regionControlGeometry);

    if (!toolbarGeometry.isValid() || toolbarGeometry.isEmpty() ||
        !regionControlGeometry.isValid() || regionControlGeometry.isEmpty() ||
        !dimensionInfoGeometry.isValid() || dimensionInfoGeometry.isEmpty()) {
        QSKIP("Attachment geometry did not stabilize in this environment.");
    }

    const int popupOverhead = regionControlGeometry.height() - kControlMainHeight;
    const QRect defaultExternalRect(
        QPoint(dimensionInfoGeometry.right() + 1 + kAttachmentGap,
               dimensionInfoGeometry.top() +
                   (dimensionInfoGeometry.height() - kControlMainHeight) / 2 -
                   popupOverhead),
        regionControlGeometry.size());

    QVERIFY(toolbarGeometry.bottom() < selectionGlobalRect.top());
    QVERIFY(defaultExternalRect.intersects(toolbarGeometry));
    QVERIFY(selectionGlobalRect.contains(regionControlMainGeometry));
    QVERIFY(!regionControlMainGeometry.intersects(toolbarGeometry));
}

QTEST_MAIN(tst_RegionSelectorAttachmentLayout)
#include "tst_AttachmentLayout.moc"
