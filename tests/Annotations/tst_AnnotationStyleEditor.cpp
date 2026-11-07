#include <QtTest/QtTest>

#include "annotation/AnnotationStyleEditor.h"
#include "InlineTextEditor.h"
#include "TextFormattingState.h"
#include <QWidget>
#include "annotations/AnnotationZOrderCommands.h"
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
    void defaultFontIsUndoable();
    void idleEndsCoalescing();
    void deselectRestoresDefaults();
    void targetFollowsIdAfterIndexShift();
    void inlineTextEditingBypassesSelectionTarget();
    void nonStylableSelectionHasNoTarget();
    void arrangeButtonsMoveSelection();
    void zOrderShortcutsMapToMoves();
    void zOrderKeyNeedsSelection();
    void zOrderKeyMovesNonStylableSelection();
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

void tst_AnnotationStyleEditor::arrangeButtonsMoveSelection()
{
    AnnotationLayer layer;
    PinToolOptionsViewModel vm;
    AnnotationStyleEditor editor(&layer, &vm);
    QSignalSpy applied(&editor, &AnnotationStyleEditor::styleApplied);
    layer.addItem(redArrow(0));
    layer.addItem(redArrow(0));
    const auto bottomId = layer.idAt(0);
    layer.setSelectedIndex(0);
    QVERIFY(vm.canRaiseSelection());
    QVERIFY(!vm.canLowerSelection());
    QCOMPARE(vm.zOrderOptions().size(), 4);

    vm.handleZOrderRequested(int(AnnotationLayer::ZOrderMove::ToFront));
    QCOMPARE(layer.idAt(1), bottomId);
    QCOMPARE(applied.count(), 1);
    QVERIFY(!vm.canRaiseSelection());
    QVERIFY(vm.canLowerSelection());

    // At the front a further request changes nothing.
    vm.handleZOrderRequested(int(AnnotationLayer::ZOrderMove::Forward));
    QCOMPARE(applied.count(), 1);

    layer.undo();
    QCOMPARE(layer.idAt(0), bottomId);
    QVERIFY(vm.canRaiseSelection());
}

void tst_AnnotationStyleEditor::zOrderShortcutsMapToMoves()
{
    using Move = AnnotationLayer::ZOrderMove;
#ifdef Q_OS_MACOS
    const Qt::KeyboardModifiers toEnd = Qt::ControlModifier | Qt::AltModifier;
#else
    const Qt::KeyboardModifiers toEnd = Qt::ControlModifier | Qt::ShiftModifier;
#endif
    const auto moveFor = [](Qt::Key key, Qt::KeyboardModifiers modifiers) {
        QKeyEvent event(QEvent::KeyPress, key, modifiers);
        return AnnotationZOrderCommands::moveForKeyEvent(&event);
    };
    QCOMPARE(moveFor(Qt::Key_BracketRight, Qt::ControlModifier), std::optional<Move>(Move::Forward));
    QCOMPARE(moveFor(Qt::Key_BracketLeft, Qt::ControlModifier), std::optional<Move>(Move::Backward));
    QCOMPARE(moveFor(Qt::Key_BracketRight, toEnd), std::optional<Move>(Move::ToFront));
    QCOMPARE(moveFor(Qt::Key_BracketLeft, toEnd), std::optional<Move>(Move::ToBack));
    // Shift turns the bracket into a brace on most layouts.
    QCOMPARE(moveFor(Qt::Key_BraceRight, toEnd), std::optional<Move>(Move::ToFront));
    QCOMPARE(moveFor(Qt::Key_BraceLeft, toEnd), std::optional<Move>(Move::ToBack));
    QVERIFY(!moveFor(Qt::Key_BracketRight, Qt::NoModifier));
    QVERIFY(!moveFor(Qt::Key_BracketRight, Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier));
    QVERIFY(!moveFor(Qt::Key_A, Qt::ControlModifier));

    // Each displayed shortcut triggers its own move.
    for (const Move move : {Move::Forward, Move::Backward, Move::ToFront, Move::ToBack}) {
        const QKeyCombination combination = AnnotationZOrderCommands::shortcut(move)[0];
        QCOMPARE(moveFor(combination.key(), combination.keyboardModifiers()), std::optional<Move>(move));
        QVERIFY(!AnnotationZOrderCommands::actionText(move).isEmpty());
        QVERIFY(!AnnotationZOrderCommands::announcement(move).isEmpty());
    }
}

void tst_AnnotationStyleEditor::zOrderKeyNeedsSelection()
{
    AnnotationLayer layer;
    PinToolOptionsViewModel vm;
    AnnotationStyleEditor editor(&layer, &vm);
    layer.addItem(redArrow(0));
    layer.addItem(redArrow(0));
    QKeyEvent forward(QEvent::KeyPress, Qt::Key_BracketRight, Qt::ControlModifier);
    // No selection: the key is left for the host (a pin resets opacity).
    QVERIFY(!editor.handleZOrderKey(&forward));

    layer.setSelectedIndex(1);
    const std::size_t history = layer.historyCommandCount();
    // Already in front: consumed, but records nothing.
    QVERIFY(editor.handleZOrderKey(&forward));
    QCOMPARE(layer.historyCommandCount(), history);
}

void tst_AnnotationStyleEditor::zOrderKeyMovesNonStylableSelection()
{
    AnnotationLayer layer;
    PinToolOptionsViewModel vm;
    AnnotationStyleEditor editor(&layer, &vm);
    layer.addItem(std::make_unique<EmojiStickerAnnotation>(QPoint(20, 20), QStringLiteral("A")));
    layer.addItem(redArrow(20));
    const auto emojiId = layer.idAt(0);
    layer.setSelectedIndex(0);
    QVERIFY(!editor.hasTarget());
    QKeyEvent forward(QEvent::KeyPress, Qt::Key_BracketRight, Qt::ControlModifier);
    QVERIFY(editor.handleZOrderKey(&forward));
    QCOMPARE(layer.idAt(1), emojiId);
}

QTEST_MAIN(tst_AnnotationStyleEditor)
#include "tst_AnnotationStyleEditor.moc"

void tst_AnnotationStyleEditor::defaultFontIsUndoable()
{
    AnnotationLayer layer;
    PinToolOptionsViewModel vm;
    AnnotationStyleEditor editor(&layer, &vm);
    QFont font(QStringLiteral("Courier"), 24);
    font.setBold(true);
    font.setItalic(true);
    layer.addItem(std::make_unique<TextBoxAnnotation>(QPointF(10, 20), QStringLiteral("sample"), font, Qt::red));
    layer.setSelectedIndex(0);
    auto* text = static_cast<TextBoxAnnotation*>(layer.selectedItem());
    QVERIFY(vm.routeToSelection(StyleProperty::FontFamily, QString()));
    QWidget host;
    InlineTextEditor inlineEditor(&host);
    inlineEditor.setFont(font);
    inlineEditor.setFontFamily(QString());
    QCOMPARE(text->font(), inlineEditor.font());
    auto defaults = TextFormattingState::fromQFont(font);
    defaults.fontFamily.clear();
    QCOMPARE(text->font(), defaults.toQFont());
    QCOMPARE(text->font().families(), QFont().families());
    QCOMPARE(text->font().pointSize(), 24);
    QVERIFY(text->font().bold());
    QVERIFY(text->font().italic());
    layer.undo();
    QCOMPARE(text->font(), font);
    layer.redo();
    QCOMPARE(text->font().families(), QFont().families());
}
