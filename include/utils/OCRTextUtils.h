#pragma once

#include <QStringList>

namespace OCRTextUtils {
// Skip empty tokens and join Han/kana (including kana long-vowel marks) and
// selected adjacent CJK punctuation without added spaces. Standalone : / ：
// between numeric tokens is joined; other boundaries receive a space unless
// whitespace already exists. Preserve text inside native words/lines verbatim.
QString joinWords(const QStringList& words);
}
