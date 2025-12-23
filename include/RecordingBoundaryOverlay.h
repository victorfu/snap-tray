#ifndef RECORDINGBOUNDARYOVERLAY_H
#define RECORDINGBOUNDARYOVERLAY_H

#include <QWidget>
#include <QRect>

class RecordingBoundaryOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit RecordingBoundaryOverlay(QWidget *parent = nullptr);
    ~RecordingBoundaryOverlay();

    void setRegion(const QRect &region);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QRect m_region;
    static const int BORDER_WIDTH = 3;
};

#endif // RECORDINGBOUNDARYOVERLAY_H
