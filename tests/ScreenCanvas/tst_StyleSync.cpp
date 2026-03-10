#include <QtTest/QtTest>

#include "ScreenCanvas.h"
#include "cursor/CursorManager.h"
#include "tools/ToolManager.h"

#include <QApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QMenu>

class TestScreenCanvasStyleSync : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testPopupAwayFromCursorKeepsCanvasToolCursor();
    void testPopupUnderCursorUsesArrowCursor();
};

void TestScreenCanvasStyleSync::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for ScreenCanvas tests in this environment.");
    }
}

void TestScreenCanvasStyleSync::testPopupAwayFromCursorKeepsCanvasToolCursor()
{
    ScreenCanvas canvas;
    canvas.resize(420, 260);
    canvas.show();
    QTRY_VERIFY(canvas.isVisible());

    QVERIFY(canvas.m_toolManager);

    canvas.m_currentToolId = ToolId::Pencil;
    canvas.m_toolManager->setCurrentTool(ToolId::Pencil);
    canvas.setToolCursor();

    const QPoint canvasCenter = canvas.rect().center();
    QCursor::setPos(canvas.mapToGlobal(canvasCenter));
    QTRY_VERIFY(canvas.rect().contains(canvas.mapFromGlobal(QCursor::pos())));

    auto& cursorManager = CursorManager::instance();
    cursorManager.reapplyCursorForWidget(&canvas);

    const Qt::CursorShape expectedToolCursorShape = canvas.cursor().shape();
    QVERIFY(expectedToolCursorShape != Qt::ArrowCursor);

    QMenu popup(&canvas);
    popup.addAction(QStringLiteral("Solid"));
    popup.addAction(QStringLiteral("Dashed"));
    popup.popup(canvas.mapToGlobal(QPoint(12, 12)));
    QTRY_VERIFY(popup.isVisible());
    QTRY_COMPARE(QApplication::activePopupWidget(), static_cast<QWidget*>(&popup));
    QVERIFY(!popup.frameGeometry().contains(QCursor::pos()));

    canvas.syncFloatingUiCursor();
    QTRY_COMPARE(canvas.cursor().shape(), expectedToolCursorShape);

    popup.close();
    QTRY_VERIFY(!popup.isVisible());
}

void TestScreenCanvasStyleSync::testPopupUnderCursorUsesArrowCursor()
{
    ScreenCanvas canvas;
    canvas.resize(420, 260);
    canvas.show();
    QTRY_VERIFY(canvas.isVisible());

    QVERIFY(canvas.m_toolManager);

    canvas.m_currentToolId = ToolId::Pencil;
    canvas.m_toolManager->setCurrentTool(ToolId::Pencil);
    canvas.setToolCursor();

    QMenu popup(&canvas);
    popup.addAction(QStringLiteral("Solid"));
    popup.addAction(QStringLiteral("Dashed"));
    popup.popup(canvas.mapToGlobal(QPoint(12, 12)));
    QTRY_VERIFY(popup.isVisible());

    const QPoint popupCenter = popup.frameGeometry().center();
    QCursor::setPos(popupCenter);
    QTRY_VERIFY(popup.frameGeometry().contains(QCursor::pos()));

    canvas.syncFloatingUiCursor();
    QTRY_COMPARE(canvas.cursor().shape(), Qt::ArrowCursor);

    popup.close();
    QTRY_VERIFY(!popup.isVisible());
}

QTEST_MAIN(TestScreenCanvasStyleSync)
#include "tst_StyleSync.moc"
