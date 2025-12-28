#ifndef ARROWSTYLESECTION_H
#define ARROWSTYLESECTION_H

#include <QObject>
#include <QRect>
#include "ui/IWidgetSection.h"
#include "annotations/ArrowAnnotation.h"  // For LineEndStyle enum

/**
 * @brief Arrow style section for ColorAndWidthWidget.
 *
 * Provides a dropdown to select arrow line end style (None, EndArrow)
 * and a toggle for polyline mode.
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

    // =========================================================================
    // Polyline Mode Management
    // =========================================================================

    void setPolylineMode(bool enabled);
    bool polylineMode() const { return m_polylineMode; }

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
    void polylineModeChanged(bool enabled);

private:
    /**
     * @brief Get the option at the given position.
     * @return -3 for polyline toggle, -2 for button, 0-1 for dropdown options, -1 for none
     */
    int optionAtPosition(const QPoint& pos) const;

    /**
     * @brief Draw the arrow style icon.
     */
    void drawArrowStyleIcon(QPainter& painter, LineEndStyle style, const QRect& rect,
        const ToolbarStyleConfig& styleConfig) const;

    /**
     * @brief Draw the polyline toggle icon.
     */
    void drawPolylineIcon(QPainter& painter, const QRect& rect, bool active,
        const ToolbarStyleConfig& styleConfig) const;

    // State
    LineEndStyle m_arrowStyle = LineEndStyle::EndArrow;
    bool m_polylineMode = false;
    bool m_dropdownOpen = false;
    bool m_dropdownExpandsUpward = false;
    int m_hoveredOption = -1;  // -3=polyline toggle, -2=button, 0-1=options, -1=none

    // Layout
    QRect m_sectionRect;
    QRect m_buttonRect;
    QRect m_polylineToggleRect;
    QRect m_dropdownRect;

    // Layout constants
    static constexpr int BUTTON_WIDTH = 52;
    static constexpr int TOGGLE_WIDTH = 28;
    static constexpr int BUTTON_HEIGHT = 24;
    static constexpr int DROPDOWN_OPTION_HEIGHT = 28;
    static constexpr int TOGGLE_SPACING = 8;  // Match SECTION_SPACING for visual consistency
};

#endif // ARROWSTYLESECTION_H
