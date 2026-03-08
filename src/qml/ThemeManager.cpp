#include "qml/ThemeManager.h"
#include "ui/DesignSystem.h"

#include <QJSEngine>

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
    // Forward DesignSystem's themeChanged so existing QML bindings keep working.
    connect(&DesignSystem::instance(), &DesignSystem::themeChanged,
            this, [this]() {
        Theme resolved = DesignSystem::instance().isDarkMode()
            ? Theme::Dark : Theme::Light;
        if (m_effectiveTheme != resolved) {
            m_effectiveTheme = resolved;
            emit themeChanged();
        }
    });
    updateEffectiveTheme();
}

ThemeManager& ThemeManager::instance()
{
    static ThemeManager inst;
    return inst;
}

ThemeManager* ThemeManager::create(QQmlEngine *, QJSEngine *jsEngine)
{
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
    return DesignSystem::instance().backgroundPrimary();
}

QColor ThemeManager::elevatedSurface() const
{
    return DesignSystem::instance().backgroundElevated();
}

void ThemeManager::refreshTheme()
{
    DesignSystem::instance().refreshTheme();
    // themeChanged is forwarded via the connect in the constructor
}

// ---- private ----

void ThemeManager::updateEffectiveTheme()
{
    Theme resolved = DesignSystem::instance().isDarkMode()
        ? Theme::Dark : Theme::Light;

    if (m_effectiveTheme != resolved) {
        m_effectiveTheme = resolved;
        emit themeChanged();
    }
}
