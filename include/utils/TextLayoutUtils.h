#pragma once
#include <QFont>
#include <QSizeF>
#include <QString>
class QTextDocument;

namespace TextLayoutUtils {
void configure(QTextDocument& document, const QFont& font);
void populate(QTextDocument& document, const QString& text, const QFont& font, qreal width);
qreal baselineAdjustment(const QTextDocument& document, const QFont& font);
QSizeF measure(const QString& text, const QFont& font, qreal width = -1.0);

// Widest of `lines` as reported by `measure`, capped at `widthLimit` (>= 0).
// Equivalent to min(max(measure(line)), widthLimit), but stops measuring once
// the cap is reached because later lines can no longer change the result.
template <typename Lines, typename Measure>
int boundedMaxLineWidth(const Lines& lines, int widthLimit, Measure&& measure)
{
    int maxWidth = 0;
    for (const auto& line : lines) {
        // Plain comparison: <windows.h> may define a max() macro.
        const int width = static_cast<int>(measure(line));
        if (width > maxWidth) {
            maxWidth = width;
        }
        if (maxWidth >= widthLimit) {
            return widthLimit;
        }
    }
    return maxWidth;
}
}
