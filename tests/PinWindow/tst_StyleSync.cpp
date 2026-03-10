#include <QtTest/QtTest>
#include <QApplication>
#include <QGuiApplication>
#include <QCursor>
#include <QMenu>

#include "PinWindow.h"
#include "cursor/CursorManager.h"
#include "qml/QmlWindowedToolbar.h"
#include "qml/QmlFloatingSubToolbar.h"
#include "qml/PinToolOptionsViewModel.h"
#include "tools/ToolManager.h"

namespace {

QPixmap createPixmap()
{
    QPixmap pixmap(120, 80);
    pixmap.fill(Qt::white);
    return pixmap;
}

}  // namespace

class TestPinWindowStyleSync : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testArrowStyleChangeUpdatesViewModelAndToolManager();
    void testLineStyleChangeUpdatesViewModelAndToolManager();
    void testDropdownLifecycleRestoresToolCursor();
    void testQueuedFloatingUiRestoreClearsArrowOverride();
    void testPopupAwayFromCursorKeepsAnnotationToolCursor();
};

void TestPinWindowStyleSync::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for PinWindow tests in this environment.");
    }
}

void TestPinWindowStyleSync::testArrowStyleChangeUpdatesViewModelAndToolManager()
{
    PinWindow window(createPixmap(), QPoint(0, 0), nullptr, false);
    window.initializeAnnotationComponents();

    QVERIFY(window.m_subToolbar);
    QVERIFY(window.m_toolManager);

    window.onArrowStyleChanged(LineEndStyle::BothArrowOutline);

    QCOMPARE(window.m_subToolbar->viewModel()->arrowStyle(),
        static_cast<int>(LineEndStyle::BothArrowOutline));
    QCOMPARE(static_cast<int>(window.m_toolManager->arrowStyle()),
        static_cast<int>(LineEndStyle::BothArrowOutline));
}

void TestPinWindowStyleSync::testLineStyleChangeUpdatesViewModelAndToolManager()
{
    PinWindow window(createPixmap(), QPoint(0, 0), nullptr, false);
    window.initializeAnnotationComponents();

    QVERIFY(window.m_subToolbar);
    QVERIFY(window.m_toolManager);

    window.onLineStyleChanged(LineStyle::Dotted);

    QCOMPARE(window.m_subToolbar->viewModel()->lineStyle(),
        static_cast<int>(LineStyle::Dotted));
    QCOMPARE(static_cast<int>(window.m_toolManager->lineStyle()),
        static_cast<int>(LineStyle::Dotted));
}

void TestPinWindowStyleSync::testDropdownLifecycleRestoresToolCursor()
{
    PinWindow window(createPixmap(), QPoint(0, 0), nullptr, false);
    window.initializeAnnotationComponents();
    window.enterAnnotationMode();

    QVERIFY(window.m_settingsHelper);

    const QPoint center = window.rect().center();
    QCursor::setPos(window.mapToGlobal(center));
    QTRY_VERIFY(window.rect().contains(window.mapFromGlobal(QCursor::pos())));

    auto& cursorManager = CursorManager::instance();
    cursorManager.reapplyCursorForWidget(&window);

    const Qt::CursorShape expectedToolCursorShape = window.cursor().shape();
    QVERIFY(expectedToolCursorShape != Qt::ArrowCursor);

    cursorManager.pushCursorForWidget(&window, CursorContext::Override, Qt::ArrowCursor);
    cursorManager.reapplyCursorForWidget(&window);
    QCOMPARE(window.cursor().shape(), Qt::ArrowCursor);

    window.syncFloatingUiCursor();
    QTRY_COMPARE(window.cursor().shape(), expectedToolCursorShape);
}

void TestPinWindowStyleSync::testQueuedFloatingUiRestoreClearsArrowOverride()
{
    PinWindow window(createPixmap(), QPoint(0, 0), nullptr, false);
    window.initializeAnnotationComponents();
    window.enterAnnotationMode();

    QVERIFY(window.m_toolbar);

    const QPoint center = window.rect().center();
    QCursor::setPos(window.mapToGlobal(center));
    QTRY_VERIFY(window.rect().contains(window.mapFromGlobal(QCursor::pos())));

    auto& cursorManager = CursorManager::instance();
    window.updateCursorForTool();

    const Qt::CursorShape expectedToolCursorShape = window.cursor().shape();
    QVERIFY(expectedToolCursorShape != Qt::ArrowCursor);

    cursorManager.pushCursorForWidget(&window, CursorContext::Override, Qt::ArrowCursor);
    cursorManager.reapplyCursorForWidget(&window);
    QCOMPARE(window.cursor().shape(), Qt::ArrowCursor);

    window.m_toolbar->cursorRestoreRequested();
    QTRY_COMPARE(window.cursor().shape(), expectedToolCursorShape);
}

void TestPinWindowStyleSync::testPopupAwayFromCursorKeepsAnnotationToolCursor()
{
    PinWindow window(createPixmap(), QPoint(0, 0), nullptr, false);
    window.initializeAnnotationComponents();
    window.enterAnnotationMode();
    window.show();
    QTRY_VERIFY(window.isVisible());

    QVERIFY(window.m_toolManager);

    window.m_currentToolId = ToolId::Pencil;
    window.m_toolManager->setCurrentTool(ToolId::Pencil);
    window.updateCursorForTool();

    const QPoint center = window.rect().center();
    QCursor::setPos(window.mapToGlobal(center));
    QTRY_VERIFY(window.rect().contains(window.mapFromGlobal(QCursor::pos())));

    auto& cursorManager = CursorManager::instance();
    cursorManager.reapplyCursorForWidget(&window);

    const Qt::CursorShape expectedToolCursorShape = window.cursor().shape();
    QVERIFY(expectedToolCursorShape != Qt::ArrowCursor);

    QMenu popup(&window);
    popup.addAction(QStringLiteral("Solid"));
    popup.addAction(QStringLiteral("Dashed"));
    popup.popup(window.mapToGlobal(QPoint(12, 12)));
    QTRY_VERIFY(popup.isVisible());
    QTRY_COMPARE(QApplication::activePopupWidget(), static_cast<QWidget*>(&popup));
    QVERIFY(!popup.frameGeometry().contains(QCursor::pos()));

    window.syncFloatingUiCursor();
    QTRY_COMPARE(window.cursor().shape(), expectedToolCursorShape);

    popup.close();
    QTRY_VERIFY(!popup.isVisible());
}

QTEST_MAIN(TestPinWindowStyleSync)
#include "tst_StyleSync.moc"
