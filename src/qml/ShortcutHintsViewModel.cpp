#include "qml/ShortcutHintsViewModel.h"

#include <QCoreApplication>
#include <QStringList>
#include <QVariantMap>

namespace {
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
        {{QStringLiteral("Esc")}, "Cancel capture"},
        {{QStringLiteral("Enter")}, "Confirm selection (after selection)"},
        {{QStringLiteral("M")}, "Toggle multi-region mode"},
        {{QStringLiteral("Shift")}, "Switch RGB/HEX (when magnifier visible)"},
        {{QStringLiteral("C")}, "Copy color value (before selection)"},
        {{QStringLiteral("Arrow")}, "Move selection by 1 pixel (after selection)"},
        {{QStringLiteral("Shift"), QStringLiteral("Arrow")}, "Resize selection by 1 pixel (after selection)"},
    };

    for (const auto& row : rows) {
        QVariantMap hint;
        hint[QStringLiteral("keys")] = QVariant::fromValue(row.keys);
        hint[QStringLiteral("description")] = trOverlay(row.description);
        m_hints.append(hint);
    }
}
