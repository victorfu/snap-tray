#include "qml/EmojiPickerBackend.h"

namespace SnapTray {

EmojiPickerBackend::EmojiPickerBackend(QObject* parent)
    : QObject(parent)
{
    static const QStringList kEmojis = {
        QString::fromUtf8("\xF0\x9F\x98\x80"),  // 😀
        QString::fromUtf8("\xF0\x9F\x98\x82"),  // 😂
        QString::fromUtf8("\xE2\x9D\xA4\xEF\xB8\x8F"),  // ❤️
        QString::fromUtf8("\xF0\x9F\x91\x8D"),  // 👍
        QString::fromUtf8("\xF0\x9F\x8E\x89"),  // 🎉
        QString::fromUtf8("\xE2\xAD\x90"),      // ⭐
        QString::fromUtf8("\xF0\x9F\x94\xA5"),  // 🔥
        QString::fromUtf8("\xE2\x9C\xA8"),      // ✨
        QString::fromUtf8("\xF0\x9F\x92\xA1"),  // 💡
        QString::fromUtf8("\xF0\x9F\x93\x8C"),  // 📌
        QString::fromUtf8("\xF0\x9F\x8E\xAF"),  // 🎯
        QString::fromUtf8("\xE2\x9C\x85"),      // ✅
        QString::fromUtf8("\xE2\x9D\x8C"),      // ❌
        QString::fromUtf8("\xE2\x9A\xA0\xEF\xB8\x8F"),  // ⚠️
        QString::fromUtf8("\xF0\x9F\x92\xAC"),  // 💬
        QString::fromUtf8("\xF0\x9F\x94\x92"),  // 🔒
    };

    for (const auto& emoji : kEmojis) {
        m_emojiModel.append(emoji);
    }

    if (!kEmojis.isEmpty()) {
        m_currentEmoji = kEmojis.first();
    }
}

void EmojiPickerBackend::setCurrentEmoji(const QString& emoji)
{
    if (m_currentEmoji == emoji) {
        return;
    }

    m_currentEmoji = emoji;
    emit currentEmojiChanged();
}

void EmojiPickerBackend::activateEmoji(const QString& emoji)
{
    setCurrentEmoji(emoji);
    emit emojiSelected(emoji);
}

} // namespace SnapTray
