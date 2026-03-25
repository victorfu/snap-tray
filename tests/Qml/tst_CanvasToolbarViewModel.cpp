#include <QtTest/QtTest>

#include "qml/CanvasToolbarViewModel.h"

#include <QSignalSpy>

namespace {

QVariantMap findButtonByIconSource(const QVariantList& buttons, const QString& iconSource)
{
    for (const QVariant& button : buttons) {
        const QVariantMap entry = button.toMap();
        if (entry.value(QStringLiteral("iconSource")).toString() == iconSource) {
            return entry;
        }
    }

    return {};
}

int indexOfButtonId(const QVariantList& buttons, int buttonId)
{
    for (int index = 0; index < buttons.size(); ++index) {
        if (buttons.at(index).toMap().value(QStringLiteral("id")).toInt() == buttonId) {
            return index;
        }
    }

    return -1;
}

} // namespace

class tst_CanvasToolbarViewModel : public QObject
{
    Q_OBJECT

private slots:
    void testLaserPointerInsertedBeforeCanvasModeToggle();
    void testLaserPointerButtonRoutesThroughToolSelected();
    void testCopyButtonEmitsCopySignal();
    void testCanvasModeButtonsEmitActionSignals();
};

void tst_CanvasToolbarViewModel::testLaserPointerInsertedBeforeCanvasModeToggle()
{
    CanvasToolbarViewModel viewModel;
    const QVariantList buttons = viewModel.buttons();

    const QVariantMap laserButton = findButtonByIconSource(
        buttons, QStringLiteral("qrc:/icons/icons/laser-pointer.svg"));
    QVERIFY(!laserButton.isEmpty());

    const int laserIndex = indexOfButtonId(
        buttons, laserButton.value(QStringLiteral("id")).toInt());
    const int whiteboardIndex = indexOfButtonId(
        buttons, static_cast<int>(ToolId::CanvasWhiteboard));

    QVERIFY(laserIndex >= 0);
    QVERIFY(whiteboardIndex >= 0);
    QVERIFY(laserIndex < whiteboardIndex);
}

void tst_CanvasToolbarViewModel::testLaserPointerButtonRoutesThroughToolSelected()
{
    CanvasToolbarViewModel viewModel;
    const QVariantMap laserButton = findButtonByIconSource(
        viewModel.buttons(), QStringLiteral("qrc:/icons/icons/laser-pointer.svg"));
    QVERIFY(!laserButton.isEmpty());

    const int laserButtonId = laserButton.value(QStringLiteral("id")).toInt();
    QSignalSpy toolSpy(&viewModel, &CanvasToolbarViewModel::toolSelected);

    viewModel.handleButtonClicked(laserButtonId);

    QCOMPARE(toolSpy.count(), 1);
    QCOMPARE(toolSpy.at(0).at(0).toInt(), laserButtonId);
}

void tst_CanvasToolbarViewModel::testCopyButtonEmitsCopySignal()
{
    CanvasToolbarViewModel viewModel;
    const int copyButtonId = static_cast<int>(ToolId::Copy);
    QVERIFY(indexOfButtonId(viewModel.buttons(), copyButtonId) >= 0);

    QSignalSpy toolSpy(&viewModel, &CanvasToolbarViewModel::toolSelected);
    QSignalSpy copySpy(&viewModel, &CanvasToolbarViewModel::copyClicked);

    viewModel.handleButtonClicked(copyButtonId);

    QCOMPARE(toolSpy.count(), 0);
    QCOMPARE(copySpy.count(), 1);
}

void tst_CanvasToolbarViewModel::testCanvasModeButtonsEmitActionSignals()
{
    CanvasToolbarViewModel viewModel;
    QSignalSpy toolSpy(&viewModel, &CanvasToolbarViewModel::toolSelected);
    QSignalSpy whiteboardSpy(&viewModel, &CanvasToolbarViewModel::canvasWhiteboardClicked);
    QSignalSpy blackboardSpy(&viewModel, &CanvasToolbarViewModel::canvasBlackboardClicked);

    viewModel.handleButtonClicked(static_cast<int>(ToolId::CanvasWhiteboard));
    viewModel.handleButtonClicked(static_cast<int>(ToolId::CanvasBlackboard));

    QCOMPARE(toolSpy.count(), 0);
    QCOMPARE(whiteboardSpy.count(), 1);
    QCOMPARE(blackboardSpy.count(), 1);
}

QTEST_MAIN(tst_CanvasToolbarViewModel)
#include "tst_CanvasToolbarViewModel.moc"
