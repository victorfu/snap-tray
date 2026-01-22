#include "pinwindow/UIIndicators.h"
#include "pinwindow/ClickThroughExitButton.h"

UIIndicators::UIIndicators(QWidget* parentWidget, QObject* parent)
    : QObject(parent)
    , m_parentWidget(parentWidget)
{
    // All UI components are now created lazily on first use to improve
    // initial PinWindow creation performance
}

void UIIndicators::ensureZoomLabelCreated()
{
    if (m_zoomLabel) return;

    m_zoomLabel = new QLabel(m_parentWidget);
    m_zoomLabel->setStyleSheet(
        "QLabel {"
        "  background-color: rgba(0, 0, 0, 180);"
        "  color: white;"
        "  padding: 4px 8px;"
        "  border-radius: 4px;"
        "  font-size: 12px;"
        "}"
    );
    m_zoomLabel->hide();

    m_zoomLabelTimer = new QTimer(this);
    m_zoomLabelTimer->setSingleShot(true);
    connect(m_zoomLabelTimer, &QTimer::timeout, m_zoomLabel, &QLabel::hide);
}

void UIIndicators::ensureOpacityLabelCreated()
{
    if (m_opacityLabel) return;

    m_opacityLabel = new QLabel(m_parentWidget);
    m_opacityLabel->setStyleSheet(
        "QLabel {"
        "  background-color: rgba(0, 0, 0, 180);"
        "  color: white;"
        "  padding: 4px 8px;"
        "  border-radius: 4px;"
        "  font-size: 12px;"
        "}"
    );
    m_opacityLabel->hide();

    m_opacityLabelTimer = new QTimer(this);
    m_opacityLabelTimer->setSingleShot(true);
    connect(m_opacityLabelTimer, &QTimer::timeout, m_opacityLabel, &QLabel::hide);
}

void UIIndicators::ensureToastLabelCreated()
{
    if (m_toastLabel) return;

    m_toastLabel = new QLabel(m_parentWidget);
    m_toastLabel->hide();

    m_toastTimer = new QTimer(this);
    m_toastTimer->setSingleShot(true);
    connect(m_toastTimer, &QTimer::timeout, m_toastLabel, &QLabel::hide);
}

void UIIndicators::ensureClickThroughExitButtonCreated()
{
    if (m_clickThroughExitButton) return;

    m_clickThroughExitButton = new ClickThroughExitButton(m_parentWidget);
    m_clickThroughExitButton->attachTo(m_parentWidget);
    connect(m_clickThroughExitButton, &ClickThroughExitButton::exitClicked,
            this, &UIIndicators::exitClickThroughRequested);
}

void UIIndicators::showZoomIndicator(qreal zoomLevel)
{
    ensureZoomLabelCreated();
    m_zoomLabel->setText(QString("%1%").arg(qRound(zoomLevel * 100)));
    m_zoomLabel->adjustSize();
    // Position at top-left corner
    m_zoomLabel->move(m_shadowMargin + 8, m_shadowMargin + 8);
    m_zoomLabel->show();
    m_zoomLabel->raise();
    m_zoomLabelTimer->start(1500);
}

void UIIndicators::showOpacityIndicator(qreal opacity)
{
    ensureOpacityLabelCreated();
    m_opacityLabel->setText(QString("%1%").arg(qRound(opacity * 100)));
    m_opacityLabel->adjustSize();
    // Position at bottom-left corner
    m_opacityLabel->move(m_shadowMargin + 8,
                         m_parentWidget->height() - m_shadowMargin - m_opacityLabel->height() - 8);
    m_opacityLabel->show();
    m_opacityLabel->raise();
    m_opacityLabelTimer->start(1500);
}

void UIIndicators::showClickThroughIndicator(bool enabled)
{
    ensureClickThroughExitButtonCreated();
    if (enabled) {
        m_clickThroughExitButton->updatePosition();
        m_clickThroughExitButton->show();
        m_clickThroughExitButton->raise();
    } else {
        m_clickThroughExitButton->hide();
    }
}

void UIIndicators::showToast(bool success, const QString& message)
{
    ensureToastLabelCreated();
    if (success) {
        m_toastLabel->setStyleSheet(
            "QLabel {"
            "  background-color: rgba(34, 139, 34, 220);"
            "  color: white;"
            "  padding: 8px 16px;"
            "  border-radius: 6px;"
            "  font-size: 13px;"
            "  font-weight: bold;"
            "}"
        );
    } else {
        m_toastLabel->setStyleSheet(
            "QLabel {"
            "  background-color: rgba(200, 60, 60, 220);"
            "  color: white;"
            "  padding: 8px 16px;"
            "  border-radius: 6px;"
            "  font-size: 13px;"
            "  font-weight: bold;"
            "}"
        );
    }

    m_toastLabel->setText(message);
    m_toastLabel->adjustSize();
    int x = (m_parentWidget->width() - m_toastLabel->width()) / 2;
    int y = m_shadowMargin + 12;
    m_toastLabel->move(x, y);
    m_toastLabel->show();
    m_toastLabel->raise();
    m_toastTimer->start(2500);
}
