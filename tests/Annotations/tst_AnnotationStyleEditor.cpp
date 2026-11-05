#include <QtTest/QtTest>

#include "annotation/AnnotationStyleEditor.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/TextBoxAnnotation.h"
#include "qml/PinToolOptionsViewModel.h"
#include "tools/ToolId.h"

class tst_AnnotationStyleEditor : public QObject
{
    Q_OBJECT
private slots:
    void editsSelectedArrowWithUndo();
    void idleEndsCoalescing();
    void deselectRestoresDefaults();
    void targetFollowsIdAfterIndexShift();
    void inlineTextEditingBypassesSelectionTarget();
    void nonStylableSelectionHasNoTarget();
};

namespace {
std::unique_ptr<ArrowAnnotation> redArrow(int y = 0, int width = 3)
{
    return std::make_unique<ArrowAnnotation>(QPoint(0, y), QPoint(50, y), Qt::red, width);
}
ArrowAnnotation* arrowAt(AnnotationLayer& layer, int index)
{
    return static_cast<ArrowAnnotation*>(layer.itemAt(index));
}
} // namespace

void tst_AnnotationStyleEditor::editsSelectedArrowWithUndo()
{
    AnnotationLayer layer;
    PinToolOptionsViewModel vm;
    AnnotationStyleEditor editor(&layer, &vm);
    QSignalSpy applied(&editor, &AnnotationStyleEditor::styleApplied);
    layer.addItem(redArrow());
    layer.setSelectedIndex(0);
    QVERIFY(editor.hasTarget());
    QCOMPARE(vm.targetLabel(), QStringLiteral("Selected: Arrow"));
    vm.handleWidthChanged(5);
    vm.handleWidthChanged(6);
    vm.handleWidthChanged(7);
    QCOMPARE(arrowAt(layer, 0)->width(), 7);
    QCOMPARE(applied.count(), 3);
    QCOMPARE(layer.historyCommandCount(), size_t(2)); // Add + one coalesced Modify
    layer.undo();
    QCOMPARE(arrowAt(layer, 0)->width(), 3);
    QCOMPARE(vm.currentWidth(), 3); // strip follows the undone state
    QVERIFY(editor.hasTarget());
    layer.redo();
    QCOMPARE(vm.currentWidth(), 7);
}

void tst_AnnotationStyleEditor::idleEndsCoalescing()
{
    AnnotationLayer layer;
    PinToolOptionsViewModel vm;
    AnnotationStyleEditor editor(&layer, &vm);
    layer.addItem(redArrow());
    layer.setSelectedIndex(0);
    vm.handleWidthChanged(5);
    QTest::qWait(AnnotationStyleEditor::kCoalesceIdleMs + 150);
    vm.handleWidthChanged(6);
    QCOMPARE(layer.historyCommandCount(), size_t(3));
}

void tst_AnnotationStyleEditor::deselectRestoresDefaults()
{
    AnnotationLayer layer;
    PinToolOptionsViewModel vm;
    vm.showForTool(int(ToolId::Arrow));
    vm.setCurrentWidth(2);
    AnnotationStyleEditor editor(&layer, &vm);
    QSignalSpy cleared(&editor, &AnnotationStyleEditor::targetCleared);
    QSignalSpy shown(&editor, &AnnotationStyleEditor::targetShown);
    layer.addItem(redArrow(0, 9));
    layer.setSelectedIndex(0);
    QCOMPARE(shown.count(), 1);
    QCOMPARE(vm.currentWidth(), 9);
    layer.clearSelection();
    QCOMPARE(cleared.count(), 1);
    QVERIFY(!editor.hasTarget());
    QVERIFY(!vm.editingSelection());
    QCOMPARE(vm.currentWidth(), 2);
}

void tst_AnnotationStyleEditor::targetFollowsIdAfterIndexShift()
{
    AnnotationLayer layer;
    PinToolOptionsViewModel vm;
    AnnotationStyleEditor editor(&layer, &vm);
    layer.addItem(redArrow(0));
    layer.addItem(redArrow(40));
    const auto id = layer.idAt(1);
    layer.setSelectedIndex(1);
    // Erase the first arrow; the selected one moves to index 0.
    layer.beginEraseTransaction();
    auto removed = layer.removeItemsIntersecting(QPoint(25, 0), 6);
    QCOMPARE(removed.size(), size_t(1));
    QVERIFY(layer.commitEraseTransaction(std::move(removed)));
    QCOMPARE(layer.indexOfId(id), 0);
    vm.handleColorClicked(1);
    const QColor expected = vm.colorPalette().at(1).value<QColor>();
    QCOMPARE(arrowAt(layer, layer.indexOfId(id))->color(), expected);
}

void tst_AnnotationStyleEditor::inlineTextEditingBypassesSelectionTarget()
{
    AnnotationLayer layer;
    PinToolOptionsViewModel vm;
    AnnotationStyleEditor editor(&layer, &vm);
    bool editing = true;
    editor.setInlineEditingPredicate([&editing] { return editing; });
    layer.addItem(std::make_unique<TextBoxAnnotation>(QPointF(0, 0), QStringLiteral("Hi"),
                                                      QFont(), Qt::black));
    layer.setSelectedIndex(0);
    QVERIFY(!editor.hasTarget());
    QVERIFY(!vm.editingSelection());
    QSignalSpy defaultBold(&vm, &PinToolOptionsViewModel::boldToggled);
    vm.handleBoldToggled();
    QCOMPARE(defaultBold.count(), 1); // the inline editor path, not a Modify
    QCOMPARE(layer.historyCommandCount(), size_t(1));
    editing = false;
    editor.refreshTarget();
    QVERIFY(editor.hasTarget());
}

void tst_AnnotationStyleEditor::nonStylableSelectionHasNoTarget()
{
    AnnotationLayer layer;
    PinToolOptionsViewModel vm;
    AnnotationStyleEditor editor(&layer, &vm);
    layer.addItem(std::make_unique<EmojiStickerAnnotation>(QPoint(0, 0), QStringLiteral("x")));
    layer.setSelectedIndex(0);
    QVERIFY(!editor.hasTarget());
    QVERIFY(!vm.editingSelection());
}

QTEST_MAIN(tst_AnnotationStyleEditor)
#include "tst_AnnotationStyleEditor.moc"
