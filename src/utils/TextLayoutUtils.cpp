#include "utils/TextLayoutUtils.h"
#include <QTextDocument>
#include <QTextBlock>
#include <QTextLayout>
#include <QFontMetrics>

void TextLayoutUtils::configure(QTextDocument& document, const QFont& font)
{
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setFlags(QTextOption::IncludeTrailingSpaces);
    constexpr qreal kTabStopDistance = 80.0;
    option.setTabStopDistance(kTabStopDistance);
    document.setDefaultTextOption(option);
    document.setDocumentMargin(0.0);
    document.setDefaultFont(font);
}

void TextLayoutUtils::populate(QTextDocument& document, const QString& text, const QFont& font, qreal width)
{
    configure(document, font);
    document.setPlainText(text);
    document.setTextWidth(width > 0 ? width : -1.0);
    (void)document.size(); // Finish layout before querying lines or drawing.
}

qreal TextLayoutUtils::baselineAdjustment(const QTextDocument& document, const QFont& font)
{
    const QTextLayout* layout = document.firstBlock().layout();
    if (!layout || layout->lineCount() == 0) return 0.0;
    return QFontMetrics(font).ascent() - layout->lineAt(0).ascent();
}

QSizeF TextLayoutUtils::measure(const QString& text, const QFont& font, qreal width)
{
    QTextDocument document;
    populate(document, text, font, width);
    return QSizeF(width > 0 ? width : document.idealWidth(),
                  document.size().height() + baselineAdjustment(document, font));
}
