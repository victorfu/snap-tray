#ifndef SCREENCANVAS_H
#define SCREENCANVAS_H

#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QRect>
#include <QWidget>
#include <memory>

class QScreen;
class QCloseEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QShowEvent;
class QWheelEvent;
class ToolManager;
class InlineTextEditor;
class TextAnnotationEditor;
class ShapeAnnotationEditor;
class RegionSettingsHelper;
class ScreenCanvasSession;

enum class CanvasBackgroundMode {
    Screen,
    Whiteboard,
    Blackboard
};

class ScreenCanvas : public QWidget
{
    Q_OBJECT

    friend class TestScreenCanvasStyleSync;

public:
    explicit ScreenCanvas(ScreenCanvasSession* session = nullptr, QWidget* parent = nullptr);
    ~ScreenCanvas() override;

    void setSession(ScreenCanvasSession* session);
    void initializeForScreenSurface(QScreen* screen, const QRect& desktopGeometry);
    void setSharedToolManager(ToolManager* toolManager);

    QScreen* canvasScreen() const;
    QRect screenGeometry() const;
    QRect desktopGeometry() const { return m_desktopGeometry; }

    QPoint annotationOffset() const;
    QPoint toAnnotationPoint(const QPoint& localPos) const;
    QPointF toAnnotationPointF(const QPointF& localPos) const;
    QPoint toLocalPoint(const QPoint& annotationPos) const;
    QPointF toLocalPointF(const QPointF& annotationPos) const;

    InlineTextEditor* inlineTextEditor() const { return m_textEditor; }
    TextAnnotationEditor* textAnnotationEditor() const { return m_textAnnotationEditor; }
    ShapeAnnotationEditor* shapeAnnotationEditor() const { return m_shapeAnnotationEditor.get(); }
    RegionSettingsHelper* settingsHelper() const { return m_settingsHelper; }
    ToolManager* sharedToolManager() const { return m_toolManager; }

    void closeFromSession();

signals:
    void closeRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    ScreenCanvasSession* m_session = nullptr;
    QPointer<QScreen> m_screen;
    QRect m_desktopGeometry;
    ToolManager* m_toolManager = nullptr;

    InlineTextEditor* m_textEditor = nullptr;
    TextAnnotationEditor* m_textAnnotationEditor = nullptr;
    std::unique_ptr<ShapeAnnotationEditor> m_shapeAnnotationEditor;
    RegionSettingsHelper* m_settingsHelper = nullptr;

    bool m_sessionClosing = false;
};

#endif // SCREENCANVAS_H
