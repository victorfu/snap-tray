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
}
