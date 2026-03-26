#ifndef STATICCAPTUREBACKGROUNDWINDOW_H
#define STATICCAPTUREBACKGROUNDWINDOW_H

#include <QPixmap>
#include <QWidget>

class StaticCaptureBackgroundWindow : public QWidget
{
public:
    explicit StaticCaptureBackgroundWindow(QWidget* parent = nullptr);
    ~StaticCaptureBackgroundWindow() override = default;

    void syncToHost(QWidget* host, const QPixmap& backgroundPixmap, bool shouldShow);
    void hideOverlay();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QWidget* m_host = nullptr;
    QPixmap m_backgroundPixmap;
    bool m_useNativeLayeredBackend = false;
};

#endif // STATICCAPTUREBACKGROUNDWINDOW_H
