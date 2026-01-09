#ifndef SCROLLINGCAPTUREONBOARDING_H
#define SCROLLINGCAPTUREONBOARDING_H

#include <QWidget>
#include <QPropertyAnimation>
#include <QLabel>
#include <QPushButton>

class ScrollingCaptureOnboarding : public QWidget {
    Q_OBJECT
public:
    explicit ScrollingCaptureOnboarding(QWidget* parent = nullptr);
    
    static bool hasSeenTutorial();
    static void markTutorialSeen();
    
    void showTutorial();  // Full tutorial overlay
    void showHint();      // Brief arrow hint (auto-hide 2-3s)
    void hide();
    
signals:
    void tutorialCompleted();
    void tutorialSkipped();
    
private:
    void setupTutorialUI();
    void setupHintUI();
    void startBounceAnimation();
    
    QWidget* m_tutorialOverlay = nullptr;
    QWidget* m_hintWidget = nullptr;
    QLabel* m_hintArrow = nullptr;
    QLabel* m_hintText = nullptr;
    QPropertyAnimation* m_bounceAnim = nullptr;
    QTimer* m_hintHideTimer = nullptr;
    
    static constexpr const char* SETTINGS_KEY = "scrollingCapture/tutorialSeen";
};

#endif // SCROLLINGCAPTUREONBOARDING_H
