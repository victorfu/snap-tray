#include "ScrollCaptureControlBar.h"

#include "platform/WindowLevel.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>

#include <algorithm>

ScrollCaptureControlBar::ScrollCaptureControlBar(QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("Scroll Capture"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(6);

    m_statusLabel = new QLabel(tr("Choose mode"), this);
    m_progressLabel = new QLabel(QString(), this);

    m_webButton = new QPushButton(tr("Web Page"), this);
    m_chatButton = new QPushButton(tr("Chat History"), this);
    m_stopButton = new QPushButton(tr("Stop"), this);
    m_cancelButton = new QPushButton(tr("Cancel"), this);
    m_openSettingsButton = new QPushButton(tr("Open Settings"), this);

    layout->addWidget(m_statusLabel);
    layout->addWidget(m_progressLabel);
    layout->addWidget(m_webButton);
    layout->addWidget(m_chatButton);
    layout->addWidget(m_stopButton);
    layout->addWidget(m_openSettingsButton);
    layout->addWidget(m_cancelButton);

    connect(m_webButton, &QPushButton::clicked, this, &ScrollCaptureControlBar::presetWebRequested);
    connect(m_chatButton, &QPushButton::clicked, this, &ScrollCaptureControlBar::presetChatRequested);
    connect(m_stopButton, &QPushButton::clicked, this, &ScrollCaptureControlBar::stopRequested);
    connect(m_cancelButton, &QPushButton::clicked, this, &ScrollCaptureControlBar::cancelRequested);
    connect(m_openSettingsButton, &QPushButton::clicked, this, &ScrollCaptureControlBar::openSettingsRequested);

    setStyleSheet(
        "QWidget { background: rgba(24, 24, 24, 232); border: 1px solid rgba(255,255,255,35); border-radius: 8px; }"
        "QLabel { color: #f3f3f3; font-size: 12px; }"
        "QPushButton { color: #f3f3f3; background: rgba(255,255,255,24); border: 1px solid rgba(255,255,255,35); border-radius: 6px; padding: 4px 8px; }"
        "QPushButton:hover { background: rgba(255,255,255,34); }"
    );

    setMode(Mode::PresetSelection);
}

void ScrollCaptureControlBar::setMode(Mode mode)
{
    m_mode = mode;
    updateVisibilityForMode();
}

void ScrollCaptureControlBar::setStatusText(const QString& text)
{
    if (m_statusLabel) {
        m_statusLabel->setText(text);
    }
}

void ScrollCaptureControlBar::setProgress(int frameCount, int stitchedHeight)
{
    if (!m_progressLabel) {
        return;
    }
    if (m_mode != Mode::Capturing) {
        m_progressLabel->clear();
        return;
    }

    m_progressLabel->setText(tr("Frames: %1  Height: ~%2 px")
        .arg(frameCount)
        .arg(stitchedHeight));
}

void ScrollCaptureControlBar::setOpenSettingsVisible(bool visible)
{
    m_openSettingsVisible = visible;
    updateVisibilityForMode();
}

void ScrollCaptureControlBar::positionNear(const QRect& targetRect)
{
    if (!targetRect.isValid()) {
        return;
    }

    adjustSize();
    const QSize size = this->size();

    QPoint desired(targetRect.center().x() - size.width() / 2,
                   targetRect.top() - size.height() - 10);

    QScreen* screen = QGuiApplication::screenAt(targetRect.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    if (screen) {
        const QRect bounds = screen->availableGeometry();
        if (desired.y() < bounds.top() + 6) {
            desired.setY(targetRect.bottom() + 10);
        }
        desired.setX(std::clamp(desired.x(),
                                bounds.left() + 6,
                                bounds.right() - size.width() - 6));
        desired.setY(std::clamp(desired.y(),
                                bounds.top() + 6,
                                bounds.bottom() - size.height() - 6));
    }

    move(desired);
    raiseWindowAboveMenuBar(this);
}

void ScrollCaptureControlBar::updateVisibilityForMode()
{
    const bool presetMode = (m_mode == Mode::PresetSelection);
    const bool errorMode = (m_mode == Mode::Error);
    m_progressLabel->setVisible(!presetMode && !errorMode);
    m_webButton->setVisible(presetMode);
    m_chatButton->setVisible(presetMode);
    m_stopButton->setVisible(m_mode == Mode::Capturing);
    m_openSettingsButton->setVisible(errorMode && m_openSettingsVisible);
    m_cancelButton->setVisible(true);
    adjustSize();
}
