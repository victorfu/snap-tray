#include "pinwindow/UIIndicators.h"
#include "pinwindow/ClickThroughExitButton.h"
#include "qml/QmlBadge.h"

UIIndicators::UIIndicators(QWidget* parentWidget, QObject* parent)
    : QObject(parent)
    , m_parentWidget(parentWidget)
{
    // All UI components are now created lazily on first use to improve
    // initial PinWindow creation performance
}

void UIIndicators::ensureZoomBadgeCreated()
{
    if (m_zoomBadge) return;
    m_zoomBadge = new SnapTray::QmlBadge(m_parentWidget);
}

void UIIndicators::ensureOpacityBadgeCreated()
{
    if (m_opacityBadge) return;
    m_opacityBadge = new SnapTray::QmlBadge(m_parentWidget);
}

void UIIndicators::ensureClickThroughExitButtonCreated()
{
    if (m_clickThroughExitButton) return;

    m_clickThroughExitButton = new ClickThroughExitButton(m_parentWidget);
    m_clickThroughExitButton->attachTo(m_parentWidget);
    connect(m_clickThroughExitButton, &ClickThroughExitButton::exitClicked,
            this, &UIIndicators::exitClickThroughRequested);
}

void UIIndicators::showZoomIndicator(const QString& text, bool topRight)
{
    m_zoomText = text;
    ensureZoomBadgeCreated();
    m_zoomBadge->showBadge(text);
    // Top-left, or top-right when the top-left holds the Live indicator.
    const int x = topRight
        ? m_parentWidget->width() - m_shadowMargin - m_zoomBadge->width() - 8
        : m_shadowMargin + 8;
    m_zoomBadge->move(x, m_shadowMargin + 8);
    m_zoomBadge->raise();
}

void UIIndicators::showOpacityIndicator(const QString& text)
{
    m_opacityText = text;
    ensureOpacityBadgeCreated();
    m_opacityBadge->showBadge(text);
    // Position at bottom-left corner (after showBadge which calls setFixedSize)
    m_opacityBadge->move(m_shadowMargin + 8,
                         m_parentWidget->height() - m_shadowMargin - m_opacityBadge->height() - 8);
    m_opacityBadge->raise();
}

void UIIndicators::showClickThroughIndicator(bool enabled)
{
    ensureClickThroughExitButtonCreated();
    m_clickThroughExitButton->setClickThroughEnabled(enabled);
}
