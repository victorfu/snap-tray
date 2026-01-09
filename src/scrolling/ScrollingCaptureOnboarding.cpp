#include "scrolling/ScrollingCaptureOnboarding.h"
#include <QSettings>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QTimer>
#include <QGraphicsDropShadowEffect>

ScrollingCaptureOnboarding::ScrollingCaptureOnboarding(QWidget* parent)
    : QWidget(parent)
{
    // Make widget cover parent completely
    if (parent) {
        resize(parent->size());
    }
    
    // Transparent background
    setAttribute(Qt::WA_TranslucentBackground);
    
    // Create UI components
    setupTutorialUI();
    setupHintUI();
    
    m_hintHideTimer = new QTimer(this);
    m_hintHideTimer->setSingleShot(true);
    connect(m_hintHideTimer, &QTimer::timeout, this, &ScrollingCaptureOnboarding::hide);
}

bool ScrollingCaptureOnboarding::hasSeenTutorial()
{
    QSettings settings;
    return settings.value(SETTINGS_KEY, false).toBool();
}

void ScrollingCaptureOnboarding::markTutorialSeen()
{
    QSettings settings;
    settings.setValue(SETTINGS_KEY, true);
}

void ScrollingCaptureOnboarding::showTutorial()
{
    if (m_tutorialOverlay) {
        m_tutorialOverlay->show();
    }
    if (m_hintWidget) {
        m_hintWidget->hide();
    }
    show();
    raise();
}

void ScrollingCaptureOnboarding::showHint()
{
    if (m_tutorialOverlay) {
        m_tutorialOverlay->hide();
    }
    if (m_hintWidget) {
        m_hintWidget->show();
        startBounceAnimation();
    }
    show();
    raise();
    
    // Pass through mouse events for hint
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    
    m_hintHideTimer->start(3000);
}

void ScrollingCaptureOnboarding::hide()
{
    if (m_bounceAnim) {
        m_bounceAnim->stop();
    }
    QWidget::hide();
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
}

void ScrollingCaptureOnboarding::setupTutorialUI()
{
    m_tutorialOverlay = new QWidget(this);
    m_tutorialOverlay->setStyleSheet("background-color: rgba(0, 0, 0, 180);");
    m_tutorialOverlay->setGeometry(rect());
    
    QVBoxLayout* layout = new QVBoxLayout(m_tutorialOverlay);
    layout->setAlignment(Qt::AlignCenter);
    
    QWidget* card = new QWidget(m_tutorialOverlay);
    card->setFixedWidth(400);
    card->setStyleSheet("background-color: white; border-radius: 12px;");
    
    QVBoxLayout* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(24, 24, 24, 24);
    cardLayout->setSpacing(16);
    
    QLabel* title = new QLabel("Scrolling Capture", card);
    title->setStyleSheet("font-size: 20px; font-weight: bold; color: #333;");
    title->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(title);
    
    QLabel* content = new QLabel(
        "1. Select the area you want to capture\n"
        "2. Start scrolling slowly\n"
        "3. SnapTray will stitch frames automatically\n\n"
        "Tip: Maintain a steady scroll speed for best results.", card);
    content->setStyleSheet("font-size: 14px; color: #555;");
    content->setWordWrap(true);
    cardLayout->addWidget(content);
    
    QHBoxLayout* btnLayout = new QHBoxLayout();
    
    QPushButton* gotItBtn = new QPushButton("Got it", card);
    gotItBtn->setStyleSheet(
        "QPushButton { background-color: #007AFF; color: white; border-radius: 6px; padding: 8px 16px; font-weight: bold; }"
        "QPushButton:hover { background-color: #0062C4; }"
    );
    connect(gotItBtn, &QPushButton::clicked, this, [this]() {
        markTutorialSeen();
        hide();
        emit tutorialCompleted();
    });
    
    btnLayout->addStretch();
    btnLayout->addWidget(gotItBtn);
    btnLayout->addStretch();
    
    cardLayout->addLayout(btnLayout);
    layout->addWidget(card);
    
    m_tutorialOverlay->hide();
}

void ScrollingCaptureOnboarding::setupHintUI()
{
    m_hintWidget = new QWidget(this);
    m_hintWidget->setFixedSize(200, 100);
    
    // Position at bottom center
    m_hintWidget->move((width() - m_hintWidget->width()) / 2, height() - 150);
    
    QVBoxLayout* layout = new QVBoxLayout(m_hintWidget);
    layout->setSpacing(4);
    
    m_hintArrow = new QLabel("↓", m_hintWidget); // Down arrow
    m_hintArrow->setStyleSheet("font-size: 48px; color: white; font-weight: bold;");
    m_hintArrow->setAlignment(Qt::AlignCenter);
    // Add shadow
    QGraphicsDropShadowEffect* shadow = new QGraphicsDropShadowEffect(m_hintArrow);
    shadow->setBlurRadius(10);
    shadow->setColor(QColor(0, 0, 0, 150));
    shadow->setOffset(0, 2);
    m_hintArrow->setGraphicsEffect(shadow);
    
    m_hintText = new QLabel("Scroll to start", m_hintWidget);
    m_hintText->setStyleSheet("font-size: 16px; color: white; font-weight: bold;");
    m_hintText->setAlignment(Qt::AlignCenter);
    QGraphicsDropShadowEffect* shadow2 = new QGraphicsDropShadowEffect(m_hintText);
    shadow2->setBlurRadius(10);
    shadow2->setColor(QColor(0, 0, 0, 150));
    shadow2->setOffset(0, 2);
    m_hintText->setGraphicsEffect(shadow2);
    
    layout->addWidget(m_hintArrow);
    layout->addWidget(m_hintText);
    
    m_hintWidget->hide();
}

void ScrollingCaptureOnboarding::startBounceAnimation()
{
    if (!m_hintWidget) return;
    
    if (!m_bounceAnim) {
        m_bounceAnim = new QPropertyAnimation(m_hintWidget, "pos", this);
        m_bounceAnim->setDuration(1000);
        m_bounceAnim->setLoopCount(-1); // Infinite
        m_bounceAnim->setEasingCurve(QEasingCurve::InOutSine);
    }
    
    QPoint startPos = m_hintWidget->pos();
    QPoint endPos = startPos + QPoint(0, 20);
    
    m_bounceAnim->setStartValue(startPos);
    m_bounceAnim->setKeyValueAt(0.5, endPos);
    m_bounceAnim->setEndValue(startPos);
    
    m_bounceAnim->start();
}
