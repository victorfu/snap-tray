#pragma once

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QVariantList>

#include <functional>

class QKeyEvent;
class QQuickItem;
class QQuickView;
class QWindow;

namespace SnapTray {

/**
 * The More menu of a floating toolbar (#224).
 *
 * Shared by QmlWindowedToolbar (pin) and QmlFloatingToolbar (capture, canvas).
 * Owns the popup window, keyboard navigation (Up/Down/Enter/Esc) and
 * click-outside dismissal while visible, and emits closed() on every hide so
 * the owner can return focus to its More button.
 */
class ToolbarOverflowPopup : public QObject
{
    Q_OBJECT

public:
    // viewModel must expose handleButtonClicked(int) and the properties
    // ToolbarOverflowMenu.qml reads. Ownership is not transferred.
    ToolbarOverflowPopup(QObject* viewModel, QWindow* owner, QObject* parent = nullptr);
    ~ToolbarOverflowPopup() override;

    // Shows the menu for `buttons` anchored to `anchorGlobal` inside `bounds`,
    // or hides it when it is already visible.
    void toggle(const QVariantList& buttons, const QRect& anchorGlobal, const QRect& bounds);
    void hide();
    bool isVisible() const;
    QWindow* window() const;

    // Handles Esc/Up/Down/Enter while visible; returns true if consumed.
    bool handleKey(QKeyEvent* event);

    // Global points for which a press must not dismiss the menu (the More button).
    void setKeepOpenTest(std::function<bool(const QPoint&)> test);
    void setCursorOwnerId(const QString& ownerId);

signals:
    void closed();

private slots:
    void onActionTriggered(int buttonId);

private:
    friend class TestToolbarOverflow;
    bool ensureView();
    void position();
    bool eventFilter(QObject* obj, QEvent* event) override;

    QPointer<QObject> m_viewModel;
    QPointer<QWindow> m_owner;
    QQuickView* m_view = nullptr;
    QQuickItem* m_rootItem = nullptr;
    QString m_cursorSurfaceId;
    QString m_cursorOwnerId;
    QRect m_anchor;
    QRect m_bounds;
    std::function<bool(const QPoint&)> m_keepOpen;
    bool m_filterInstalled = false;
};

} // namespace SnapTray
