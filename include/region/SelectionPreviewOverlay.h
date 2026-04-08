#ifndef SELECTIONPREVIEWOVERLAY_H
#define SELECTIONPREVIEWOVERLAY_H

#include <QRect>
#include <QWidget>

class QPixmap;

class SelectionPreviewOverlay : public QWidget
{
public:
    explicit SelectionPreviewOverlay(QWidget* parent = nullptr);
    ~SelectionPreviewOverlay() override = default;

    void syncToHost(QWidget* host,
                    const QRect& selectionRect,
                    const QPixmap* backgroundPixmap,
                    qreal devicePixelRatio,
                    int cornerRadius,
                    bool ratioLocked,
                    bool shouldShow);
    void hideOverlay();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QWidget* m_host = nullptr;
    const QPixmap* m_backgroundPixmap = nullptr;
    QRect m_selectionRect;
    QRect m_visualRect;
    qreal m_devicePixelRatio = 1.0;
    int m_cornerRadius = 0;
    bool m_ratioLocked = false;
};

#endif // SELECTIONPREVIEWOVERLAY_H
