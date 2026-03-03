#include "qml/ThemeManager.h"
#include "settings/Settings.h"
#include "ToolbarStyle.h"

#include <QGuiApplication>
#include <QJSEngine>
#include <QStyleHints>

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
    loadColorScheme();

    // Track real-time system theme changes
    if (auto *hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this] {
            if (m_colorScheme == ColorScheme::System)
                updateEffectiveTheme();
        });
    }
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

ThemeManager::ColorScheme ThemeManager::colorScheme() const
{
    return m_colorScheme;
}

void ThemeManager::setColorScheme(ColorScheme scheme)
{
    if (m_colorScheme == scheme)
        return;

    m_colorScheme = scheme;
    saveColorScheme();
    emit colorSchemeChanged();
    updateEffectiveTheme();
}

ThemeManager::Theme ThemeManager::effectiveTheme() const
{
    return m_effectiveTheme;
}

bool ThemeManager::isDarkMode() const
{
    // Always read from the authoritative source (the app's toolbar style setting)
    // to avoid stale cache when the setting changes outside ThemeManager.
    return ToolbarStyleConfig::loadStyle() == ToolbarStyleType::Dark;
}

QColor ThemeManager::primarySurface() const
{
    return isDarkMode() ? QColor(0x1A, 0x1A, 0x2E) : QColor(0xFF, 0xFF, 0xFF);
}

QColor ThemeManager::elevatedSurface() const
{
    return isDarkMode() ? QColor(0x2E, 0x2E, 0x50) : QColor(0xF5, 0xF5, 0xFA);
}

void ThemeManager::loadColorScheme()
{
    auto settings = SnapTray::getSettings();
    int stored = settings.value(kSettingsKeyColorScheme,
                                static_cast<int>(ColorScheme::System)).toInt();
    if (stored >= static_cast<int>(ColorScheme::System)
        && stored <= static_cast<int>(ColorScheme::Dark)) {
        m_colorScheme = static_cast<ColorScheme>(stored);
    } else {
        m_colorScheme = ColorScheme::System;
    }
    updateEffectiveTheme();
}

// ---- private ----

ThemeManager::Theme ThemeManager::resolveSystemTheme() const
{
    // Sync with the existing app-level toolbar style setting (appearance/toolbarStyle)
    // so QML components match the rest of the UI.
    // ToolbarStyleType::Dark = 0, Light = 1
    return (ToolbarStyleConfig::loadStyle() == ToolbarStyleType::Dark)
        ? Theme::Dark : Theme::Light;
}

void ThemeManager::updateEffectiveTheme()
{
    Theme resolved = Theme::Light;
    switch (m_colorScheme) {
    case ColorScheme::System: resolved = resolveSystemTheme(); break;
    case ColorScheme::Light:  resolved = Theme::Light;         break;
    case ColorScheme::Dark:   resolved = Theme::Dark;          break;
    }

    if (m_effectiveTheme != resolved) {
        m_effectiveTheme = resolved;
        emit themeChanged();
    }
}

void ThemeManager::saveColorScheme() const
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kSettingsKeyColorScheme, static_cast<int>(m_colorScheme));
}
