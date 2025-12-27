#ifndef SIZESECTION_H
#define SIZESECTION_H

#include <QObject>
#include <QRect>
#include <QVector>
#include "ui/IWidgetSection.h"
#include "annotations/StepBadgeAnnotation.h"

/**
 * @brief Size selector section for step badges (Small/Medium/Large).
 *
 * Provides three buttons to select step badge size, with circle icons
 * representing each size option.
 */
class SizeSection : public QObject, public IWidgetSection
{
    Q_OBJECT

public:
    explicit SizeSection(QObject* parent = nullptr);

    // =========================================================================
    // Size Management
    // =========================================================================

    void setSize(StepBadgeSize size);
    StepBadgeSize size() const { return m_size; }

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
    void sizeChanged(StepBadgeSize size);

private:
    /**
     * @brief Get the button at the given position.
     * @return Button index: 0=Small, 1=Medium, 2=Large, -1=none
     */
    int buttonAtPosition(const QPoint& pos) const;

    // State
    StepBadgeSize m_size = StepBadgeSize::Medium;
    int m_hoveredButton = -1;

    // Layout
    QRect m_sectionRect;
    QVector<QRect> m_buttonRects;  // 3 buttons: [Small, Medium, Large]

    // Layout constants
    static constexpr int BUTTON_SIZE = 24;
    static constexpr int BUTTON_SPACING = 2;
    static constexpr int SECTION_PADDING = 6;
};

#endif // SIZESECTION_H
