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

    virtual void positionAt(const QRect& anchorRect);
    virtual void showAt(const QRect& anchorRect);
    virtual void hide();
    virtual void close();
    virtual void setParentWidget(QWidget* parent);

    virtual bool isVisible() const;
    virtual QRect geometry() const;
    virtual QWindow* window() const;

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
