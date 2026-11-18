#include <QtTest>
#include <QAction>
#include <QMenu>
#include <QtQml/qqmlextensionplugin.h>

#include "PinWindow.h"
#include "PlatformFeatures.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/PolylineAnnotation.h"
#include "beautify/BeautifyRenderer.h"
#include "beautify/BeautifySettings.h"
#include "qml/ImageExportDialogController.h"
#include "qml/ImageExportViewModel.h"
#include "settings/Settings.h"

#include <QImageReader>
#include <QSignalSpy>
#include <QTemporaryDir>

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

// #225: the pin context menu, grouped by task.
class TestPinContextMenuLayout : public QObject
{
    Q_OBJECT
private slots:
    // Each case starts from default export options (settings are per test process).
    void init()
    {
        auto settings = SnapTray::getSettings();
        settings.remove(QStringLiteral("files/imageExport"));
    }
    void testContextMenuGroups();
    void testZoomAndOpacityResetItems();
    void testShortcutHintsShown();
    void exportOpensWithComposedImageAndLeavesPinAlone();
    void beautifyExportUsesTheBeautifiedImage();

private:
    static QPixmap pixmap()
    {
        QPixmap p(240, 160);
        p.fill(Qt::darkYellow);
        return p;
    }
    static QList<QStringList> groupsOf(QMenu* menu)
    {
        QList<QStringList> groups{{}};
        for (QAction* action : menu->actions()) {
            if (action->isSeparator()) {
                groups.append(QStringList());
                continue;
            }
            groups.last().append(action->objectName());
        }
        return groups;
    }
    static QAction* findAction(QMenu* menu, const QString& name)
    {
        for (QAction* action : menu->findChildren<QAction*>())
            if (action->objectName() == name) return action;
        for (QAction* action : menu->actions())
            if (action->objectName() == name) return action;
        return nullptr;
    }
};

void TestPinContextMenuLayout::testContextMenuGroups()
{
    PinWindow window(pixmap(), QPoint(0, 0), nullptr, false, false);
    window.createContextMenu();
    QStringList modes = {"pinMenuLive", "pinMenuLivePause", "pinMenuFrameRate", "pinMenuRegionLayout"};
    if (PlatformFeatures::instance().capabilities().supportsClickThrough)
        modes.prepend("pinMenuClickThrough");
    // Arrange (#268) leads; it is hidden unless an annotation is selected.
    const QList<QStringList> expected = {
        {"pinMenuArrange"},
        {"pinMenuShowToolbar", "pinMenuShowBorder", "pinMenuZoom", "pinMenuOpacity", "pinMenuImageProcessing"},
        {"pinMenuCopy", "pinMenuSave", "pinMenuExport", "pinMenuWatermark", "pinMenuInfo", "pinMenuHistoryFolder"},
        modes,
        {"pinMenuMoveToScreen", "pinMenuMerge", "pinMenuCloseAll"},
        {"pinMenuClose"}};
    QCOMPARE(groupsOf(window.m_contextMenu), expected);
}

void TestPinContextMenuLayout::testZoomAndOpacityResetItems()
{
    PinWindow window(pixmap(), QPoint(0, 0), nullptr, false, false);
    window.createContextMenu();
    window.setZoomLevel(2.0);
    window.setOpacity(0.4);
    QAction* resetZoom = findAction(window.m_contextMenu, "pinMenuZoomReset");
    QAction* resetOpacity = findAction(window.m_contextMenu, "pinMenuOpacityReset");
    QVERIFY(resetZoom && resetOpacity);
    resetZoom->trigger();
    resetOpacity->trigger();
    QCOMPARE(window.zoomLevel(), 1.0);
    QCOMPARE(window.opacity(), 1.0);
}

void TestPinContextMenuLayout::testShortcutHintsShown()
{
    PinWindow window(pixmap(), QPoint(0, 0), nullptr, false, false);
    window.createContextMenu();
    for (const char* name : {"pinMenuZoomReset", "pinMenuZoomIn", "pinMenuOpacityReset", "pinMenuLive", "pinMenuLivePause"})
        QVERIFY2(findAction(window.m_contextMenu, QString::fromLatin1(name)), name);
    QCOMPARE(findAction(window.m_contextMenu, "pinMenuZoomReset")->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_0));
    QCOMPARE(findAction(window.m_contextMenu, "pinMenuZoomIn")->shortcut(), QKeySequence(QKeySequence::ZoomIn));
    QCOMPARE(findAction(window.m_contextMenu, "pinMenuOpacityReset")->shortcut(),
             QKeySequence(Qt::CTRL | Qt::Key_BracketRight));
    QCOMPARE(findAction(window.m_contextMenu, "pinMenuLive")->shortcut(), QKeySequence(Qt::Key_L));
    QCOMPARE(findAction(window.m_contextMenu, "pinMenuLivePause")->shortcut(), QKeySequence(Qt::Key_P));
}

