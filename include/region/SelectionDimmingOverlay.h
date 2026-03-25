#ifndef SELECTIONDIMMINGOVERLAY_H
#define SELECTIONDIMMINGOVERLAY_H

#include <QRect>
#include <QWidget>

class SelectionDimmingOverlay : public QWidget
{
public:
    explicit SelectionDimmingOverlay(QWidget* parent = nullptr);
    ~SelectionDimmingOverlay() override = default;

    void syncToHost(QWidget* host, const QRect& selectionRect, bool shouldShow);
    void hideOverlay();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QWidget* m_host = nullptr;
    QRect m_selectionRect;
};

#endif // SELECTIONDIMMINGOVERLAY_H
