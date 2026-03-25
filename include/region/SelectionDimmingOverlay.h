#ifndef SELECTIONDIMMINGOVERLAY_H
#define SELECTIONDIMMINGOVERLAY_H

#include <QRect>
#include <array>
#include <memory>

class QWidget;
class SelectionDimmingStripWindow;

class SelectionDimmingOverlay
{
public:
    explicit SelectionDimmingOverlay(QWidget* parent = nullptr);
    ~SelectionDimmingOverlay();

    void syncToHost(QWidget* host, const QRect& selectionRect, bool shouldShow);
    void hideOverlay();

private:
    void syncStrip(int index, const QRect& globalRect);

    QWidget* m_host = nullptr;
    QRect m_selectionRect;
    std::array<std::unique_ptr<SelectionDimmingStripWindow>, 4> m_strips;
};

#endif // SELECTIONDIMMINGOVERLAY_H
