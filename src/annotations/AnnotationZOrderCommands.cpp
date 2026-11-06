#include "annotations/AnnotationZOrderCommands.h"

#include <QCoreApplication>
#include <QKeyEvent>

namespace {

constexpr const char* kContext = "AnnotationZOrderCommands";

enum class Bracket { None, Left, Right };

// Native codes of the bracket keys, for presses where Shift or Option
// changed the reported key: macOS virtual keys, Windows VK_OEM_4/VK_OEM_6,
// X11 keysyms (where Shift reports the brace keysyms).
#if defined(Q_OS_MACOS)
constexpr quint32 kNativeLeftBracket[] = {0x21};          // kVK_ANSI_LeftBracket
constexpr quint32 kNativeRightBracket[] = {0x1E};         // kVK_ANSI_RightBracket
// Option is the macOS modifier for front/back, as in Keynote.
constexpr Qt::KeyboardModifiers kToEndModifier = Qt::AltModifier;
#elif defined(Q_OS_WIN)
constexpr quint32 kNativeLeftBracket[] = {0xDB};          // VK_OEM_4
constexpr quint32 kNativeRightBracket[] = {0xDD};         // VK_OEM_6
constexpr Qt::KeyboardModifiers kToEndModifier = Qt::ShiftModifier;
#else
constexpr quint32 kNativeLeftBracket[] = {0x5B, 0x7B};    // XK_bracketleft, XK_braceleft
constexpr quint32 kNativeRightBracket[] = {0x5D, 0x7D};   // XK_bracketright, XK_braceright
constexpr Qt::KeyboardModifiers kToEndModifier = Qt::ShiftModifier;
#endif

struct ZOrderCommand {
    AnnotationZOrderCommands::Move move;
    Bracket bracket;
    bool toEnd;
    Qt::Key key;
    const char* actionText;
    const char* announcement;
};

constexpr ZOrderCommand kCommands[] = {
    {AnnotationZOrderCommands::Move::Forward, Bracket::Right, false, Qt::Key_BracketRight,
     QT_TRANSLATE_NOOP("AnnotationZOrderCommands", "Bring Forward"),
     QT_TRANSLATE_NOOP("AnnotationZOrderCommands", "Moved forward")},
    {AnnotationZOrderCommands::Move::Backward, Bracket::Left, false, Qt::Key_BracketLeft,
     QT_TRANSLATE_NOOP("AnnotationZOrderCommands", "Send Backward"),
     QT_TRANSLATE_NOOP("AnnotationZOrderCommands", "Moved backward")},
    {AnnotationZOrderCommands::Move::ToFront, Bracket::Right, true, Qt::Key_BracketRight,
     QT_TRANSLATE_NOOP("AnnotationZOrderCommands", "Bring to Front"),
     QT_TRANSLATE_NOOP("AnnotationZOrderCommands", "Moved to front")},
    {AnnotationZOrderCommands::Move::ToBack, Bracket::Left, true, Qt::Key_BracketLeft,
     QT_TRANSLATE_NOOP("AnnotationZOrderCommands", "Send to Back"),
     QT_TRANSLATE_NOOP("AnnotationZOrderCommands", "Moved to back")},
};

const ZOrderCommand* commandFor(AnnotationZOrderCommands::Move move)
{
    for (const auto& command : kCommands) {
        if (command.move == move) return &command;
    }
    return nullptr;
}

Bracket bracketOf(const QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_BracketLeft:
    case Qt::Key_BraceLeft:
        return Bracket::Left;
    case Qt::Key_BracketRight:
    case Qt::Key_BraceRight:
        return Bracket::Right;
    default:
        break;
    }
    const quint32 native = event->nativeVirtualKey();
    if (native == 0) return Bracket::None;   // synthesized events carry no native key
    for (quint32 code : kNativeLeftBracket) {
        if (native == code) return Bracket::Left;
    }
    for (quint32 code : kNativeRightBracket) {
        if (native == code) return Bracket::Right;
    }
    return Bracket::None;
}

} // namespace

namespace AnnotationZOrderCommands {

std::optional<Move> moveForKeyEvent(const QKeyEvent* event)
{
    if (!event) return std::nullopt;
    const Qt::KeyboardModifiers modifiers = event->modifiers()
        & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);
    const bool toEnd = modifiers == (Qt::ControlModifier | kToEndModifier);
    if (modifiers != Qt::ControlModifier && !toEnd) return std::nullopt;
    const Bracket bracket = bracketOf(event);
    for (const auto& command : kCommands) {
        if (command.bracket == bracket && command.toEnd == toEnd) return command.move;
    }
    return std::nullopt;
}

QKeySequence shortcut(Move move)
{
    const ZOrderCommand* command = commandFor(move);
    if (!command) return {};
    const Qt::KeyboardModifiers modifiers = command->toEnd
        ? (Qt::ControlModifier | kToEndModifier)
        : Qt::KeyboardModifiers(Qt::ControlModifier);
    return QKeySequence(QKeyCombination(modifiers, command->key));
}

QString actionText(Move move)
{
    const ZOrderCommand* command = commandFor(move);
    return command ? QCoreApplication::translate(kContext, command->actionText) : QString();
}

QString announcement(Move move)
{
    const ZOrderCommand* command = commandFor(move);
    return command ? QCoreApplication::translate(kContext, command->announcement) : QString();
}

} // namespace AnnotationZOrderCommands
