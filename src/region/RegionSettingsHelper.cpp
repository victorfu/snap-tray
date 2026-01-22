#include "region/RegionSettingsHelper.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/LineStyle.h"
#include "annotations/ShapeAnnotation.h"
#include "settings/Settings.h"
#include "TextFormattingState.h"

#include <QSettings>
#include <QMenu>
#include <QAction>
#include <QWidget>
#include <QFontDatabase>

// Settings keys
static const char* SETTINGS_KEY_ARROW_STYLE = "annotation/arrowStyle";
static const char* SETTINGS_KEY_LINE_STYLE = "annotation/lineStyle";
static const char* SETTINGS_KEY_SHAPE_TYPE = "annotation/shapeType";
static const char* SETTINGS_KEY_SHAPE_FILL_MODE = "annotation/shapeFillMode";
static const char* SETTINGS_KEY_TEXT_BOLD = "textBold";
static const char* SETTINGS_KEY_TEXT_ITALIC = "textItalic";
static const char* SETTINGS_KEY_TEXT_UNDERLINE = "textUnderline";
static const char* SETTINGS_KEY_TEXT_SIZE = "textFontSize";
static const char* SETTINGS_KEY_TEXT_FAMILY = "textFontFamily";

// Helper to get QSettings instance
static QSettings getSettings()
{
    return SnapTray::getSettings();
}

RegionSettingsHelper::RegionSettingsHelper(QObject* parent)
    : QObject(parent)
{
}

void RegionSettingsHelper::setParentWidget(QWidget* widget)
{
    m_parentWidget = widget;
}

// ========== Arrow Style ==========

LineEndStyle RegionSettingsHelper::loadArrowStyle()
{
    int style = getSettings().value(SETTINGS_KEY_ARROW_STYLE,
                                    static_cast<int>(LineEndStyle::EndArrow)).toInt();
    return static_cast<LineEndStyle>(style);
}

void RegionSettingsHelper::saveArrowStyle(LineEndStyle style)
{
    getSettings().setValue(SETTINGS_KEY_ARROW_STYLE, static_cast<int>(style));
}

// ========== Line Style ==========

LineStyle RegionSettingsHelper::loadLineStyle()
{
    int style = getSettings().value(SETTINGS_KEY_LINE_STYLE,
                                    static_cast<int>(LineStyle::Solid)).toInt();
    return static_cast<LineStyle>(style);
}

void RegionSettingsHelper::saveLineStyle(LineStyle style)
{
    getSettings().setValue(SETTINGS_KEY_LINE_STYLE, static_cast<int>(style));
}

// ========== Text Formatting ==========

TextFormattingState RegionSettingsHelper::loadTextFormatting()
{
    QSettings settings = getSettings();
    TextFormattingState state;
    state.bold = settings.value(SETTINGS_KEY_TEXT_BOLD, true).toBool();
    state.italic = settings.value(SETTINGS_KEY_TEXT_ITALIC, false).toBool();
    state.underline = settings.value(SETTINGS_KEY_TEXT_UNDERLINE, false).toBool();
    state.fontSize = settings.value(SETTINGS_KEY_TEXT_SIZE, 16).toInt();
    state.fontFamily = settings.value(SETTINGS_KEY_TEXT_FAMILY, QString()).toString();
    return state;
}

// ========== Shape Settings ==========

ShapeType RegionSettingsHelper::loadShapeType()
{
    int type = getSettings().value(SETTINGS_KEY_SHAPE_TYPE,
                                   static_cast<int>(ShapeType::Rectangle)).toInt();
    return static_cast<ShapeType>(type);
}

ShapeFillMode RegionSettingsHelper::loadShapeFillMode()
{
    int mode = getSettings().value(SETTINGS_KEY_SHAPE_FILL_MODE,
                                   static_cast<int>(ShapeFillMode::Outline)).toInt();
    return static_cast<ShapeFillMode>(mode);
}

// ========== Dropdown Menus ==========

QString RegionSettingsHelper::dropdownMenuStyle()
{
    return QStringLiteral(
        "QMenu { background: #2d2d2d; color: white; border: 1px solid #3d3d3d; } "
        "QMenu::item { padding: 4px 20px; } "
        "QMenu::item:selected { background: #0078d4; }");
}

QMenu* RegionSettingsHelper::createStyledMenu()
{
    QMenu* menu = new QMenu(m_parentWidget);
    menu->setStyleSheet(dropdownMenuStyle());
    return menu;
}

void RegionSettingsHelper::showFontSizeDropdown(const QPoint& pos, int currentSize)
{
    QMenu* menu = createStyledMenu();

    static const int sizes[] = { 8, 10, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48, 72 };
    for (int size : sizes) {
        QAction* action = menu->addAction(QString::number(size));
        action->setCheckable(true);
        action->setChecked(size == currentSize);
        connect(action, &QAction::triggered, this, [this, size]() {
            emit fontSizeSelected(size);
        });
    }

    if (m_parentWidget) {
        menu->exec(m_parentWidget->mapToGlobal(pos));
    } else {
        menu->exec(pos);
    }

    menu->deleteLater();
}

void RegionSettingsHelper::showFontFamilyDropdown(const QPoint& pos, const QString& currentFamily)
{
    QMenu* menu = createStyledMenu();

    // Add "Default" option
    QAction* defaultAction = menu->addAction(tr("Default"));
    defaultAction->setCheckable(true);
    defaultAction->setChecked(currentFamily.isEmpty());
    connect(defaultAction, &QAction::triggered, this, [this]() {
        emit fontFamilySelected(QString());
    });

    menu->addSeparator();

    // Add common font families
    QStringList families = QFontDatabase::families();
    QStringList commonFonts = { "Arial", "Helvetica", "Times New Roman", "Courier New",
                               "Verdana", "Georgia", "Trebuchet MS", "Impact" };
    for (const QString& family : commonFonts) {
        if (families.contains(family)) {
            QAction* action = menu->addAction(family);
            action->setCheckable(true);
            action->setChecked(family == currentFamily);
            connect(action, &QAction::triggered, this, [this, family]() {
                emit fontFamilySelected(family);
            });
        }
    }

    if (m_parentWidget) {
        menu->exec(m_parentWidget->mapToGlobal(pos));
    } else {
        menu->exec(pos);
    }

    menu->deleteLater();
}
