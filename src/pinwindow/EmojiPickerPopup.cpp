#include "pinwindow/EmojiPickerPopup.h"
#include "EmojiPicker.h"
#include "platform/WindowLevel.h"

#include <QEnterEvent>
#include <QPainter>
#include <QMouseEvent>
#include <QScreen>
#include <QGuiApplication>

EmojiPickerPopup::EmojiPickerPopup(QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setCursor(Qt::ArrowCursor);

    m_picker = new EmojiPicker(this);
    m_picker->setVisible(true);

    connect(m_picker, &EmojiPicker::emojiSelected,
            this, &EmojiPickerPopup::emojiSelected);
}

void EmojiPickerPopup::positionAt(const QRect& anchorRect)
{
    // Position emoji picker below the anchor rect
    m_picker->updatePosition(QRect(0, 0, 0, 0), false);
    QRect pickerRect = m_picker->boundingRect();
    setFixedSize(pickerRect.width(), pickerRect.height());

    // Position below the anchor, left-aligned
    int x = anchorRect.left();
    int y = anchorRect.bottom() + 4;

    // Keep on screen
    QScreen* screen = QGuiApplication::screenAt(anchorRect.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect screenGeom = screen->geometry();
        if (y + height() > screenGeom.bottom() - 10)
            y = anchorRect.top() - height() - 4;
        x = qBound(screenGeom.left() + 10, x, screenGeom.right() - width() - 10);
    }

    move(x, y);
}

void EmojiPickerPopup::showAt(const QRect& anchorRect)
{
    positionAt(anchorRect);
    QWidget::show();
    raise();
    forceNativeArrowCursor(this);
    emit cursorSyncRequested();
}

void EmojiPickerPopup::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QRect pickerRect = m_picker->boundingRect();
    painter.translate(-pickerRect.x(), -pickerRect.y());
    m_picker->draw(painter);
}

void EmojiPickerPopup::enterEvent(QEnterEvent* event)
{
    forceNativeArrowCursor(this);
    emit cursorSyncRequested();
    QWidget::enterEvent(event);
}

void EmojiPickerPopup::mousePressEvent(QMouseEvent* event)
{
    forceNativeArrowCursor(this);
    emit cursorSyncRequested();
    QRect pickerRect = m_picker->boundingRect();
    QPoint localPos = event->pos() + QPoint(pickerRect.x(), pickerRect.y());
    if (m_picker->handleClick(localPos)) {
        update();
        return;
    }
    QWidget::mousePressEvent(event);
}

void EmojiPickerPopup::mouseMoveEvent(QMouseEvent* event)
{
    forceNativeArrowCursor(this);
    emit cursorSyncRequested();
    QRect pickerRect = m_picker->boundingRect();
    QPoint localPos = event->pos() + QPoint(pickerRect.x(), pickerRect.y());
    if (m_picker->updateHoveredEmoji(localPos)) {
        update();
    }
}

void EmojiPickerPopup::leaveEvent(QEvent* event)
{
    m_picker->updateHoveredEmoji(QPoint(-1, -1));
    emit cursorRestoreRequested();
    update();
    QWidget::leaveEvent(event);
}

void EmojiPickerPopup::hideEvent(QHideEvent* event)
{
    emit cursorRestoreRequested();
    QWidget::hideEvent(event);
}
