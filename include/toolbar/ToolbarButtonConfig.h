#ifndef TOOLBARBUTTONCONFIG_H
#define TOOLBARBUTTONCONFIG_H

#include <QString>

namespace Toolbar {

/**
 * @brief Unified button configuration used across all toolbars.
 *
 * This struct provides a common configuration format for toolbar buttons,
 * supporting special button types (action, cancel, record) and separator
 * positioning.
 */
struct ButtonConfig {
    int id = 0;
    QString iconKey;
    QString tooltip;
    bool separatorBefore = false;

    // Extended properties for special button types
    bool isAction = false;      // Blue action color (Pin, Save, Copy)
    bool isCancel = false;      // Red cancel color
    bool isRecord = false;      // Red record color
    bool isToggle = false;      // Toggle button behavior

    ButtonConfig() = default;
    ButtonConfig(int id, const QString& iconKey, const QString& tooltip = QString(),
                 bool separatorBefore = false);

    // Builder pattern for fluent configuration
    ButtonConfig& action();
    ButtonConfig& cancel();
    ButtonConfig& record();
    ButtonConfig& toggle();
    ButtonConfig& separator();
};

} // namespace Toolbar

#endif // TOOLBARBUTTONCONFIG_H
