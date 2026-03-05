#include "qml/ThemeManager.h"
#include "ToolbarStyle.h"

#include <QJSEngine>

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
    updateEffectiveTheme();
}

ThemeManager& ThemeManager::instance()
{
    static ThemeManager inst;
    return inst;
}

ThemeManager* ThemeManager::create(QQmlEngine *, QJSEngine *jsEngine)
{
    // Return the C++ singleton; prevent the QML engine from taking ownership
    // so the static instance is not double-deleted.
    auto *inst = &instance();
    QJSEngine::setObjectOwnership(inst, QJSEngine::CppOwnership);
    return inst;
}

ThemeManager::Theme ThemeManager::effectiveTheme() const
{
    return m_effectiveTheme;
}

bool ThemeManager::isDarkMode() const
{
    return m_effectiveTheme == Theme::Dark;
}

QColor ThemeManager::primarySurface() const
{
    return isDarkMode() ? QColor(0x1A, 0x1A, 0x2E) : QColor(0xFF, 0xFF, 0xFF);
}

QColor ThemeManager::elevatedSurface() const
{
    return isDarkMode() ? QColor(0x2E, 0x2E, 0x50) : QColor(0xF5, 0xF5, 0xFA);
}

void ThemeManager::refreshTheme()
{
    updateEffectiveTheme();
}

// ---- private ----

void ThemeManager::updateEffectiveTheme()
{
    Theme resolved = (ToolbarStyleConfig::loadStyle() == ToolbarStyleType::Dark)
        ? Theme::Dark : Theme::Light;

    if (m_effectiveTheme != resolved) {
        m_effectiveTheme = resolved;
        emit themeChanged();
    }
}
