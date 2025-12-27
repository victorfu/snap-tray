#include "pinwindow/UIIndicators.h"
#include "pinwindow/ClickThroughExitButton.h"

UIIndicators::UIIndicators(QWidget* parentWidget, QObject* parent)
    : QObject(parent)
    , m_parentWidget(parentWidget)
{
    setupLabels();
    setupClickThroughExitButton();
}

void UIIndicators::setupLabels()
{
    // Zoom indicator label
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

    // Opacity indicator label
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

    // OCR toast label
    m_ocrToastLabel = new QLabel(m_parentWidget);
    m_ocrToastLabel->hide();

    m_ocrToastTimer = new QTimer(this);
    m_ocrToastTimer->setSingleShot(true);
    connect(m_ocrToastTimer, &QTimer::timeout, m_ocrToastLabel, &QLabel::hide);
}

void UIIndicators::setupClickThroughExitButton()
{
    m_clickThroughExitButton = new ClickThroughExitButton();
    m_clickThroughExitButton->attachTo(m_parentWidget);
    connect(m_clickThroughExitButton, &ClickThroughExitButton::exitClicked,
            this, &UIIndicators::exitClickThroughRequested);
}

void UIIndicators::showZoomIndicator(qreal zoomLevel)
{
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
    if (enabled) {
        m_clickThroughExitButton->updatePosition();
        m_clickThroughExitButton->show();
        m_clickThroughExitButton->raise();
    } else {
        m_clickThroughExitButton->hide();
    }
}

void UIIndicators::updatePositions(const QSize& windowSize)
{
    Q_UNUSED(windowSize);
    // Update click-through exit button position if visible
    if (m_clickThroughExitButton->isVisible()) {
        m_clickThroughExitButton->updatePosition();
    }
}

void UIIndicators::showOCRToast(bool success, const QString& message)
{
    if (success) {
        m_ocrToastLabel->setStyleSheet(
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
        m_ocrToastLabel->setStyleSheet(
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

    m_ocrToastLabel->setText(message);
    m_ocrToastLabel->adjustSize();
    int x = (m_parentWidget->width() - m_ocrToastLabel->width()) / 2;
    int y = m_shadowMargin + 12;
    m_ocrToastLabel->move(x, y);
    m_ocrToastLabel->show();
    m_ocrToastLabel->raise();
    m_ocrToastTimer->start(2500);
}
