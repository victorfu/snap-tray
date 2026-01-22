#ifndef AUTOBLURSECTION_H
#define AUTOBLURSECTION_H

#include <QObject>
#include <QRect>
#include "ui/IWidgetSection.h"

/**
 * @brief Auto blur button section for ColorAndWidthWidget.
 *
 * Provides a single button to trigger automatic face/text blur detection.
 * Shows different states: normal, processing (yellow), disabled (gray).
 */
class AutoBlurSection : public QObject, public IWidgetSection
{
    Q_OBJECT

public:
    explicit AutoBlurSection(QObject* parent = nullptr);

    // =========================================================================
    // State Management
    // =========================================================================

    /**
     * @brief Set whether the button is enabled (e.g., when image is captured).
     */
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    /**
     * @brief Set whether processing is in progress (shows yellow state).
     */
    void setProcessing(bool processing);
    bool isProcessing() const { return m_processing; }

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

signals:
    /**
     * @brief Emitted when the auto blur button is clicked.
     */
    void autoBlurRequested();

private:
    void drawTooltip(QPainter& painter, const ToolbarStyleConfig& styleConfig);

    // State
    bool m_enabled = true;
    bool m_processing = false;
    bool m_hovered = false;

    // Layout
    QRect m_sectionRect;
    QRect m_buttonRect;

    // Tooltip
    QString m_tooltip = "Auto Blur Faces";

    // Layout constants
    static constexpr int BUTTON_SIZE = 22;  // Match other section button sizes
    static constexpr int SECTION_PADDING = 6;
};

#endif // AUTOBLURSECTION_H
