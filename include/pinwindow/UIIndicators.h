#ifndef UIINDICATORS_H
#define UIINDICATORS_H

#include <QObject>
#include <QWidget>

class ClickThroughExitButton;

namespace SnapTray { class OverlayBadge; }

class UIIndicators : public QObject
{
    Q_OBJECT

public:
    explicit UIIndicators(QWidget* parentWidget, QObject* parent = nullptr);
    ~UIIndicators() override = default;

    // Set margins for positioning
    void setShadowMargin(int margin) { m_shadowMargin = margin; }

    // Show indicators
    void showZoomIndicator(qreal zoomLevel);
    void showOpacityIndicator(qreal opacity);
    void showClickThroughIndicator(bool enabled);

signals:
    void exitClickThroughRequested();

private:
    void ensureZoomBadgeCreated();
    void ensureOpacityBadgeCreated();
    void ensureClickThroughExitButtonCreated();

    QWidget* m_parentWidget = nullptr;
    int m_shadowMargin = 8;

    // Zoom indicator
    SnapTray::OverlayBadge* m_zoomBadge = nullptr;

    // Opacity indicator
    SnapTray::OverlayBadge* m_opacityBadge = nullptr;

    // Click-through exit button (independent floating window)
    ClickThroughExitButton* m_clickThroughExitButton = nullptr;

};

#endif // UIINDICATORS_H
