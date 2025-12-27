#ifndef LINESTYLESECTION_H
#define LINESTYLESECTION_H

#include <QObject>
#include <QRect>
#include "ui/IWidgetSection.h"
#include "annotations/LineStyle.h"

/**
 * @brief Line style section for ColorAndWidthWidget.
 *
 * Provides a dropdown to select line style (Solid, Dashed, Dotted).
 */
class LineStyleSection : public QObject, public IWidgetSection
{
    Q_OBJECT

public:
    explicit LineStyleSection(QObject* parent = nullptr);

    // =========================================================================
    // Line Style Management
    // =========================================================================

    void setLineStyle(LineStyle style);
    LineStyle lineStyle() const { return m_lineStyle; }

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
    void lineStyleChanged(LineStyle style);

private:
    /**
     * @brief Get the option at the given position.
     * @return -2 for button, 0-2 for dropdown options, -1 for none
     */
    int optionAtPosition(const QPoint& pos) const;

    /**
     * @brief Draw the line style icon.
     */
    void drawLineStyleIcon(QPainter& painter, LineStyle style, const QRect& rect,
                           const ToolbarStyleConfig& styleConfig) const;

    // State
    LineStyle m_lineStyle = LineStyle::Solid;
    bool m_dropdownOpen = false;
    bool m_dropdownExpandsUpward = false;
    int m_hoveredOption = -1;  // -2=button, 0-2=options, -1=none

    // Layout
    QRect m_sectionRect;
    QRect m_buttonRect;
    QRect m_dropdownRect;

    // Layout constants
    static constexpr int BUTTON_WIDTH = 42;
    static constexpr int BUTTON_HEIGHT = 24;
    static constexpr int DROPDOWN_OPTION_HEIGHT = 28;
    static constexpr int NUM_OPTIONS = 3;  // Solid, Dashed, Dotted
};

#endif // LINESTYLESECTION_H
