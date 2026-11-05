#pragma once

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QTimer>

#include <functional>

class PinHoverBarViewModel;
class QQuickItem;
class QQuickView;
class QWindow;

// The windows, timing and placement of the pin hover bar (#225).
//
// The bar is a non-activating tool window inside the pin's bottom edge; the
// value panel is a popup above the bar's button. The bar appears after the
// pointer rests on the pin for kShowDelayMs and hides kHideDelayMs after the
// pointer has left the pin, the bar and the panel. While the panel is open an
// application-wide filter closes it on Esc or a press outside.
class PinHoverBar : public QObject
{
    Q_OBJECT

public:
    static constexpr int kShowDelayMs = 300;
    static constexpr int kHideDelayMs = 500;
    static constexpr int kBottomInset = 8;
    static constexpr int kPanelGap = 6;

    PinHoverBar(PinHoverBarViewModel* viewModel, QWindow* owner, QObject* parent = nullptr);
    ~PinHoverBar() override;

    // The pin's native window; it can be recreated during the pin's life.
    void setOwner(QWindow* owner);
    // The pin's visible image, in global coordinates.
    void setPinGeometry(const QRect& contentGlobal);
    void pointerEntered();
    void pointerLeft();
    // While suppressed the bar and panel are hidden and nothing shows them.
    void setSuppressed(bool suppressed);
    bool isSuppressed() const { return m_suppressed; }
    void showNow();
    void hideNow();

    bool isBarVisible() const;
    bool isPanelVisible() const;
    QWindow* barWindow() const;
    QWindow* panelWindow() const;
    QRect barGeometry() const;
    bool containsGlobal(const QPoint& pos) const;
    // Moves keyboard focus to a bar button (QML objectName).
    void focusButton(const QString& name);

    void setCursorPositionProvider(std::function<QPoint()> provider);

signals:
    void barShown();
    void barHidden();

private:
    bool ensureBar();
    bool ensurePanel();
    bool barFits() const;
    void updateCompact();
    void positionBar();
    void positionPanel();
    void onShowTimeout();
    void onHideTimeout();
    void onPanelChanged();
    void hidePanel();
    QPoint cursorPos() const;
    bool eventFilter(QObject* obj, QEvent* event) override;

    QPointer<PinHoverBarViewModel> m_viewModel;
    QPointer<QWindow> m_owner;
    QQuickView* m_barView = nullptr;
    QQuickItem* m_barRoot = nullptr;
    QQuickView* m_panelView = nullptr;
    QQuickItem* m_panelRoot = nullptr;
    QRect m_pin;
    QTimer m_showTimer;
    QTimer m_hideTimer;
    bool m_suppressed = false;
    bool m_filterInstalled = false;
    int m_lastPanel = 0;
    std::function<QPoint()> m_cursorPos;
};
