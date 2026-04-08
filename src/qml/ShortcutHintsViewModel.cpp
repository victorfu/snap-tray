#include "qml/ShortcutHintsViewModel.h"

#include <QCoreApplication>
#include <QStringList>
#include <QVariantMap>

namespace {
constexpr char kCancelCapture[] =
    QT_TRANSLATE_NOOP("CaptureShortcutHintsOverlay", "Cancel capture");
constexpr char kConfirmSelection[] =
    QT_TRANSLATE_NOOP("CaptureShortcutHintsOverlay", "Confirm selection (after selection)");
constexpr char kReplayCaptureHistory[] =
    QT_TRANSLATE_NOOP("CaptureShortcutHintsOverlay", "Replay capture history");
constexpr char kToggleMultiRegionMode[] =
    QT_TRANSLATE_NOOP("CaptureShortcutHintsOverlay", "Toggle multi-region mode");
constexpr char kSwitchRgbHex[] =
    QT_TRANSLATE_NOOP("CaptureShortcutHintsOverlay", "Switch RGB/HEX (when magnifier visible)");
constexpr char kCopyColorValue[] =
    QT_TRANSLATE_NOOP("CaptureShortcutHintsOverlay", "Copy color value (before selection)");
constexpr char kMoveSelection[] =
    QT_TRANSLATE_NOOP("CaptureShortcutHintsOverlay", "Move selection by 1 pixel (after selection)");
constexpr char kResizeSelection[] =
    QT_TRANSLATE_NOOP("CaptureShortcutHintsOverlay", "Resize selection by 1 pixel (after selection)");

QString trOverlay(const char* text)
{
    return QCoreApplication::translate("CaptureShortcutHintsOverlay", text);
}
} // namespace

ShortcutHintsViewModel::ShortcutHintsViewModel(QObject* parent)
    : QObject(parent)
{
    struct HintRow {
        QStringList keys;
        const char* description;
    };

    const HintRow rows[] = {
        {{QStringLiteral("Esc")}, kCancelCapture},
        {{QStringLiteral("Enter")}, kConfirmSelection},
        {{QStringLiteral(", / .")}, kReplayCaptureHistory},
        {{QStringLiteral("M")}, kToggleMultiRegionMode},
        {{QStringLiteral("Shift")}, kSwitchRgbHex},
        {{QStringLiteral("C")}, kCopyColorValue},
        {{QStringLiteral("Arrow")}, kMoveSelection},
        {{QStringLiteral("Shift"), QStringLiteral("Arrow")}, kResizeSelection},
    };

    for (const auto& row : rows) {
        QVariantMap hint;
        hint[QStringLiteral("keys")] = QVariant::fromValue(row.keys);
        hint[QStringLiteral("description")] = trOverlay(row.description);
        m_hints.append(hint);
    }
}
