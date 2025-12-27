#ifndef UIINDICATORS_H
#define UIINDICATORS_H

#include <QObject>
#include <QLabel>
#include <QTimer>
#include <QWidget>

class ClickThroughExitButton;

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

    // Update positions (call after window resize)
    void updatePositions(const QSize& windowSize);

    // OCR toast
    void showOCRToast(bool success, const QString& message);

signals:
    void exitClickThroughRequested();

private:
    void setupLabels();
    void setupClickThroughExitButton();

    QWidget* m_parentWidget = nullptr;
    int m_shadowMargin = 8;

    // Zoom indicator
    QLabel* m_zoomLabel = nullptr;
    QTimer* m_zoomLabelTimer = nullptr;

    // Opacity indicator
    QLabel* m_opacityLabel = nullptr;
    QTimer* m_opacityLabelTimer = nullptr;

    // Click-through exit button (independent floating window)
    ClickThroughExitButton* m_clickThroughExitButton = nullptr;

    // OCR toast
    QLabel* m_ocrToastLabel = nullptr;
    QTimer* m_ocrToastTimer = nullptr;
};

#endif // UIINDICATORS_H
