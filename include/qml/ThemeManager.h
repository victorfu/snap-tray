#pragma once

#include <QObject>
#include <QColor>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlEngine;

/**
 * @brief Singleton managing dark/light theme state for QML.
 *
 * Bridges system theme detection (via QStyleHints::colorScheme()) to QML.
 * The user can choose System, Light, or Dark; when System is selected the
 * effective theme tracks the OS in real time.
 *
 * Surfaces follow an Atlassian-style elevation hierarchy:
 *   Dark  — primary #1A1A2E, elevated #2E2E50
 *   Light — primary #FFFFFF, elevated #F5F5FA
 */
class ThemeManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    enum class ColorScheme { System = 0, Light, Dark };
    Q_ENUM(ColorScheme)

    enum class Theme { Light = 0, Dark };
    Q_ENUM(Theme)

    Q_PROPERTY(ColorScheme colorScheme READ colorScheme WRITE setColorScheme NOTIFY colorSchemeChanged)
    Q_PROPERTY(Theme effectiveTheme READ effectiveTheme NOTIFY themeChanged)
    Q_PROPERTY(bool isDarkMode READ isDarkMode NOTIFY themeChanged)

    // Surface colors exposed for QML convenience
    Q_PROPERTY(QColor primarySurface READ primarySurface NOTIFY themeChanged)
    Q_PROPERTY(QColor elevatedSurface READ elevatedSurface NOTIFY themeChanged)

    static ThemeManager& instance();

    // Factory method for QML engine singleton instantiation
    static ThemeManager* create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    ColorScheme colorScheme() const;
    void setColorScheme(ColorScheme scheme);

    Theme effectiveTheme() const;
    bool isDarkMode() const;

    QColor primarySurface() const;
    QColor elevatedSurface() const;

    void loadColorScheme();

    /**
     * @brief Re-evaluate the effective theme from current settings.
     *
     * Call after changing ToolbarStyleConfig to apply the new theme immediately.
     */
    void refreshTheme();

signals:
    void colorSchemeChanged();
    void themeChanged();

private:
    explicit ThemeManager(QObject *parent = nullptr);
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    Theme resolveSystemTheme() const;
    void updateEffectiveTheme();
    void saveColorScheme() const;

    ColorScheme m_colorScheme = ColorScheme::System;
    Theme m_effectiveTheme = Theme::Light;

    static constexpr const char* kSettingsKeyColorScheme = "qml/colorScheme";
};
