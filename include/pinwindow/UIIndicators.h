#ifndef UIINDICATORS_H
#define UIINDICATORS_H

#include <QObject>
#include <QWidget>

class ClickThroughExitButton;

namespace SnapTray { class QmlBadge; }

class UIIndicators : public QObject
{
    Q_OBJECT

public:
    explicit UIIndicators(QWidget* parentWidget, QObject* parent = nullptr);
    ~UIIndicators() override = default;

    // Set margins for positioning
    void setShadowMargin(int margin) { m_shadowMargin = margin; }

    // Show indicators
    // The text names the value ("Zoom 150%", #225). The zoom badge moves to
    // the top-right corner while that is free and the top-left is taken (Live).
    void showZoomIndicator(const QString& text, bool topRight = false);
    void showOpacityIndicator(const QString& text);
    QString zoomBadgeText() const { return m_zoomText; }
    QString opacityBadgeText() const { return m_opacityText; }
    void showClickThroughIndicator(bool enabled);

signals:
    void exitClickThroughRequested();

private:
    void ensureZoomBadgeCreated();
    void ensureOpacityBadgeCreated();
    void ensureClickThroughExitButtonCreated();

    QWidget* m_parentWidget = nullptr;
    QString m_zoomText;
    QString m_opacityText;
    int m_shadowMargin = 8;

    // Zoom indicator
    SnapTray::QmlBadge* m_zoomBadge = nullptr;

    // Opacity indicator
    SnapTray::QmlBadge* m_opacityBadge = nullptr;

    // Click-through exit button (independent floating window)
    ClickThroughExitButton* m_clickThroughExitButton = nullptr;

};

#endif // UIINDICATORS_H
