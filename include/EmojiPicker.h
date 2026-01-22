#ifndef EMOJIPICKER_H
#define EMOJIPICKER_H

#include <QObject>
#include <QVector>
#include <QString>
#include <QRect>
#include <QPoint>
#include "ToolbarStyle.h"

class QPainter;

class EmojiPicker : public QObject
{
    Q_OBJECT

public:
    explicit EmojiPicker(QObject* parent = nullptr);

    void setCurrentEmoji(const QString& emoji);
    QString currentEmoji() const { return m_currentEmoji; }

    void setVisible(bool visible) { m_visible = visible; }
    bool isVisible() const { return m_visible; }

    void updatePosition(const QRect& anchorRect, bool above = true);
    void draw(QPainter& painter);

    QRect boundingRect() const { return m_pickerRect; }
    int emojiAtPosition(const QPoint& pos) const;
    bool updateHoveredEmoji(const QPoint& pos);
    bool handleClick(const QPoint& pos);
    bool contains(const QPoint& pos) const;

signals:
    void emojiSelected(const QString& emoji);

private:
    void updateEmojiRects();

    QVector<QString> m_emojis;
    QString m_currentEmoji;
    bool m_visible;

    QRect m_pickerRect;
    QVector<QRect> m_emojiRects;
    int m_hoveredEmoji;

    static const int EMOJI_SIZE = 24;
    static const int EMOJI_SPACING = 4;
    static const int PICKER_HEIGHT = 64;
    static const int PICKER_PADDING = 8;
    static const int EMOJIS_PER_ROW = 8;

    ToolbarStyleConfig m_styleConfig;
};

#endif // EMOJIPICKER_H
