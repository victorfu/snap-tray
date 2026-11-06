#pragma once

#include <QKeySequence>
#include <QString>

#include <optional>

#include "annotations/AnnotationZOrder.h"

class QKeyEvent;

/**
 * Bring Forward, Send Backward, Bring to Front and Send to Back for the
 * selected annotation (#268), shared by Region Selector, pin windows and
 * Screen Canvas: shortcuts, menu and button names, and the screen reader
 * announcement after a move. AnnotationStyleEditor applies the moves.
 *
 * Shortcuts: Cmd/Ctrl+] and Cmd/Ctrl+[ step one layer; Option+Cmd+] /
 * Option+Cmd+[ on macOS, Ctrl+Shift+] / Ctrl+Shift+[ elsewhere, go to the
 * front or back.
 */
namespace AnnotationZOrderCommands {

using Move = AnnotationZOrderMove;

// The move a key press asks for, if any. Matches the bracket keys by their
// physical position too, as Shift or Option can change the typed character.
std::optional<Move> moveForKeyEvent(const QKeyEvent* event);
QKeySequence shortcut(Move move);
QString actionText(Move move);     // "Bring Forward"
QString announcement(Move move);   // "Moved forward"

} // namespace AnnotationZOrderCommands
