#ifndef CLICKTHROUGHEXITBUTTON_H
#define CLICKTHROUGHEXITBUTTON_H

#include <QWidget>
#include <QLabel>
#include <QTimer>

class ClickThroughExitButton : public QWidget
{
    Q_OBJECT

public:
    explicit ClickThroughExitButton(QWidget* parent = nullptr);
    ~ClickThroughExitButton() override = default;

    void attachTo(QWidget* targetWindow);
    void detach();
    void updatePosition();

signals:
    void exitClicked();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void setupUI();
    void updatePositionIfNeeded();

    QWidget* m_targetWindow = nullptr;
    QLabel* m_label = nullptr;
    QTimer* m_raiseTimer = nullptr;
    int m_shadowMargin = 8;
};

#endif // CLICKTHROUGHEXITBUTTON_H
