#pragma once

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QString>

class QEvent;
class QQuickItem;
class QQuickView;
class QWidget;
class QWindow;

namespace SnapTray {

class EmojiPickerBackend;

class QmlEmojiPickerPopup : public QObject
{
    Q_OBJECT

public:
    explicit QmlEmojiPickerPopup(QObject* parent = nullptr);
    ~QmlEmojiPickerPopup() override;

    void positionAt(const QRect& anchorRect);
    void showAt(const QRect& anchorRect);
    void hide();
    void close();
    void setParentWidget(QWidget* parent);

    bool isVisible() const;
    QRect geometry() const;
    QWindow* window() const;

signals:
    void emojiSelected(const QString& emoji);

private:
    void ensureView();
    void applyPlatformWindowFlags();
    void syncCursorSurface();
    bool eventFilter(QObject* obj, QEvent* event) override;

    QQuickView* m_view = nullptr;
    QQuickItem* m_rootItem = nullptr;
    EmojiPickerBackend* m_backend = nullptr;
    QPointer<QWidget> m_parentWidget;
    QString m_cursorSurfaceId;
    QString m_cursorOwnerId;
};

} // namespace SnapTray
