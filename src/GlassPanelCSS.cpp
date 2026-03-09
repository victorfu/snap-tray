#include "GlassPanelCSS.h"
#include "ToolbarStyle.h"
#include "ui/DesignSystem.h"
#include "utils/DialogThemeUtils.h"

namespace SnapTray {
namespace GlassPanelCSS {

QString comboBoxStylesheet(const ToolbarStyleConfig& config)
{
    const QString comboBg = config.buttonInactiveColor.name();
    const QString textColor = config.textColor.name();
    const QString comboBorder = config.dropdownBorder.name();
    const QString comboHover = config.buttonHoverColor.name();
    const QString dropdownBg = config.dropdownBackground.name(QColor::HexArgb);
    const QString selectionBg = config.buttonActiveColor.name();

    return QStringLiteral(
        "QComboBox {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  border-radius: 4px;"
        "  padding: 4px 8px;"
        "  font-size: 13px;"
        "}"
        "QComboBox:hover {"
        "  background-color: %4;"
        "}"
        "QComboBox::drop-down {"
        "  border: none;"
        "}"
        "QComboBox QAbstractItemView {"
        "  background-color: %5;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  selection-background-color: %6;"
        "  selection-color: white;"
        "}")
        .arg(comboBg, textColor, comboBorder, comboHover, dropdownBg, selectionBg);
}

QString sliderStylesheet(const ToolbarStyleConfig& config, ToolbarStyleType type)
{
    const QString grooveColor = config.separatorColor.name();
    const bool isDarkStyle = (type == ToolbarStyleType::Dark);
    const QString handleColor = isDarkStyle ? QStringLiteral("#FFFFFF") : QStringLiteral("#333333");

    return QStringLiteral(
        "QSlider::groove:horizontal {"
        "  background: %1;"
        "  height: 4px;"
        "  border-radius: 2px;"
        "}"
        "QSlider::sub-page:horizontal {"
        "  background: %3;"
        "  height: 4px;"
        "  border-radius: 2px;"
        "}"
        "QSlider::handle:horizontal {"
        "  background: %2;"
        "  width: 14px;"
        "  height: 14px;"
        "  margin: -5px 0;"
        "  border-radius: 7px;"
        "}")
        .arg(grooveColor, handleColor, DesignSystem::instance().accentDefault().name());
}

QString actionButtonStylesheet(const ToolbarStyleConfig& config, ToolbarStyleType type)
{
    const bool isDarkStyle = (type == ToolbarStyleType::Dark);
    const QString btnBg = config.buttonInactiveColor.name();
    const QString textColor = config.textColor.name();
    const QString btnHover = config.buttonHoverColor.name();
    const QString btnPressed = isDarkStyle ? QStringLiteral("#383838") : QStringLiteral("#C0C0C0");

    return QStringLiteral(
        "QPushButton {"
        "  background-color: %1;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 6px 16px;"
        "  font-size: 13px;"
        "  color: %2;"
        "}"
        "QPushButton:hover {"
        "  background-color: %3;"
        "}"
        "QPushButton:pressed {"
        "  background-color: %4;"
        "}")
        .arg(btnBg, textColor, btnHover, btnPressed);
}

QString checkboxStylesheet(const ToolbarStyleConfig& config)
{
    const QString textColor = config.textColor.name();
    const QString indicatorBorder = config.separatorColor.name();
    const QString indicatorBg = config.buttonInactiveColor.name();

    return QStringLiteral(
        "QCheckBox { font-size: 13px; color: %1; }"
        "QCheckBox::indicator {"
        "  width: 14px; height: 14px;"
        "  border: 1px solid %2;"
        "  border-radius: 3px;"
        "  background: %3;"
        "}"
        "QCheckBox::indicator:checked {"
        "  background: %4;"
        "  border: 1px solid %4;"
        "}")
        .arg(textColor, indicatorBorder, indicatorBg, DesignSystem::instance().accentDefault().name());
}

QString labelStylesheet(const ToolbarStyleConfig& config, int fontSize)
{
    return QStringLiteral("QLabel { font-size: %1px; color: %2; }")
        .arg(fontSize)
        .arg(config.textColor.name());
}

QString monoLabelStylesheet(const QColor& textColor, int fontSize, bool bold)
{
    const QString color = DialogTheme::toCssColor(textColor);
    const QString weight = bold ? QStringLiteral(" font-weight: 600;") : QString();
    return QStringLiteral("color: %1; font-size: %2px;%3"
                          " font-family: 'SF Mono', Menlo, Monaco, monospace;")
        .arg(color)
        .arg(fontSize)
        .arg(weight);
}

QString separatorStylesheet(const QColor& textColor)
{
    return QStringLiteral("color: %1; font-size: 11px;")
        .arg(DialogTheme::toCssColor(textColor));
}

} // namespace GlassPanelCSS
} // namespace SnapTray
