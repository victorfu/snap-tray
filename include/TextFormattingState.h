#ifndef TEXTFORMATTINGSTATE_H
#define TEXTFORMATTINGSTATE_H

#include <QFont>
#include <QString>

/**
 * @brief Holds text formatting state for annotations.
 *
 * This struct encapsulates all font-related settings that can be
 * configured for text annotations.
 */
struct TextFormattingState {
    bool bold = true;
    bool italic = false;
    bool underline = false;
    int fontSize = 16;
    QString fontFamily;  // Empty = system default

    /**
     * @brief Convert the formatting state to a QFont.
     */
    QFont toQFont() const {
        QFont font;
        if (!fontFamily.isEmpty()) {
            font.setFamily(fontFamily);
        }
        font.setPointSize(fontSize);
        font.setBold(bold);
        font.setItalic(italic);
        font.setUnderline(underline);
        return font;
    }

    /**
     * @brief Create a TextFormattingState from a QFont.
     */
    static TextFormattingState fromQFont(const QFont& font) {
        TextFormattingState state;
        state.bold = font.bold();
        state.italic = font.italic();
        state.underline = font.underline();
        state.fontSize = font.pointSize();
        state.fontFamily = font.family();
        return state;
    }
};

#endif // TEXTFORMATTINGSTATE_H
