#pragma once

#include <QStringList>

namespace OCRTextUtils {
// Join native OCR words without introducing spaces between unspaced scripts.
// Text inside each word is preserved, including native macOS line spacing.
QString joinWords(const QStringList& words);
}
