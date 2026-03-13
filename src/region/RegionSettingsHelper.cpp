#include "region/RegionSettingsHelper.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/LineStyle.h"
#include "annotations/ShapeAnnotation.h"
#include "platform/WindowLevel.h"
#include "settings/AnnotationSettingsManager.h"
#include "settings/Settings.h"
#include "TextFormattingState.h"

#include <QSettings>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QIcon>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QWidget>
#include <QFontDatabase>
#include <QTimer>

// Settings keys
static const char* SETTINGS_KEY_SHAPE_TYPE = "annotation/shapeType";
static const char* SETTINGS_KEY_SHAPE_FILL_MODE = "annotation/shapeFillMode";
static const char* SETTINGS_KEY_TEXT_BOLD = "annotation/text_bold";
static const char* SETTINGS_KEY_TEXT_ITALIC = "annotation/text_italic";
static const char* SETTINGS_KEY_TEXT_UNDERLINE = "annotation/text_underline";
static const char* SETTINGS_KEY_TEXT_SIZE = "annotation/text_size";
static const char* SETTINGS_KEY_TEXT_FAMILY = "annotation/text_family";

// Helper to get QSettings instance
static QSettings getSettings()
{
    return SnapTray::getSettings();
}

struct ArrowStyleMenuEntry {
    LineEndStyle value;
    const char* label;
};

constexpr ArrowStyleMenuEntry kArrowStyleEntries[] = {
    {LineEndStyle::None, QT_TR_NOOP("No Arrow")},
    {LineEndStyle::EndArrow, QT_TR_NOOP("End Arrow")},
    {LineEndStyle::EndArrowOutline, QT_TR_NOOP("End Arrow Outline")},
    {LineEndStyle::EndArrowLine, QT_TR_NOOP("End Arrow Line")},
    {LineEndStyle::BothArrow, QT_TR_NOOP("Both Ends")},
    {LineEndStyle::BothArrowOutline, QT_TR_NOOP("Both Ends Outline")},
};

struct LineStyleMenuEntry {
    LineStyle value;
    const char* label;
};

constexpr LineStyleMenuEntry kLineStyleEntries[] = {
    {LineStyle::Solid, QT_TR_NOOP("Solid")},
    {LineStyle::Dashed, QT_TR_NOOP("Dashed")},
    {LineStyle::Dotted, QT_TR_NOOP("Dotted")},
};

static Qt::PenStyle qtPenStyleForLineStyle(LineStyle style)
{
    switch (style) {
    case LineStyle::Solid:
        return Qt::SolidLine;
    case LineStyle::Dashed:
        return Qt::DashLine;
    case LineStyle::Dotted:
        return Qt::DotLine;
    }
    return Qt::SolidLine;
}

static QPixmap createMenuIconCanvas()
{
    constexpr QSize kLogicalSize(22, 14);
    QPixmap pixmap(kLogicalSize);
    pixmap.fill(Qt::transparent);
    return pixmap;
}

static void drawArrowTriangle(QPainter& painter, const QPointF& tip, bool pointRight, bool filled,
                              const QColor& fillColor = QColor())
{
    constexpr qreal kArrowLength = 6.0;
    constexpr qreal kArrowHalfHeight = 3.5;

    QPolygonF polygon;
    if (pointRight) {
        polygon << tip
                << QPointF(tip.x() - kArrowLength, tip.y() - kArrowHalfHeight)
                << QPointF(tip.x() - kArrowLength, tip.y() + kArrowHalfHeight);
    } else {
        polygon << tip
                << QPointF(tip.x() + kArrowLength, tip.y() - kArrowHalfHeight)
                << QPointF(tip.x() + kArrowLength, tip.y() + kArrowHalfHeight);
    }

    painter.save();
    if (filled) {
        painter.setBrush(painter.pen().color());
    } else {
        QPen outlinePen = painter.pen();
        outlinePen.setWidthF(qMax(1.15, outlinePen.widthF() - 0.45));
        painter.setPen(outlinePen);
        painter.setBrush(fillColor.isValid() ? QBrush(fillColor) : Qt::NoBrush);
    }
    painter.drawPolygon(polygon);
    painter.restore();
}

