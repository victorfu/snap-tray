#include "utils/OCRTextUtils.h"

namespace {
bool isUnspacedScript(char32_t character)
{
    const auto script = QChar::script(character);
    return script == QChar::Script_Han || script == QChar::Script_Hiragana
        || script == QChar::Script_Katakana;
}

bool isCjkPunctuation(char32_t character)
{
    return character <= 0xffff
        && QStringLiteral("、。，．！？：；（）［］｛｝「」『』【】〈〉《》").contains(QChar(static_cast<char16_t>(character)));
}
}

QString OCRTextUtils::joinWords(const QStringList& words)
{
    QString result;
    for (qsizetype i = 0; i < words.size(); ++i) {
        const QString& word = words[i];
        if (word.isEmpty()) {
            continue;
        }
        if (!result.isEmpty()) {
            const auto left = result.toUcs4().last();
            const auto right = word.toUcs4().first();
            bool omitSpace = (isUnspacedScript(left) && isUnspacedScript(right))
                || (isCjkPunctuation(left) && (isUnspacedScript(right) || isCjkPunctuation(right)))
                || (isCjkPunctuation(right) && isUnspacedScript(left));
            // Windows also tokenizes times as separate digit / colon / digit words.
            if (word == QStringLiteral(":") && QChar::isDigit(left)
                && i + 1 < words.size() && !words[i + 1].isEmpty()
                && QChar::isDigit(words[i + 1].toUcs4().first())) {
                omitSpace = true;
            }
            if (left == U':' && result.size() >= 2
                && result[result.size() - 2].isDigit() && QChar::isDigit(right)) {
                omitSpace = true;
            }
            if (!omitSpace && !QChar::isSpace(left) && !QChar::isSpace(right)) {
                result += QLatin1Char(' ');
            }
        }
        result += word;
    }
    return result;
}
