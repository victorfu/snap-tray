#ifndef SNAPTRAY_STYLED_COLOR_DIALOG_H
#define SNAPTRAY_STYLED_COLOR_DIALOG_H

#include "colorwidgets/ColorDialog.h"

class QLabel;

namespace snaptray {
namespace colorwidgets {

/**
 * @brief StyledColorDialog - A SnapTray-themed color dialog
 *
 * This dialog applies SnapTray toolbar-style-aware theming to ColorDialog,
 * matching the aesthetic of other SnapTray UI components.
 *
 * Features:
 * - Theme-aware styling for dark/light toolbar modes
 * - Custom title bar with drag support
 * - Rounded corners and glass-effect background
 * - Integration with toolbar style setting
 */
class StyledColorDialog : public ColorDialog
{
    Q_OBJECT
    Q_PROPERTY(bool customTitleBar READ hasCustomTitleBar WRITE setCustomTitleBar)

public:
    explicit StyledColorDialog(QWidget* parent = nullptr);
    explicit StyledColorDialog(const QColor& initial, QWidget* parent = nullptr);
    ~StyledColorDialog() override;

    bool hasCustomTitleBar() const;
    void setCustomTitleBar(bool custom);

    // Static convenience method with SnapTray styling
    static QColor getColor(const QColor& initial = Qt::white, QWidget* parent = nullptr,
                           const QString& title = QString());

protected:
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void applySnapTrayStyle();
    void setupCustomTitleBar();

    bool m_customTitleBar = true;
    bool m_dragging = false;
    QPoint m_dragStartPos;

    QLabel* m_titleLabel = nullptr;
    QPushButton* m_closeButton = nullptr;
};

}  // namespace colorwidgets
}  // namespace snaptray

#endif  // SNAPTRAY_STYLED_COLOR_DIALOG_H
