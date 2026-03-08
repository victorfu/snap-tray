#pragma once

#include <QWidget>
#include <memory>

class EmojiPicker;
class QEnterEvent;
class QHideEvent;

/**
 * @brief Standalone floating popup window that wraps EmojiPicker for PinWindow.
 *
 * Shown when the emoji sticker tool is selected in the QML sub-toolbar.
 * EmojiPicker draws via QPainter, so this remains a QWidget.
 */
class EmojiPickerPopup : public QWidget
{
    Q_OBJECT

public:
    explicit EmojiPickerPopup(QWidget* parent = nullptr);

    void positionAt(const QRect& anchorRect);
    void showAt(const QRect& anchorRect);

signals:
    void emojiSelected(const QString& emoji);
    void cursorRestoreRequested();
    void cursorSyncRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    EmojiPicker* m_picker = nullptr;
};
