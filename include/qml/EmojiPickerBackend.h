#pragma once

#include <QObject>
#include <QVariantList>

namespace SnapTray {

class EmojiPickerBackend : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList emojiModel READ emojiModel CONSTANT)
    Q_PROPERTY(QString currentEmoji READ currentEmoji WRITE setCurrentEmoji NOTIFY currentEmojiChanged)

public:
    explicit EmojiPickerBackend(QObject* parent = nullptr);

    QVariantList emojiModel() const { return m_emojiModel; }
    QString currentEmoji() const { return m_currentEmoji; }
    void setCurrentEmoji(const QString& emoji);

    Q_INVOKABLE void activateEmoji(const QString& emoji);

signals:
    void currentEmojiChanged();
    void emojiSelected(const QString& emoji);

private:
    QVariantList m_emojiModel;
    QString m_currentEmoji;
};

} // namespace SnapTray
