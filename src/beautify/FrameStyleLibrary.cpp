#include "beautify/FrameStyleLibrary.h"

#include "settings/BeautifySettingsManager.h"

#include <QCoreApplication>

#include <algorithm>

namespace SnapTray {

FrameStyleLibrary& FrameStyleLibrary::instance()
{
    static FrameStyleLibrary library;
    return library;
}

FrameStyleLibrary::FrameStyleLibrary()
    : m_styles(BeautifySettingsManager::instance().loadFrameStyles())
{
}

bool FrameStyleLibrary::isFull() const
{
    return m_styles.size() >= maxStyles();
}

int FrameStyleLibrary::maxStyles()
{
    return BeautifySettingsManager::kMaxCustomStyles;
}

QString FrameStyleLibrary::suggestedName() const
{
    for (int n = 1;; ++n) {
        // Kept in the "BeautifyPanel" context, where it was first translated (#227).
        const QString name = QCoreApplication::translate("BeautifyPanel", "My style %1").arg(n);
        if (!nameTaken(name)) return name;
    }
}

bool FrameStyleLibrary::nameTaken(const QString& name, int exceptIndex) const
{
    const QString trimmed = name.trimmed();
    for (int i = 0; i < m_styles.size(); ++i) {
        if (i != exceptIndex && m_styles.at(i).name.compare(trimmed, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

bool FrameStyleLibrary::save(const QString& name, const FrameStyle& style)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return false;
    QVector<NamedFrameStyle> styles = m_styles;
    auto existing = std::find_if(styles.begin(), styles.end(), [&trimmed](const NamedFrameStyle& s) {
        return s.name.compare(trimmed, Qt::CaseInsensitive) == 0;
    });
    if (existing != styles.end()) {
        existing->name = trimmed;
        existing->style = style;
    } else if (!isFull()) {
        styles.append({trimmed, style});
    } else {
        return false;
    }
    store(styles);
    return true;
}

bool FrameStyleLibrary::rename(int index, const QString& name)
{
    const QString trimmed = name.trimmed();
    if (index < 0 || index >= m_styles.size() || trimmed.isEmpty() || nameTaken(trimmed, index)) return false;
    if (m_styles.at(index).name == trimmed) return true;
    QVector<NamedFrameStyle> styles = m_styles;
    styles[index].name = trimmed;
    store(styles);
    return true;
}

void FrameStyleLibrary::remove(int index)
{
    if (index < 0 || index >= m_styles.size()) return;
    QVector<NamedFrameStyle> styles = m_styles;
    styles.removeAt(index);
    store(styles);
}

void FrameStyleLibrary::reload()
{
    m_styles = BeautifySettingsManager::instance().loadFrameStyles();
    emit stylesChanged();
}

void FrameStyleLibrary::store(const QVector<NamedFrameStyle>& styles)
{
    BeautifySettingsManager::instance().saveFrameStyles(styles);
    // Read back, so the list is exactly what the next start will see.
    m_styles = BeautifySettingsManager::instance().loadFrameStyles();
    emit stylesChanged();
}

} // namespace SnapTray
