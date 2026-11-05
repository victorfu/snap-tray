#pragma once

#include <QObject>

// The pin hover bar (#225): zoom and opacity values, the Live status chip,
// the compact layout for narrow pins, and the requests the bar's buttons and
// value panels make. PinWindow applies the requests; this class only clamps.
class PinHoverBarViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int zoomPercent READ zoomPercent NOTIFY valuesChanged)
    Q_PROPERTY(int minZoomPercent READ minZoomPercent NOTIFY valuesChanged)
    Q_PROPERTY(int opacityPercent READ opacityPercent NOTIFY valuesChanged)
    Q_PROPERTY(int status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool compact READ compact NOTIFY compactChanged)
    Q_PROPERTY(int panel READ panel NOTIFY panelChanged)

public:
    enum Status { None = 0, Live, Paused };
    Q_ENUM(Status)
    enum Panel { NoPanel = 0, ZoomPanel, OpacityPanel };
    Q_ENUM(Panel)

    static constexpr int kMaxZoomPercent = 500;
    static constexpr int kMinOpacityPercent = 10;
    static constexpr int kMaxOpacityPercent = 100;

    explicit PinHoverBarViewModel(QObject* parent = nullptr);

    int zoomPercent() const { return m_zoomPercent; }
    int minZoomPercent() const { return m_minZoomPercent; }
    int opacityPercent() const { return m_opacityPercent; }
    int status() const { return m_status; }
    bool compact() const { return m_compact; }
    int panel() const { return m_panel; }

    void setValues(qreal zoom, qreal minZoom, qreal opacity);
    void setStatus(Status status);
    // Compact keeps the value buttons and More. PinHoverBar decides it by
    // measuring the bar against the pin (labels differ by language).
    void setCompact(bool compact);

    Q_INVOKABLE void openPanel(int panel);
    Q_INVOKABLE void closePanel();
    Q_INVOKABLE void requestZoomPercent(int percent);
    Q_INVOKABLE void requestOpacityPercent(int percent);
    Q_INVOKABLE void requestAnnotate() { emit annotateRequested(); }
    Q_INVOKABLE void requestMore() { emit moreRequested(); }
    Q_INVOKABLE void toggleStatus();

signals:
    void valuesChanged();
    void statusChanged();
    void compactChanged();
    void panelChanged();
    void zoomRequested(qreal zoom);
    void opacityRequested(qreal opacity);
    void annotateRequested();
    void moreRequested();
    void pauseRequested();
    void resumeRequested();

private:
    int m_zoomPercent = 100;
    int m_minZoomPercent = 10;
    int m_opacityPercent = 100;
    int m_status = None;
    bool m_compact = false;
    int m_panel = NoPanel;
};
