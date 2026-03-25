#include "region/SelectionDimmingOverlay.h"

#include "platform/WindowLevel.h"

#include <QPaintEvent>
#include <QPainter>
#include <QShowEvent>
#include <QWidget>

const QColor kSelectionDimmingColor(0, 0, 0, 100);

class SelectionDimmingStripWindow : public QWidget
{
public:
    SelectionDimmingStripWindow()
        : QWidget(nullptr,
                  Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                      Qt::WindowTransparentForInput | Qt::NoDropShadowWindowHint)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
        hide();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QPainter painter(this);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(event->rect(), Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.fillRect(event->rect(), kSelectionDimmingColor);
    }

    void showEvent(QShowEvent* event) override
    {
        QWidget::showEvent(event);

        setWindowClickThrough(this, true);
        setWindowExcludedFromCapture(this, true);
        setWindowVisibleOnAllWorkspaces(this, true);
        raiseWindowAboveOverlays(this);
        raise();
    }
};

SelectionDimmingOverlay::SelectionDimmingOverlay(QWidget* parent)
{
    Q_UNUSED(parent);

    for (auto& strip : m_strips) {
        strip = std::make_unique<SelectionDimmingStripWindow>();
    }
}

SelectionDimmingOverlay::~SelectionDimmingOverlay() = default;

void SelectionDimmingOverlay::syncStrip(int index, const QRect& globalRect)
{
    if (index < 0 || index >= static_cast<int>(m_strips.size())) {
        return;
    }

    auto& strip = m_strips[static_cast<size_t>(index)];
    if (!strip) {
        return;
    }

    if (!globalRect.isValid() || globalRect.isEmpty()) {
        strip->hide();
        return;
    }

    if (strip->geometry() != globalRect) {
        strip->setGeometry(globalRect);
    }
    if (!strip->isVisible()) {
        strip->show();
    }
    strip->raise();
    strip->update();
}

void SelectionDimmingOverlay::syncToHost(QWidget* host, const QRect& selectionRect, bool shouldShow)
{
    m_host = host;
    m_selectionRect = selectionRect.normalized();

    if (!m_host || !m_host->isVisible() || !shouldShow) {
        hideOverlay();
        return;
    }

    const QRect hostGlobalRect(m_host->mapToGlobal(QPoint(0, 0)), m_host->size());
    const QRect selection = m_selectionRect.intersected(m_host->rect());

    if (!selection.isValid() || selection.isEmpty()) {
        syncStrip(0, hostGlobalRect);
        syncStrip(1, QRect());
        syncStrip(2, QRect());
        syncStrip(3, QRect());
        return;
    }

    const QRect selectionGlobal(QPoint(
        hostGlobalRect.left() + selection.left(),
        hostGlobalRect.top() + selection.top()),
        selection.size());

    const QRect topRect(
        hostGlobalRect.left(),
        hostGlobalRect.top(),
        hostGlobalRect.width(),
        selectionGlobal.top() - hostGlobalRect.top());
    const QRect bottomRect(
        hostGlobalRect.left(),
        selectionGlobal.bottom() + 1,
        hostGlobalRect.width(),
        hostGlobalRect.bottom() - selectionGlobal.bottom());
    const QRect leftRect(
        hostGlobalRect.left(),
        selectionGlobal.top(),
        selectionGlobal.left() - hostGlobalRect.left(),
        selectionGlobal.height());
    const QRect rightRect(
        selectionGlobal.right() + 1,
        selectionGlobal.top(),
        hostGlobalRect.right() - selectionGlobal.right(),
        selectionGlobal.height());

    syncStrip(0, topRect);
    syncStrip(1, bottomRect);
    syncStrip(2, leftRect);
    syncStrip(3, rightRect);
}

void SelectionDimmingOverlay::hideOverlay()
{
    for (auto& strip : m_strips) {
        if (strip) {
            strip->hide();
        }
    }
}