// #357: Export As... hands the dialog what Save would write (annotations
// included) and changes neither the pin nor its annotations.
void TestPinContextMenuLayout::exportOpensWithComposedImageAndLeavesPinAlone()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    PinWindow window(pixmap(), QPoint(0, 0), nullptr, false, false);
    window.createContextMenu();
    window.showToolbar();
    QVERIFY(window.m_annotationLayer);
    window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
        QVector<QPoint>{QPoint(20, 20), QPoint(200, 140)}, QColor(Qt::red), 6,
        LineEndStyle::None, LineStyle::Solid));
    const QImage originalBefore = window.m_originalPixmap.toImage();
    const QImage composed = window.getExportPixmapWithAnnotations().toImage();
    QVERIFY(composed != originalBefore.convertToFormat(composed.format()));

    auto* controller = window.imageExportController();
    controller->setShowDialogForTesting(false);
    const QString target = dir.filePath("pin.png");
    controller->setSaveFileDialogForTesting(
        [&](const QString&, const QString&, const QString&) { return target; });
    QSignalSpy saved(&window, &PinWindow::saveCompleted);

    QAction* exportAction = findAction(window.m_contextMenu, "pinMenuExport");
    QVERIFY(exportAction);
    exportAction->trigger();
    QVERIFY(controller->isOpen());
    auto* vm = controller->viewModel();
    QCOMPARE(vm->outputPixelSize(), composed.size());
    vm->setResizeMode(int(ImageExportOptions::Resize::Percent));
    vm->setPercent(50);
    QTRY_VERIFY(vm->canSave());
    vm->save();

    QCOMPARE(saved.count(), 1);
    QCOMPARE(saved.first().at(1).toString(), target);
    const QImage written(target);
    QCOMPARE(written.size(), composed.size() / 2);
    // The red stroke is in the exported pixels.
    const QColor onStroke = written.pixelColor(55, 40);
    QVERIFY2(onStroke.red() > 180 && onStroke.green() < 110, qPrintable(onStroke.name()));

    // Pin image, size and annotation are as they were.
    QCOMPARE(window.m_originalPixmap.toImage(), originalBefore);
    QCOMPARE(window.size(), QSize(240, 160));
    QCOMPARE(window.m_annotationLayer->itemCount(), size_t(1));
    QCOMPARE(window.getExportPixmapWithAnnotations().toImage(), composed);

    // An open dialog holds a snapshot. Editing the pin closes it, so an
    // out-of-date picture is never exported.
    exportAction->trigger();
    QVERIFY(controller->isOpen());
    window.m_annotationLayer->addItem(std::make_unique<PolylineAnnotation>(
        QVector<QPoint>{QPoint(30, 120), QPoint(90, 130)}, QColor(Qt::blue), 4,
        LineEndStyle::None, LineStyle::Solid));
    QVERIFY(!controller->isOpen());
    exportAction->trigger();
    QVERIFY(controller->isOpen());
    window.rotateRight();
    QVERIFY(!controller->isOpen());
}

void TestPinContextMenuLayout::beautifyExportUsesTheBeautifiedImage()
{
    PinWindow window(pixmap(), QPoint(0, 0), nullptr, false, false);
    auto* controller = window.imageExportController();
    controller->setShowDialogForTesting(false);

    BeautifySettings settings;
    settings.padding = 40;
    const QSize expected =
        BeautifyRenderer::applyToPixmap(window.getExportPixmapWithAnnotations(), settings).size();
    QVERIFY(expected.width() > 240);

    window.onBeautifyExport(settings);
    QVERIFY(controller->isOpen());
    QCOMPARE(controller->viewModel()->outputPixelSize(), expected);
    controller->close();
    QCOMPARE(window.m_originalPixmap.size(), QSize(240, 160));
}

QTEST_MAIN(TestPinContextMenuLayout)
#include "tst_ContextMenuLayout.moc"
