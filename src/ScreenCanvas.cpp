#include "ScreenCanvas.h"

#include "InlineTextEditor.h"
#include "ScreenCanvasSession.h"
#include "cursor/CursorManager.h"
#include "region/RegionSettingsHelper.h"
#include "region/ShapeAnnotationEditor.h"
#include "region/TextAnnotationEditor.h"
#include "tools/ToolManager.h"

#include <QCloseEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QShowEvent>
#include <QTimer>

ScreenCanvas::ScreenCanvas(ScreenCanvasSession* session, QWidget* parent)
    : QWidget(parent)
    , m_session(session)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                   Qt::Tool | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    m_textEditor = new InlineTextEditor(this);
    m_textAnnotationEditor = new TextAnnotationEditor(this);
    m_shapeAnnotationEditor = std::make_unique<ShapeAnnotationEditor>();
    m_settingsHelper = new RegionSettingsHelper(this);
    m_settingsHelper->setParentWidget(this);

    CursorManager::instance().registerWidget(this, nullptr);
}

ScreenCanvas::~ScreenCanvas()
{
    CursorManager::instance().clearAllForWidget(this);
}

void ScreenCanvas::setSession(ScreenCanvasSession* session)
{
    m_session = session;
}

void ScreenCanvas::initializeForScreenSurface(QScreen* screen, const QRect& desktopGeometry)
{
    m_screen = screen;
    m_desktopGeometry = desktopGeometry;

    if (!m_screen) {
        return;
    }

    const QRect geometry = m_screen->geometry();
    setFixedSize(geometry.size());
    move(geometry.topLeft());

    if (m_settingsHelper) {
        m_settingsHelper->setParentWidget(this);
    }
    if (m_textAnnotationEditor) {
        m_textAnnotationEditor->setParentWidget(this);
    }
    if (m_toolManager) {
        m_toolManager->setTextEditingBounds(rect());
        m_toolManager->setDevicePixelRatio(m_screen->devicePixelRatio());
    }
}

void ScreenCanvas::setSharedToolManager(ToolManager* toolManager)
{
    m_toolManager = toolManager;
    auto& cursorManager = CursorManager::instance();
    cursorManager.setToolManagerForWidget(this, toolManager);

    if (m_toolManager) {
        m_toolManager->setTextEditingBounds(rect());
        if (m_screen) {
            m_toolManager->setDevicePixelRatio(m_screen->devicePixelRatio());
        }
        cursorManager.updateToolCursorForWidget(this);
        cursorManager.reapplyCursorForWidget(this);
    }
}

QScreen* ScreenCanvas::canvasScreen() const
{
    return m_screen.data();
}

QRect ScreenCanvas::screenGeometry() const
{
    return m_screen ? m_screen->geometry() : QRect();
}

QPoint ScreenCanvas::annotationOffset() const
{
    const QRect geometry = screenGeometry();
    if (!geometry.isValid() || !m_desktopGeometry.isValid()) {
        return {};
    }
    return geometry.topLeft() - m_desktopGeometry.topLeft();
}

QPoint ScreenCanvas::toAnnotationPoint(const QPoint& localPos) const
{
    return localPos + annotationOffset();
}

QPointF ScreenCanvas::toAnnotationPointF(const QPointF& localPos) const
{
    return localPos + QPointF(annotationOffset());
}

QPoint ScreenCanvas::toLocalPoint(const QPoint& annotationPos) const
{
    return annotationPos - annotationOffset();
}

QPointF ScreenCanvas::toLocalPointF(const QPointF& annotationPos) const
{
    return annotationPos - QPointF(annotationOffset());
}

void ScreenCanvas::closeFromSession()
{
    m_sessionClosing = true;
    close();
}

void ScreenCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (!m_session) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::Antialiasing);
    m_session->handleSurfacePaint(this, painter);
}

void ScreenCanvas::mousePressEvent(QMouseEvent* event)
{
    if (m_session) {
        m_session->handleSurfaceMousePress(this, event);
        return;
    }
    QWidget::mousePressEvent(event);
}

void ScreenCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (m_session) {
        m_session->handleSurfaceMouseMove(this, event);
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void ScreenCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_session) {
        m_session->handleSurfaceMouseRelease(this, event);
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ScreenCanvas::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_session) {
        m_session->handleSurfaceMouseDoubleClick(this, event);
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ScreenCanvas::wheelEvent(QWheelEvent* event)
{
    if (m_session) {
        m_session->handleSurfaceWheel(this, event);
        return;
    }
    QWidget::wheelEvent(event);
}

void ScreenCanvas::keyPressEvent(QKeyEvent* event)
{
    if (m_session) {
        m_session->handleSurfaceKeyPress(this, event);
        return;
    }
    QWidget::keyPressEvent(event);
}

void ScreenCanvas::closeEvent(QCloseEvent* event)
{
    if (m_session && !m_sessionClosing) {
        event->ignore();
        emit closeRequested();
        m_session->handleSurfaceCloseRequest(this);
        return;
    }

    QWidget::closeEvent(event);
}

void ScreenCanvas::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    QTimer::singleShot(100, this, [this]() {
        CursorManager::instance().reapplyCursorForWidget(this);
    });
}
