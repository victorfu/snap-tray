#pragma once

#include <QColor>
#include <QString>

struct ToolbarStyleConfig;
enum class ToolbarStyleType;

namespace SnapTray {
namespace GlassPanelCSS {

/**
 * @brief QComboBox stylesheet matching glass panel style.
 *
 * Includes dropdown, hover, and item list styling.
 */
QString comboBoxStylesheet(const ToolbarStyleConfig& config);

/**
 * @brief QSlider stylesheet matching glass panel style.
 *
 * Includes groove, sub-page, and handle styling.
 * @param type Used to determine handle color (dark vs light).
 */
QString sliderStylesheet(const ToolbarStyleConfig& config, ToolbarStyleType type);

/**
 * @brief QPushButton stylesheet for action buttons on glass panels.
 *
 * Includes hover and pressed states.
 * @param type Used to determine pressed background color.
 */
QString actionButtonStylesheet(const ToolbarStyleConfig& config, ToolbarStyleType type);

/**
 * @brief QCheckBox stylesheet matching glass panel style.
 *
 * Includes indicator styling with checked state.
 */
QString checkboxStylesheet(const ToolbarStyleConfig& config);

/**
 * @brief QLabel stylesheet with glass-panel-aware text color.
 */
QString labelStylesheet(const ToolbarStyleConfig& config, int fontSize = 13);

/**
 * @brief Monospace QLabel stylesheet for data display (e.g. recording control bar).
 */
QString monoLabelStylesheet(const QColor& textColor, int fontSize = 11, bool bold = false);

/**
 * @brief Separator/divider label stylesheet.
 */
QString separatorStylesheet(const QColor& textColor);

} // namespace GlassPanelCSS
} // namespace SnapTray
