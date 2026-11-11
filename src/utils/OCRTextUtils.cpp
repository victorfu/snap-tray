#include "utils/OCRTextUtils.h"

#include <algorithm>

namespace {
bool isUnspacedScript(char32_t character)
{
    const auto script = QChar::script(character);
    return script == QChar::Script_Han || script == QChar::Script_Hiragana
        || script == QChar::Script_Katakana
        || character == U'ー' || character == U'ｰ';
}

char32_t firstCodePoint(QStringView text)
{
    return text.size() >= 2 && text.front().isHighSurrogate() && text[1].isLowSurrogate()
        ? QChar::surrogateToUcs4(text.front(), text[1]) : text.front().unicode();
}

char32_t lastCodePoint(QStringView text)
{
    return text.size() >= 2 && text.back().isLowSurrogate() && text[text.size() - 2].isHighSurrogate()
        ? QChar::surrogateToUcs4(text[text.size() - 2], text.back()) : text.back().unicode();
}

bool isNumber(QStringView word)
{
    return !word.isEmpty() && std::all_of(word.begin(), word.end(), [](QChar ch) { return ch.isDigit(); });
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
    QStringView previousWord;
    bool joinAfterStandaloneColon = false;
    for (qsizetype i = 0; i < words.size(); ++i) {
        const QString& word = words[i];
        if (word.isEmpty()) {
            continue;
        }
        if (!result.isEmpty()) {
            const auto left = lastCodePoint(result);
            const auto right = firstCodePoint(word);
            bool omitSpace = (isUnspacedScript(left) && isUnspacedScript(right))
                || (isCjkPunctuation(left) && (isUnspacedScript(right) || isCjkPunctuation(right)))
                || (isCjkPunctuation(right) && isUnspacedScript(left));
            omitSpace |= joinAfterStandaloneColon;
            joinAfterStandaloneColon = false;
            // Collapse numeric time/ratio tokens only when the colon is a word
            // of its own. Preserve native boundaries such as "Step" / "1:" / "2".
            if ((word == QStringLiteral(":") || word == QStringLiteral("：")) && isNumber(previousWord)) {
                qsizetype next = i + 1;
                while (next < words.size() && words[next].isEmpty()) {
                    ++next;
                }
                if (next < words.size() && isNumber(words[next])) {
                    omitSpace = true;
                    joinAfterStandaloneColon = true;
                }
            }
            if (!omitSpace && !QChar::isSpace(left) && !QChar::isSpace(right)) {
                result += QLatin1Char(' ');
            }
        }
        result += word;
        previousWord = word;
    }
    return result;
}
