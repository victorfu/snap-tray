#ifndef CLICKTHROUGHEXITBUTTON_H
#define CLICKTHROUGHEXITBUTTON_H

#include <QWidget>
#include <QTimer>

class ClickThroughExitButton : public QWidget
{
    Q_OBJECT

public:
    explicit ClickThroughExitButton(QWidget* parent = nullptr);
    ~ClickThroughExitButton() override = default;

    void attachTo(QWidget* targetWindow);
    void setClickThroughEnabled(bool enabled);
    void updatePosition();
    // "Click-through · Turn off ✕" (#225): the state and the exit in words.
    QString text() const;

signals:
    void exitClicked();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void setupUI();
    void updatePositionIfNeeded();

    QWidget* m_targetWindow = nullptr;
    bool m_clickThroughEnabled = false;
    bool m_hovered = false;
    QTimer* m_raiseTimer = nullptr;
    int m_shadowMargin = 8;
};

#endif // CLICKTHROUGHEXITBUTTON_H
