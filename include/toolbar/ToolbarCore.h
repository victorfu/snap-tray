#ifndef TOOLBARCORE_H
#define TOOLBARCORE_H

#include <QObject>
#include <QVector>
#include <QRect>
#include <QPoint>
#include <QColor>
#include <QString>
#include <functional>
#include "ToolbarStyle.h"
#include "toolbar/ToolbarButtonConfig.h"

class QPainter;

/**
 * @brief Reusable toolbar rendering component.
 *
 * Provides toolbar background rendering with gradient/shadow, button management,
 * hover states, hit-testing, and tooltip display.
 */
class ToolbarCore : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Button configuration structure.
     *
     * Uses Toolbar::ButtonConfig for unified configuration across all toolbars.
     */
    using ButtonConfig = Toolbar::ButtonConfig;

    /**
     * @brief Function type for determining icon color.
     */
    using IconColorProvider = std::function<QColor(int buttonId, bool isActive, bool isHovered)>;

    explicit ToolbarCore(QObject* parent = nullptr);

    /**
     * @brief Set the button configurations.
     */
    void setButtons(const QVector<ButtonConfig>& buttons);

    /**
     * @brief Set the currently active button.
     * @param buttonId The active button ID, or -1 for no active button
     */
    void setActiveButton(int buttonId);

    /**
     * @brief Get the currently active button.
     */
    int activeButton() const { return m_activeButton; }

    /**
     * @brief Set the icon color provider function.
     */
    void setIconColorProvider(const IconColorProvider& provider);

    /**
     * @brief Set the toolbar position (centered at given point).
     * @param centerX X coordinate of toolbar center
     * @param bottomY Y coordinate of toolbar bottom
     */
    void setPosition(int centerX, int bottomY);

    /**
     * @brief Set position anchored to a reference rect.
     * @param referenceRect The rect to anchor below (e.g., selection area)
     * @param viewportHeight Height of the viewport for boundary checking
     */
    void setPositionForSelection(const QRect& referenceRect, int viewportHeight);

    /**
     * @brief Draw the toolbar.
     * @param painter The QPainter to draw with
     */
    void draw(QPainter& painter);

    /**
     * @brief Draw the tooltip for hovered button.
     * @param painter The QPainter to draw with
     */
    void drawTooltip(QPainter& painter);

    /**
     * @brief Get the bounding rectangle of the toolbar.
     */
    QRect boundingRect() const { return m_toolbarRect; }

    /**
     * @brief Get the button ID at the given position.
     * @return Button ID, or -1 if not on any button
     */
    int buttonAtPosition(const QPoint& pos) const;

    /**
     * @brief Update the hovered button based on mouse position.
     * @return true if the hover state changed
     */
    bool updateHoveredButton(const QPoint& pos);

    /**
     * @brief Get the currently hovered button ID.
     */
    int hoveredButton() const { return m_hoveredButton; }

    /**
     * @brief Check if position is within the toolbar.
     */
    bool contains(const QPoint& pos) const;

    /**
     * @brief Check if a button is an "active" type (stays highlighted when selected).
     */
    void setActiveButtonIds(const QVector<int>& ids) { m_activeButtonIds = ids; }

    /**
     * @brief Set the viewport width for boundary checking.
     */
    void setViewportWidth(int width) { m_viewportWidth = width; }

    /**
     * @brief Get the button ID at the given index.
     */
    int buttonIdAt(int index) const;

    /**
     * @brief Set the toolbar style.
     */
    void setStyle(ToolbarStyleType type);

    /**
     * @brief Set the toolbar style configuration directly.
     */
    void setStyleConfig(const ToolbarStyleConfig& config);

    /**
     * @brief Get the current style configuration.
     */
    const ToolbarStyleConfig& styleConfig() const { return m_styleConfig; }

signals:
    /**
     * @brief Emitted when a button is clicked.
     */
    void buttonClicked(int buttonId);

private:
    void updateButtonRects();
    QColor getIconColor(int buttonId, bool isActive, bool isHovered) const;

    QVector<ButtonConfig> m_buttons;
    int m_activeButton;
    int m_hoveredButton;

    QRect m_toolbarRect;
    QVector<QRect> m_buttonRects;
    QVector<int> m_activeButtonIds;  // Button IDs that can be "active" (tools vs actions)
    int m_viewportWidth;              // Viewport width for boundary checking

    IconColorProvider m_iconColorProvider;
    ToolbarStyleConfig m_styleConfig;

    // Layout constants
    static const int TOOLBAR_HEIGHT = 32;
    static const int BUTTON_WIDTH = 28;
    static const int BUTTON_SPACING = 2;
    static const int SEPARATOR_WIDTH = 4;
};

#endif // TOOLBARCORE_H
