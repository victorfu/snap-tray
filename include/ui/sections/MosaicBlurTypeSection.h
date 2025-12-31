#ifndef MOSAICBLURTYPESECTION_H
#define MOSAICBLURTYPESECTION_H

#include <QObject>
#include <QRect>
#include "ui/IWidgetSection.h"

/**
 * @brief Blur type dropdown section for Mosaic tool.
 *
 * Provides a dropdown for switching between Pixelate and Gaussian blur modes.
 * This affects both manual mosaic strokes and auto-blur detection.
 */
class MosaicBlurTypeSection : public QObject, public IWidgetSection
{
    Q_OBJECT

public:
    enum class BlurType {
        Pixelate,
        Gaussian
    };

    explicit MosaicBlurTypeSection(QObject* parent = nullptr);

    // =========================================================================
    // Blur Type Management
    // =========================================================================

    void setBlurType(BlurType type);
    BlurType blurType() const { return m_blurType; }

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
    void blurTypeChanged(BlurType type);

private:
    /**
     * @brief Get the option at the given position.
     * @return -2 for button, 0-1 for dropdown options, -1 for none
     */
    int optionAtPosition(const QPoint& pos) const;

    // State
    BlurType m_blurType = BlurType::Pixelate;
    bool m_dropdownOpen = false;
    bool m_dropdownExpandsUpward = false;
    int m_hoveredOption = -1;  // -2=button, 0-1=options, -1=none

    // Layout
    QRect m_sectionRect;
    QRect m_buttonRect;
    QRect m_dropdownRect;

    // Layout constants
    static constexpr int BUTTON_WIDTH = 72;
    static constexpr int BUTTON_HEIGHT = 22;
    static constexpr int DROPDOWN_OPTION_HEIGHT = 28;
};

#endif // MOSAICBLURTYPESECTION_H
