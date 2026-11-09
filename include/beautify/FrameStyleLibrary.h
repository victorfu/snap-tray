#pragma once

#include "beautify/FrameStyle.h"

#include <QObject>
#include <QVector>

namespace SnapTray {

// The saved styles (#227, #269), one list for screenshot Beautify and the
// recording frame. Every panel that shows them uses this object, so a change
// in one reaches the others at once; it is kept by BeautifySettingsManager.
class FrameStyleLibrary : public QObject
{
    Q_OBJECT

public:
    static FrameStyleLibrary& instance();

    const QVector<NamedFrameStyle>& styles() const { return m_styles; }
    int count() const { return int(m_styles.size()); }
    bool isFull() const;
    static int maxStyles();

    // The first "My style N" no saved style uses (any case).
    QString suggestedName() const;
    bool nameTaken(const QString& name, int exceptIndex = -1) const;
    // Saves style under name (trimmed): an existing name (any case) is
    // updated, otherwise a new entry is added while there is room.
    bool save(const QString& name, const FrameStyle& style);
    // Refused for an empty name or one another saved style uses (any case).
    bool rename(int index, const QString& name);
    void remove(int index);

    // Reads the list again from the settings (tests change them directly).
    void reload();

signals:
    void stylesChanged();

private:
    FrameStyleLibrary();
    void store(const QVector<NamedFrameStyle>& styles);

    QVector<NamedFrameStyle> m_styles;
};

} // namespace SnapTray
