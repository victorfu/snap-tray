#ifndef CAPTURECHROMEWINDOW_H
#define CAPTURECHROMEWINDOW_H

#include "region/RegionPainter.h"

#include <QPoint>
#include <QRect>
#include <QWidget>

class AnnotationLayer;
class CaptureShortcutHintsOverlay;
class LoadingSpinnerRenderer;
class SelectionStateManager;
class ToolManager;

class CaptureChromeWindow : public QWidget
{
    Q_OBJECT

public:
    explicit CaptureChromeWindow(QWidget* parent = nullptr);
    ~CaptureChromeWindow() override = default;

    void setSelectionManager(SelectionStateManager* manager);
    void setAnnotationLayer(AnnotationLayer* layer);
    void setToolManager(ToolManager* manager);
    void setShortcutHintsOverlay(CaptureShortcutHintsOverlay* overlay);

    void syncToHost(QWidget* host,
                    const QRect& selectionRect,
                    bool hasActiveSelection,
                    const QRect& highlightedWindowRect,
                    qreal devicePixelRatio,
                    int cornerRadius,
                    int currentTool,
                    bool showShortcutHints,
                    LoadingSpinnerRenderer* loadingSpinner,
                    bool showBusySpinner,
                    const QPoint& busySpinnerCenter,
                    bool shouldShow);
    void hideOverlay();

    QRect lastDimensionInfoRect() const { return m_lastDimensionInfoRect; }

signals:
    void framePainted();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QWidget* m_host = nullptr;
    SelectionStateManager* m_selectionManager = nullptr;
    AnnotationLayer* m_annotationLayer = nullptr;
    ToolManager* m_toolManager = nullptr;
    CaptureShortcutHintsOverlay* m_shortcutHintsOverlay = nullptr;
    RegionPainter m_regionPainter;
    QRect m_selectionRect;
    bool m_hasActiveSelection = false;
    QRect m_highlightedWindowRect;
    qreal m_devicePixelRatio = 1.0;
    int m_cornerRadius = 0;
    int m_currentTool = 0;
    bool m_showShortcutHints = false;
    LoadingSpinnerRenderer* m_loadingSpinner = nullptr;
    bool m_showBusySpinner = false;
    QPoint m_busySpinnerCenter;
    QRect m_lastDimensionInfoRect;
    bool m_useNativeLayeredBackend = false;
};

#endif // CAPTURECHROMEWINDOW_H
