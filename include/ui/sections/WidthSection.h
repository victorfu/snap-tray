#ifndef WIDTHSECTION_H
#define WIDTHSECTION_H

#include <QObject>
#include <QRect>
#include <QColor>
#include "ui/IWidgetSection.h"

/**
 * @brief Width preview section for ColorAndWidthWidget.
 *
 * Displays a circular preview of the current stroke width.
 * Width is adjusted via mouse wheel events.
 */
class WidthSection : public QObject, public IWidgetSection
{
    Q_OBJECT

public:
    explicit WidthSection(QObject* parent = nullptr);

    // =========================================================================
    // Width Management
    // =========================================================================

    /**
     * @brief Set the width range.
     */
    void setWidthRange(int min, int max);

    /**
     * @brief Set the current width value.
     */
    void setCurrentWidth(int width);

    /**
     * @brief Get the current width value.
     */
    int currentWidth() const { return m_currentWidth; }

    /**
     * @brief Get the minimum width.
     */
    int minWidth() const { return m_minWidth; }

    /**
     * @brief Get the maximum width.
     */
    int maxWidth() const { return m_maxWidth; }

    /**
     * @brief Set the preview color (usually matches the current annotation color).
     */
    void setPreviewColor(const QColor& color) { m_previewColor = color; }

    /**
     * @brief Get the preview color.
     */
    QColor previewColor() const { return m_previewColor; }

    // =========================================================================
    // Wheel Event
    // =========================================================================

    /**
     * @brief Handle mouse wheel event for width adjustment.
     * @param delta Wheel delta (positive = scroll up = increase, negative = decrease)
     * @return true if the width changed
     */
    bool handleWheel(int delta);

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
    /**
     * @brief Emitted when the width value changes.
     */
    void widthChanged(int width);

private:
    // State
    int m_minWidth = 1;
    int m_maxWidth = 20;
    int m_currentWidth = 3;
    QColor m_previewColor = Qt::red;
    bool m_hovered = false;

    // Layout
    QRect m_sectionRect;

    // Layout constants
    static constexpr int SECTION_SIZE = 32;  // Match widget height
    static constexpr int MAX_DOT_SIZE = 22;  // Slightly reduced for 32px height
};

#endif // WIDTHSECTION_H
