#ifndef ARROWSTYLESECTION_H
#define ARROWSTYLESECTION_H

#include <QObject>
#include <QRect>
#include "ui/IWidgetSection.h"
#include "annotations/ArrowAnnotation.h"  // For LineEndStyle enum

/**
 * @brief Arrow style section for ColorAndWidthWidget.
 *
 * Provides a dropdown to select arrow line end style:
 * - None: Plain line without arrowheads
 * - EndArrow: Filled triangle at end
 * - EndArrowOutline: Outline triangle at end
 * - EndArrowLine: V-line at end (two lines, no fill)
 * - BothArrow: Filled triangles at both ends
 * - BothArrowOutline: Outline triangles at both ends
 */
class ArrowStyleSection : public QObject, public IWidgetSection
{
    Q_OBJECT

public:
    explicit ArrowStyleSection(QObject* parent = nullptr);

    // =========================================================================
    // Arrow Style Management
    // =========================================================================

    void setArrowStyle(LineEndStyle style);
    LineEndStyle arrowStyle() const { return m_arrowStyle; }

    /**
     * @brief Check if the dropdown is currently open.
     */
    bool isDropdownOpen() const { return m_dropdownOpen; }

    /**
     * @brief Set whether dropdown should expand upward (when widget is above anchor).
     */
    void setDropdownExpandsUpward(bool upward) { m_dropdownExpandsUpward = upward; }

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

    /**
     * @brief Get the dropdown bounding rect (for extended hit testing).
     */
    QRect dropdownRect() const { return m_dropdownRect; }

signals:
    void arrowStyleChanged(LineEndStyle style);

private:
    /**
     * @brief Get the option at the given position.
     * @return -2 for button, 0-5 for dropdown options, -1 for none
     */
    int optionAtPosition(const QPoint& pos) const;

    /**
     * @brief Draw the arrow style icon.
     */
    void drawArrowStyleIcon(QPainter& painter, LineEndStyle style, const QRect& rect,
        const ToolbarStyleConfig& styleConfig) const;

    // State
    LineEndStyle m_arrowStyle = LineEndStyle::EndArrow;
    bool m_dropdownOpen = false;
    bool m_dropdownExpandsUpward = false;
    int m_hoveredOption = -1;  // -2=button, 0-5=options, -1=none

    // Layout
    QRect m_sectionRect;
    QRect m_buttonRect;
    QRect m_dropdownRect;

    // Layout constants
    static constexpr int BUTTON_WIDTH = 52;
    static constexpr int BUTTON_HEIGHT = 22;  // Reduced from 24 for 28px widget height
    static constexpr int DROPDOWN_OPTION_HEIGHT = 28;
};

#endif // ARROWSTYLESECTION_H
