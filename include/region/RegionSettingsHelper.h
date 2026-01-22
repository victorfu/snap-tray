#ifndef REGIONSETTINGSHELPER_H
#define REGIONSETTINGSHELPER_H

#include <QObject>
#include <QPoint>
#include <QColor>
#include <QString>

class QWidget;
class QMenu;

enum class LineEndStyle;
enum class LineStyle;
enum class ShapeType;
enum class ShapeFillMode;
struct TextFormattingState;

/**
 * @brief Handles settings persistence and dropdown menus for RegionSelector.
 *
 * Provides static methods for loading/saving annotation settings,
 * and instance methods for showing dropdown menus.
 */
class RegionSettingsHelper : public QObject
{
    Q_OBJECT

public:
    explicit RegionSettingsHelper(QObject* parent = nullptr);

    void setParentWidget(QWidget* widget);

    // ========== Static Settings Methods ==========

    // Arrow style
    static LineEndStyle loadArrowStyle();
    static void saveArrowStyle(LineEndStyle style);

    // Line style
    static LineStyle loadLineStyle();
    static void saveLineStyle(LineStyle style);

    // Text formatting
    static TextFormattingState loadTextFormatting();

    // Shape settings
    static ShapeType loadShapeType();
    static ShapeFillMode loadShapeFillMode();

    // ========== Dropdown Menus ==========

    // Show font size dropdown
    void showFontSizeDropdown(const QPoint& pos, int currentSize);

    // Show font family dropdown
    void showFontFamilyDropdown(const QPoint& pos, const QString& currentFamily);

signals:
    void fontSizeSelected(int size);
    void fontFamilySelected(const QString& family);

private:
    // Menu styling
    static QString dropdownMenuStyle();

    // Helper to create styled menu
    QMenu* createStyledMenu();

    QWidget* m_parentWidget = nullptr;
};

#endif // REGIONSETTINGSHELPER_H
