#ifndef SCROLLCAPTURECONTROLBAR_H
#define SCROLLCAPTURECONTROLBAR_H

#include <QWidget>

class QLabel;
class QPushButton;

class ScrollCaptureControlBar : public QWidget
{
    Q_OBJECT

public:
    enum class Mode {
        PresetSelection,
        Capturing,
        Error
    };

    explicit ScrollCaptureControlBar(QWidget* parent = nullptr);

    void setMode(Mode mode);
    void setStatusText(const QString& text);
    void setProgress(int frameCount, int stitchedHeight);
    void setOpenSettingsVisible(bool visible);
    void positionNear(const QRect& targetRect);

signals:
    void presetWebRequested();
    void presetChatRequested();
    void stopRequested();
    void cancelRequested();
    void openSettingsRequested();

private:
    void updateVisibilityForMode();

    Mode m_mode = Mode::PresetSelection;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_progressLabel = nullptr;
    QPushButton* m_webButton = nullptr;
    QPushButton* m_chatButton = nullptr;
    QPushButton* m_stopButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QPushButton* m_openSettingsButton = nullptr;
    bool m_openSettingsVisible = false;
};

#endif // SCROLLCAPTURECONTROLBAR_H