static void drawArrowLine(QPainter& painter, const QPointF& tip, bool pointRight)
{
    constexpr qreal kArrowLength = 6.0;
    constexpr qreal kArrowHalfHeight = 3.5;
    const qreal backX = pointRight ? tip.x() - kArrowLength : tip.x() + kArrowLength;
    painter.drawLine(QPointF(backX, tip.y() - kArrowHalfHeight), tip);
    painter.drawLine(QPointF(backX, tip.y() + kArrowHalfHeight), tip);
}

static QIcon createLineStyleMenuIcon(LineStyle style, const QColor& color)
{
    QPixmap pixmap = createMenuIconCanvas();
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QPen pen(color, 2.0, qtPenStyleForLineStyle(style), Qt::RoundCap, Qt::RoundJoin);
    if (style == LineStyle::Dashed) {
        pen.setDashPattern({4.0, 3.0});
    } else if (style == LineStyle::Dotted) {
        pen.setDashPattern({1.0, 3.0});
    }

    painter.setPen(pen);
    painter.drawLine(QPointF(2.0, 7.0), QPointF(20.0, 7.0));
    painter.end();
    return QIcon(pixmap);
}

static QIcon createArrowStyleMenuIcon(LineEndStyle style, const QColor& color,
                                      const QColor& backgroundColor)
{
    QPixmap pixmap = createMenuIconCanvas();
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

    const bool hasEndArrow = style != LineEndStyle::None;
    const bool hasStartArrow = style == LineEndStyle::BothArrow
                            || style == LineEndStyle::BothArrowOutline;
    const qreal lineStartX = hasStartArrow ? 8.0 : 2.0;
    const qreal lineEndX = hasEndArrow ? 14.0 : 20.0;
    constexpr qreal kCenterY = 7.0;

    painter.drawLine(QPointF(lineStartX, kCenterY), QPointF(lineEndX, kCenterY));

    switch (style) {
    case LineEndStyle::None:
        break;
    case LineEndStyle::EndArrow:
        drawArrowTriangle(painter, QPointF(20.0, kCenterY), true, true);
        break;
    case LineEndStyle::EndArrowOutline:
        drawArrowTriangle(painter, QPointF(20.0, kCenterY), true, false, backgroundColor);
        break;
    case LineEndStyle::EndArrowLine:
        drawArrowLine(painter, QPointF(20.0, kCenterY), true);
        break;
    case LineEndStyle::BothArrow:
        drawArrowTriangle(painter, QPointF(2.0, kCenterY), false, true);
        drawArrowTriangle(painter, QPointF(20.0, kCenterY), true, true);
        break;
    case LineEndStyle::BothArrowOutline:
        drawArrowTriangle(painter, QPointF(2.0, kCenterY), false, false, backgroundColor);
        drawArrowTriangle(painter, QPointF(20.0, kCenterY), true, false, backgroundColor);
        break;
    }

    painter.end();
    return QIcon(pixmap);
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
    return AnnotationSettingsManager::instance().loadArrowStyle();
}

void RegionSettingsHelper::saveArrowStyle(LineEndStyle style)
{
    AnnotationSettingsManager::instance().saveArrowStyle(style);
}

// ========== Line Style ==========

LineStyle RegionSettingsHelper::loadLineStyle()
{
    return AnnotationSettingsManager::instance().loadLineStyle();
}

