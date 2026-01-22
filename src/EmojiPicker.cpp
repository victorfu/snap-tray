#include "EmojiPicker.h"
#include "GlassRenderer.h"

#include <QPainter>
#include <QDebug>

EmojiPicker::EmojiPicker(QObject* parent)
    : QObject(parent)
    , m_visible(false)
    , m_hoveredEmoji(-1)
    , m_styleConfig(ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle()))
{
    m_emojis = {
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
        QString::fromUtf8("\xF0\x9F\x94\x92")   // 🔒
    };

    if (!m_emojis.isEmpty()) {
        m_currentEmoji = m_emojis.first();
    }
}

void EmojiPicker::setCurrentEmoji(const QString& emoji)
{
    m_currentEmoji = emoji;
}

void EmojiPicker::updatePosition(const QRect& anchorRect, bool above)
{
    int pickerWidth = EMOJIS_PER_ROW * EMOJI_SIZE + (EMOJIS_PER_ROW - 1) * EMOJI_SPACING + PICKER_PADDING * 2;

    int pickerX = anchorRect.left();
    int pickerY;

    if (above) {
        pickerY = anchorRect.top() - PICKER_HEIGHT - 4;
    } else {
        pickerY = anchorRect.bottom() + 4;
    }

    if (pickerX < 5) pickerX = 5;
    if (pickerY < 5 && above) {
        pickerY = anchorRect.bottom() + 4;
    }

    m_pickerRect = QRect(pickerX, pickerY, pickerWidth, PICKER_HEIGHT);
    updateEmojiRects();
}

void EmojiPicker::updateEmojiRects()
{
    m_emojiRects.resize(m_emojis.size());

    int baseX = m_pickerRect.left() + PICKER_PADDING;
    int rowSpacing = 4;
    int topY = m_pickerRect.top() + 6;

    for (int i = 0; i < m_emojis.size(); ++i) {
        int row = i / EMOJIS_PER_ROW;
        int col = i % EMOJIS_PER_ROW;
        int x = baseX + col * (EMOJI_SIZE + EMOJI_SPACING);
        int y = topY + row * (EMOJI_SIZE + rowSpacing);
        m_emojiRects[i] = QRect(x, y, EMOJI_SIZE, EMOJI_SIZE);
    }
}

void EmojiPicker::draw(QPainter& painter)
{
    if (!m_visible) return;

    GlassRenderer::drawGlassPanel(painter, m_pickerRect, m_styleConfig, 6);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QFont font;
#ifdef Q_OS_MAC
    font.setFamily("Apple Color Emoji");
#elif defined(Q_OS_WIN)
    font.setFamily("Segoe UI Emoji");
#else
    font.setFamily("Noto Color Emoji");
#endif
    font.setPointSize(14);
    painter.setFont(font);

    for (int i = 0; i < m_emojis.size(); ++i) {
        QRect emojiRect = m_emojiRects[i];

        if (i == m_hoveredEmoji) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_styleConfig.hoverBackgroundColor);
            painter.drawRoundedRect(emojiRect.adjusted(-2, -2, 2, 2), 4, 4);
        }

        if (m_emojis[i] == m_currentEmoji) {
            painter.setPen(QPen(QColor(0, 174, 255), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(emojiRect.adjusted(-2, -2, 2, 2), 4, 4);
        }

        painter.setPen(m_styleConfig.textColor);
        painter.drawText(emojiRect, Qt::AlignCenter, m_emojis[i]);
    }

    painter.restore();
}

int EmojiPicker::emojiAtPosition(const QPoint& pos) const
{
    if (!m_pickerRect.contains(pos)) return -1;

    for (int i = 0; i < m_emojiRects.size(); ++i) {
        if (m_emojiRects[i].contains(pos)) {
            return i;
        }
    }
    return -1;
}

bool EmojiPicker::updateHoveredEmoji(const QPoint& pos)
{
    int newHovered = emojiAtPosition(pos);
    if (newHovered != m_hoveredEmoji) {
        m_hoveredEmoji = newHovered;
        return true;
    }
    return false;
}

bool EmojiPicker::handleClick(const QPoint& pos)
{
    int emojiIdx = emojiAtPosition(pos);
    if (emojiIdx < 0) return false;

    if (emojiIdx < m_emojis.size()) {
        m_currentEmoji = m_emojis[emojiIdx];
        emit emojiSelected(m_currentEmoji);
        qDebug() << "EmojiPicker: Emoji selected:" << m_currentEmoji;
    }

    return true;
}

bool EmojiPicker::contains(const QPoint& pos) const
{
    return m_pickerRect.contains(pos);
}
