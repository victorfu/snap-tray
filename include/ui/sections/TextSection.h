#ifndef TEXTSECTION_H
#define TEXTSECTION_H

#include <QObject>
#include <QRect>
#include <QString>
#include "ui/IWidgetSection.h"

/**
 * @brief Text formatting section for ColorAndWidthWidget.
 *
 * Provides Bold/Italic/Underline toggle buttons and font size/family dropdowns.
 */
class TextSection : public QObject, public IWidgetSection
{
    Q_OBJECT

public:
    explicit TextSection(QObject* parent = nullptr);

    // =========================================================================
    // Text Formatting State
    // =========================================================================

    void setBold(bool enabled);
    bool isBold() const { return m_boldEnabled; }

    void setItalic(bool enabled);
    bool isItalic() const { return m_italicEnabled; }

    void setUnderline(bool enabled);
    bool isUnderline() const { return m_underlineEnabled; }

    void setFontSize(int size);
    int fontSize() const { return m_fontSize; }

    void setFontFamily(const QString& family);
    QString fontFamily() const { return m_fontFamily; }

    // =========================================================================
    // IWidgetSection Implementation
    // =========================================================================

    void updateLayout(int containerTop, int containerHeight, int xOffset) override;
    int preferredWidth() const override;
    QRect boundingRect() const override { return m_sectionRect; }
    void draw(QPainter& painter, const ToolbarStyleConfig& styleConfig) override;
    bool contains(const QPoint& pos) const override;
    bool handleClick(const QPoint& pos) override;
    bool updateHovered(const QPoint& pos) override;
    void resetHoverState() override;

signals:
    void boldToggled(bool enabled);
    void italicToggled(bool enabled);
    void underlineToggled(bool enabled);
    void fontSizeChanged(int size);
    void fontFamilyChanged(const QString& family);
    void fontSizeDropdownRequested(const QPoint& globalPos);
    void fontFamilyDropdownRequested(const QPoint& globalPos);

private:
    /**
     * @brief Get the text control at the given position.
     * @return Control index: 0=Bold, 1=Italic, 2=Underline, 3=FontSize, 4=FontFamily, -1=none
     */
    int controlAtPosition(const QPoint& pos) const;

    // State
    bool m_boldEnabled = true;
    bool m_italicEnabled = false;
    bool m_underlineEnabled = false;
    int m_fontSize = 16;
    QString m_fontFamily;
    int m_hoveredControl = -1;

    // Layout
    QRect m_sectionRect;
    QRect m_boldButtonRect;
    QRect m_italicButtonRect;
    QRect m_underlineButtonRect;
    QRect m_fontSizeRect;
    QRect m_fontFamilyRect;

    // Layout constants
    static constexpr int BUTTON_SIZE = 20;
    static constexpr int BUTTON_SPACING = 2;
    static constexpr int SECTION_SPACING = 8;
    static constexpr int SECTION_PADDING = 6;
    static constexpr int FONT_SIZE_WIDTH = 40;
    static constexpr int FONT_FAMILY_WIDTH = 90;
};

#endif // TEXTSECTION_H
