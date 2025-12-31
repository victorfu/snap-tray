#ifndef COUNTDOWNOVERLAY_H
#define COUNTDOWNOVERLAY_H

#include <QWidget>
#include <QTimer>
#include <QPropertyAnimation>

class QScreen;

/**
 * @brief Full-screen countdown overlay displayed before recording starts.
 *
 * Shows a 3-2-1 countdown with scale animation effect. The overlay
 * covers the recording region with a semi-transparent background and
 * displays large numbers in the center.
 *
 * The countdown can be cancelled by pressing Escape.
 */
class CountdownOverlay : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal animationProgress READ animationProgress WRITE setAnimationProgress)

public:
    explicit CountdownOverlay(QWidget *parent = nullptr);
    ~CountdownOverlay() override;

    /**
     * @brief Set the region to cover (in screen coordinates).
     */
    void setRegion(const QRect &region, QScreen *screen);

    /**
     * @brief Set the number of seconds to count down.
     * @param seconds Number of seconds (1-10, default 3)
     */
    void setCountdownSeconds(int seconds);

    /**
     * @brief Start the countdown animation.
     */
    void start();

    /**
     * @brief Cancel the countdown.
     */
    void cancel();

    /**
     * @brief Check if countdown is currently running.
     */
    bool isRunning() const { return m_running; }

    // Property accessors for animation
    qreal animationProgress() const { return m_animationProgress; }
    void setAnimationProgress(qreal progress);

signals:
    /**
     * @brief Emitted when the countdown reaches zero.
     */
    void countdownFinished();

    /**
     * @brief Emitted when the countdown is cancelled (e.g., by Esc key).
     */
    void countdownCancelled();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onSecondElapsed();
    void onAnimationFinished();

private:
    void startNextSecond();
    void setupWindow();

    QRect m_region;
    QScreen *m_screen = nullptr;

    int m_totalSeconds = 3;
    int m_currentSecond = 3;
    qreal m_animationProgress = 0.0;
    bool m_running = false;

    QTimer *m_secondTimer;
    QPropertyAnimation *m_scaleAnimation;
};

#endif // COUNTDOWNOVERLAY_H
