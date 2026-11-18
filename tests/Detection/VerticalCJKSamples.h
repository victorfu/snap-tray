#pragma once

// Synthetic single-column vertical CJK samples shared by the normalizer unit
// test and the opt-in native OCR benchmark.

#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QString>
#include <QStringList>

namespace VerticalCJKSamples {

inline QString cjkFamily(const QString& text)
{
    static const QStringList candidates{
        QStringLiteral("PingFang TC"), QStringLiteral("PingFang SC"), QStringLiteral("Hiragino Sans"),
        QStringLiteral("Microsoft JhengHei"), QStringLiteral("Microsoft YaHei"), QStringLiteral("Yu Gothic"),
        QStringLiteral("Noto Sans CJK TC"), QStringLiteral("Noto Sans CJK SC"), QStringLiteral("Noto Sans CJK JP")};
    for (const QString& family : candidates) {
        if (!QFontDatabase::hasFamily(family)) {
            continue;
        }
        QFont font(family);
        QFontMetrics metrics(font);
        bool covers = true;
        for (const QChar ch : text) {
            covers = covers && metrics.inFont(ch);
        }
        if (covers) {
            return family;
        }
    }
    return {};
}

struct Style {
    int pixelSize = 40;
    QColor foreground = Qt::black;
    QColor background = Qt::white;
    int columns = 1;
    int extraWidth = 0; // Horizontal margin around the column(s)
};

// Draws each character upright in a square cell, top to bottom, the way
// vertical CJK text is set. Multiple columns read right to left.
inline QImage renderVertical(const QString& text, const QString& family, const Style& style)
{
    QFont font(family);
    font.setPixelSize(style.pixelSize);
    const int cell = style.pixelSize + style.pixelSize / 8;
    const int perColumn = (text.size() + style.columns - 1) / style.columns;
    const int columnGap = style.pixelSize / 2;
    const int margin = style.pixelSize / 2;
    const int width = 2 * margin + style.extraWidth + style.columns * cell + (style.columns - 1) * columnGap;
    const int height = 2 * margin + perColumn * cell;
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(style.background);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setFont(font);
    painter.setPen(style.foreground);
    for (int i = 0; i < text.size(); ++i) {
        const int column = i / perColumn;
        const int row = i % perColumn;
        const int x = width - margin - style.extraWidth / 2 - (column + 1) * cell - column * columnGap;
        painter.drawText(QRect(x, margin + row * cell, cell, cell), Qt::AlignCenter, QString(text[i]));
    }
    return image;
}

} // namespace VerticalCJKSamples