void RegionSettingsHelper::saveLineStyle(LineStyle style)
{
    AnnotationSettingsManager::instance().saveLineStyle(style);
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

QMenu* RegionSettingsHelper::createMenu()
{
    QMenu* menu = m_parentWidget ? new QMenu(m_parentWidget) : new QMenu();
    return menu;
}

void RegionSettingsHelper::showFontSizeDropdown(const QPoint& pos, int currentSize)
{
    QMenu* menu = createMenu();
    populateFontSizeMenu(menu, currentSize);
    showMenu(menu, pos);
}

void RegionSettingsHelper::showFontFamilyDropdown(const QPoint& pos, const QString& currentFamily)
{
    QMenu* menu = createMenu();
    populateFontFamilyMenu(menu, currentFamily);
    showMenu(menu, pos);
}

void RegionSettingsHelper::showArrowStyleDropdown(const QPoint& pos, LineEndStyle currentStyle)
{
    QMenu* menu = createMenu();
    populateArrowStyleMenu(menu, currentStyle);
    showMenu(menu, pos);
}

void RegionSettingsHelper::showLineStyleDropdown(const QPoint& pos, LineStyle currentStyle)
{
    QMenu* menu = createMenu();
    populateLineStyleMenu(menu, currentStyle);
    showMenu(menu, pos);
}

void RegionSettingsHelper::showMenu(QMenu* menu, const QPoint& globalPos)
{
    bool menuWasShown = false;

    if (m_menuPresenter) {
        menuWasShown = m_menuPresenter(menu, globalPos);
        if (menuWasShown) {
            emit dropdownShown();
            emit dropdownHidden();
        }
        if (menu) {
            menu->deleteLater();
        }
        return;
    }

    if (!menu) {
        return;
    }

    auto bringToFront = [menu]() {
        if (!menu || !menu->isVisible()) {
            return;
        }
        raiseWindowAboveOverlays(menu);
        menu->raise();
        menu->activateWindow();
    };

    QObject::connect(menu, &QMenu::aboutToShow, menu, [menu, bringToFront]() {
        QTimer::singleShot(0, menu, bringToFront);
        QTimer::singleShot(80, menu, bringToFront);
    });
    QObject::connect(menu, &QMenu::aboutToShow, this, [this, &menuWasShown]() {
        menuWasShown = true;
        emit dropdownShown();
    });

    bringToFront();
    menu->exec(globalPos);
    if (menuWasShown) {
        emit dropdownHidden();
    }
    menu->deleteLater();
}

void RegionSettingsHelper::populateFontSizeMenu(QMenu* menu, int currentSize)
{
    static const int sizes[] = { 8, 10, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48, 72 };
    for (int size : sizes) {
        QAction* action = menu->addAction(QString::number(size));
        action->setCheckable(true);
        action->setChecked(size == currentSize);
        connect(action, &QAction::triggered, this, [this, size]() {
            emit fontSizeSelected(size);
        });
    }
}

void RegionSettingsHelper::populateFontFamilyMenu(QMenu* menu, const QString& currentFamily)
{
    QAction* defaultAction = menu->addAction(tr("Default"));
    defaultAction->setCheckable(true);
    defaultAction->setChecked(currentFamily.isEmpty());
    connect(defaultAction, &QAction::triggered, this, [this]() {
        emit fontFamilySelected(QString());
    });

    menu->addSeparator();

    const QStringList families = QFontDatabase::families();
    const QStringList commonFonts = { "Arial", "Helvetica", "Times New Roman", "Courier New",
                                      "Verdana", "Georgia", "Trebuchet MS", "Impact" };
    for (const QString& family : commonFonts) {
        if (!families.contains(family)) {
            continue;
        }

        QAction* action = menu->addAction(family);
        action->setCheckable(true);
        action->setChecked(family == currentFamily);
        connect(action, &QAction::triggered, this, [this, family]() {
            emit fontFamilySelected(family);
        });
    }
}

void RegionSettingsHelper::populateArrowStyleMenu(QMenu* menu, LineEndStyle currentStyle)
{
    auto* group = new QActionGroup(menu);
    group->setExclusive(true);
    const QColor iconColor = menu->palette().color(QPalette::WindowText);
    const QColor iconBackground = menu->palette().color(QPalette::Window);

    for (const auto& entry : kArrowStyleEntries) {
        QAction* action = menu->addAction(
            createArrowStyleMenuIcon(entry.value, iconColor, iconBackground),
            tr(entry.label));
        action->setCheckable(true);
        action->setChecked(entry.value == currentStyle);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, value = entry.value]() {
            emit arrowStyleSelected(value);
        });
    }
}

void RegionSettingsHelper::populateLineStyleMenu(QMenu* menu, LineStyle currentStyle)
{
    auto* group = new QActionGroup(menu);
    group->setExclusive(true);
    const QColor iconColor = menu->palette().color(QPalette::WindowText);

    for (const auto& entry : kLineStyleEntries) {
        QAction* action = menu->addAction(
            createLineStyleMenuIcon(entry.value, iconColor),
            tr(entry.label));
        action->setCheckable(true);
        action->setChecked(entry.value == currentStyle);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, value = entry.value]() {
            emit lineStyleSelected(value);
        });
    }
}
