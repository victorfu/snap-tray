#include "video/VideoAnnotationEditor.h"
#include "video/AnnotationTimelineWidget.h"
#include "video/IVideoPlayer.h"
#include "video/TrimTimeline.h"
#include "video/VideoPlaybackWidget.h"
#include "encoding/EncoderFactory.h"
#include "encoding/NativeGifEncoder.h"
#include "encoding/WebPAnimEncoder.h"
#include "IVideoEncoder.h"
#include "GlassRenderer.h"
#include "IconRenderer.h"
#include "InlineTextEditor.h"
#include "ToolbarStyle.h"
#include <QBoxLayout>
#include <QColorDialog>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFrame>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QAbstractButton>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QShowEvent>
#include <QSlider>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <memory>

namespace {
QString rgbaString(const QColor &color)
{
    return QString("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

QString toolButtonStyle(const ToolbarStyleConfig &config, const QColor &textColor)
{
    QColor idle = config.buttonInactiveColor;
    idle.setAlpha(160);
    QColor hover = config.buttonHoverColor;
    hover.setAlpha(200);
    QColor active = config.buttonActiveColor;
    active.setAlpha(230);
    QColor border = config.hairlineBorderColor;

    return QString(
        "QToolButton { color: %1; background-color: %2; border: 1px solid %3; "
        "border-radius: 6px; padding: 2px 6px; font-size: 12px; font-weight: 500; }"
        "QToolButton:hover { background-color: %4; }"
        "QToolButton:checked { background-color: %5; color: %6; }"
    )
        .arg(rgbaString(textColor))
        .arg(rgbaString(idle))
        .arg(rgbaString(border))
        .arg(rgbaString(hover))
        .arg(rgbaString(active))
        .arg(rgbaString(config.textActiveColor));
}

QString actionButtonStyle(const ToolbarStyleConfig &config)
{
    QColor idle = config.buttonInactiveColor;
    idle.setAlpha(160);
    QColor hover = config.buttonHoverColor;
    hover.setAlpha(200);
    QColor active = config.buttonActiveColor;
    active.setAlpha(230);
    QColor border = config.hairlineBorderColor;

    return QString(
        "QPushButton { color: %1; background-color: %2; border: 1px solid %3; "
        "border-radius: 6px; padding: 2px 8px; font-size: 12px; font-weight: 500; }"
        "QPushButton:hover { background-color: %4; }"
        "QPushButton:pressed { background-color: %5; color: %6; }"
    )
        .arg(rgbaString(config.textColor))
        .arg(rgbaString(idle))
        .arg(rgbaString(border))
        .arg(rgbaString(hover))
        .arg(rgbaString(active))
        .arg(rgbaString(config.textActiveColor));
}

QString playButtonStyle(const ToolbarStyleConfig &config)
{
    QColor idle = config.buttonInactiveColor;
    idle.setAlpha(170);
    QColor hover = config.buttonHoverColor;
    hover.setAlpha(210);
    QColor active = config.buttonActiveColor;
    active.setAlpha(240);
    QColor border = config.hairlineBorderColor;

    return QString(
        "QPushButton { color: %1; background-color: %2; border: 1px solid %3; "
        "border-radius: 8px; padding: 2px 10px; font-size: 12px; font-weight: 600; }"
        "QPushButton:hover { background-color: %4; }"
        "QPushButton:checked { background-color: %5; color: %6; }"
    )
        .arg(rgbaString(config.textColor))
        .arg(rgbaString(idle))
        .arg(rgbaString(border))
        .arg(rgbaString(hover))
        .arg(rgbaString(active))
        .arg(rgbaString(config.textActiveColor));
}

QString sliderStyle(const ToolbarStyleConfig &config)
{
    QColor groove = config.buttonInactiveColor;
    groove.setAlpha(180);
    QColor active = config.buttonActiveColor;
    QColor border = config.hairlineBorderColor;

    return QString(
        "QSlider::groove:horizontal { height: 4px; background: %1; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: %2; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 12px; margin: -4px 0; background: %3; "
        "border: 1px solid %4; border-radius: 6px; }"
    )
        .arg(rgbaString(groove))
        .arg(rgbaString(active))
        .arg(rgbaString(config.textActiveColor))
        .arg(rgbaString(border));
}

class GlassPanel : public QWidget
{
public:
    explicit GlassPanel(QWidget *parent = nullptr)
        : QWidget(parent)
        , m_cornerRadius(8)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
    }

    void setCornerRadius(int radius)
    {
        if (m_cornerRadius != radius) {
            m_cornerRadius = radius;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        ToolbarStyleConfig config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
        GlassRenderer::drawGlassPanel(painter, rect(), config, m_cornerRadius);
    }

private:
    int m_cornerRadius;
};

class IconToolButton : public QAbstractButton
{
public:
    explicit IconToolButton(const QString &iconKey, QWidget *parent = nullptr)
        : QAbstractButton(parent)
        , m_iconKey(iconKey)
        , m_hovered(false)
        , m_cornerRadius(6)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setCheckable(true);
    }

    QSize sizeHint() const override { return QSize(28, 28); }
    QSize minimumSizeHint() const override { return QSize(28, 28); }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        ToolbarStyleConfig config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
        const bool active = isDown() || isChecked();

        if (m_hovered || active) {
            QColor bg = active ? config.activeBackgroundColor : config.hoverBackgroundColor;
            painter.setPen(Qt::NoPen);
            painter.setBrush(bg);
            painter.drawRoundedRect(rect().adjusted(2, 2, -2, -2), m_cornerRadius, m_cornerRadius);
        }

        QColor iconColor = (m_hovered || active) ? config.iconActiveColor : config.iconNormalColor;
        IconRenderer::instance().renderIcon(painter, rect(), m_iconKey, iconColor);
    }

    void enterEvent(QEnterEvent *event) override
    {
        m_hovered = true;
        update();
        QAbstractButton::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hovered = false;
        update();
        QAbstractButton::leaveEvent(event);
    }

private:
    QString m_iconKey;
    bool m_hovered;
    int m_cornerRadius;
};

class IconButton : public QAbstractButton
{
public:
    explicit IconButton(const QString &iconKey, QWidget *parent = nullptr)
        : QAbstractButton(parent)
        , m_iconKey(iconKey)
        , m_hovered(false)
        , m_cornerRadius(6)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
    }

    QSize sizeHint() const override { return QSize(28, 28); }
    QSize minimumSizeHint() const override { return QSize(28, 28); }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        ToolbarStyleConfig config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
        const bool active = isDown();

        if (m_hovered || active) {
            QColor bg = active ? config.activeBackgroundColor : config.hoverBackgroundColor;
            painter.setPen(Qt::NoPen);
            painter.setBrush(bg);
            painter.drawRoundedRect(rect().adjusted(2, 2, -2, -2), m_cornerRadius, m_cornerRadius);
        }

        QColor iconColor = (m_hovered || active) ? config.iconActiveColor : config.iconNormalColor;
        IconRenderer::instance().renderIcon(painter, rect(), m_iconKey, iconColor);
    }

    void enterEvent(QEnterEvent *event) override
    {
        m_hovered = true;
        update();
        QAbstractButton::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hovered = false;
        update();
        QAbstractButton::leaveEvent(event);
    }

private:
    QString m_iconKey;
    bool m_hovered;
    int m_cornerRadius;
};
}

class AnnotationCanvas : public QWidget
{
public:
    explicit AnnotationCanvas(VideoAnnotationEditor *editor, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_editor(editor)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
        setMouseTracking(true);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        if (!m_editor) {
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        m_editor->renderAnnotations(painter, rect());
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (m_editor && event->button() == Qt::LeftButton) {
            m_editor->handleCanvasMousePress(event->pos());
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_editor) {
            m_editor->handleCanvasMouseMove(event->pos());
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_editor && event->button() == Qt::LeftButton) {
            m_editor->handleCanvasMouseRelease(event->pos());
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    VideoAnnotationEditor *m_editor;
};

VideoAnnotationEditor::VideoAnnotationEditor(QWidget *parent)
    : QWidget(parent)
    , m_track(new AnnotationTrack(this))
    , m_ownsTrack(true)
{
    setupUI();

    connect(m_track->undoStack(), &QUndoStack::indexChanged, this,
            &VideoAnnotationEditor::onUndoStackIndexChanged);
}

VideoAnnotationEditor::~VideoAnnotationEditor()
{
    QCoreApplication::removePostedEvents(this);
    if (m_videoWidget) {
        m_videoWidget->blockSignals(true);
        disconnect(m_videoWidget, nullptr, this, nullptr);
        m_videoWidget->stop();
    }
    if (m_track) {
        disconnect(m_track, nullptr, this, nullptr);
        if (m_track->undoStack()) {
            disconnect(m_track->undoStack(), nullptr, this, nullptr);
        }
    }
    if (m_ownsTrack) {
        // m_track is owned by this widget and will be deleted automatically
    }
}

void VideoAnnotationEditor::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(4);

    // Toolbar
    m_toolbarWidget = new GlassPanel(this);
    m_toolbarWidget->setFixedHeight(36);
    QHBoxLayout *toolbarLayout = new QHBoxLayout(m_toolbarWidget);
    toolbarLayout->setContentsMargins(8, 4, 8, 4);
    toolbarLayout->setSpacing(2);

    // Load SVG icons
    IconRenderer &iconRenderer = IconRenderer::instance();
    iconRenderer.loadIcon("selection", ":/icons/icons/selection.svg");
    iconRenderer.loadIcon("arrow", ":/icons/icons/arrow.svg");
    iconRenderer.loadIcon("line", ":/icons/icons/line.svg");
    iconRenderer.loadIcon("rectangle", ":/icons/icons/rectangle.svg");
    iconRenderer.loadIcon("ellipse", ":/icons/icons/ellipse.svg");
    iconRenderer.loadIcon("pencil", ":/icons/icons/pencil.svg");
    iconRenderer.loadIcon("marker", ":/icons/icons/marker.svg");
    iconRenderer.loadIcon("text", ":/icons/icons/text.svg");
    iconRenderer.loadIcon("step-badge", ":/icons/icons/step-badge.svg");
    iconRenderer.loadIcon("mosaic", ":/icons/icons/mosaic.svg");
    iconRenderer.loadIcon("highlight", ":/icons/icons/highlight.svg");
    iconRenderer.loadIcon("undo", ":/icons/icons/undo.svg");
    iconRenderer.loadIcon("redo", ":/icons/icons/redo.svg");

    auto addToolButton = [this, toolbarLayout](const QString &iconKey, const QString &tooltip,
                                               Tool tool) {
        IconToolButton *btn = new IconToolButton(iconKey, m_toolbarWidget);
        btn->setToolTip(tooltip);
        btn->setFixedSize(28, 28);
        connect(btn, &QAbstractButton::clicked, this, [this, btn, tool]() {
            setCurrentTool(tool);
            // Uncheck other buttons
            for (auto *child : m_toolButtons) {
                if (child) {
                    child->setChecked(child == btn);
                }
            }
        });
        m_toolButtons.push_back(btn);
        toolbarLayout->addWidget(btn);
        return btn;
    };

    IconToolButton *selectBtn = addToolButton("selection", "Select", Tool::Select);
    selectBtn->setChecked(true);
    addToolButton("arrow", "Arrow", Tool::Arrow);
    addToolButton("line", "Line", Tool::Line);
    addToolButton("rectangle", "Rectangle", Tool::Rectangle);
    addToolButton("ellipse", "Ellipse", Tool::Ellipse);
    addToolButton("pencil", "Pencil", Tool::Pencil);
    addToolButton("marker", "Marker", Tool::Marker);
    addToolButton("text", "Text", Tool::Text);
    addToolButton("step-badge", "Step Badge", Tool::StepBadge);
    addToolButton("mosaic", "Blur", Tool::Blur);
    addToolButton("highlight", "Highlight", Tool::Highlight);

    toolbarLayout->addStretch();

    // Color button
    m_colorButton = new QToolButton(m_toolbarWidget);
    m_colorButton->setText("●");
    m_colorButton->setToolTip("Annotation Color");
    connect(m_colorButton, &QToolButton::clicked, this, [this]() {
        QColor color = QColorDialog::getColor(m_annotationColor, this, "Select Color");
        if (color.isValid()) {
            setAnnotationColor(color);
        }
    });
    toolbarLayout->addWidget(m_colorButton);

    // Width slider
    m_widthSlider = new QSlider(Qt::Horizontal, m_toolbarWidget);
    m_widthSlider->setRange(1, 10);
    m_widthSlider->setValue(m_annotationLineWidth);
    m_widthSlider->setMaximumWidth(80);
    m_widthSlider->setToolTip("Line Width");
    connect(m_widthSlider, &QSlider::valueChanged, this, &VideoAnnotationEditor::setAnnotationLineWidth);
    toolbarLayout->addWidget(m_widthSlider);

    // Undo/Redo buttons
    m_undoButton = new IconButton("undo", m_toolbarWidget);
    m_undoButton->setFixedSize(28, 28);
    m_undoButton->setToolTip("Undo");
    connect(m_undoButton, &QAbstractButton::clicked, m_track, &AnnotationTrack::undo);
    toolbarLayout->addWidget(m_undoButton);

    m_redoButton = new IconButton("redo", m_toolbarWidget);
    m_redoButton->setFixedSize(28, 28);
    m_redoButton->setToolTip("Redo");
    connect(m_redoButton, &QAbstractButton::clicked, m_track, &AnnotationTrack::redo);
    toolbarLayout->addWidget(m_redoButton);

    mainLayout->addWidget(m_toolbarWidget);

    // Video container (for painting video + annotations)
    m_videoContainer = new QWidget(this);
    m_videoContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_videoContainer->setMinimumHeight(200);
    mainLayout->addWidget(m_videoContainer, 1);

    // Video playback widget
    m_videoWidget = new VideoPlaybackWidget(m_videoContainer);
    m_videoWidget->setGeometry(m_videoContainer->rect());

    m_annotationCanvas = new AnnotationCanvas(this, m_videoContainer);
    m_annotationCanvas->setGeometry(m_videoContainer->rect());
    m_annotationCanvas->raise();

    // Inline text editor for text annotations
    m_textEditor = new InlineTextEditor(m_annotationCanvas);
    connect(m_textEditor, &InlineTextEditor::editingFinished, this,
            &VideoAnnotationEditor::onTextEditingFinished);
    connect(m_textEditor, &InlineTextEditor::editingCancelled, this,
            &VideoAnnotationEditor::onTextEditingCancelled);

    connect(m_videoWidget, &VideoPlaybackWidget::positionChanged, this,
            &VideoAnnotationEditor::onVideoPositionChanged);
    connect(m_videoWidget, &VideoPlaybackWidget::durationChanged, this,
            &VideoAnnotationEditor::onVideoDurationChanged);
    connect(m_videoWidget, &VideoPlaybackWidget::videoLoaded, this, [this]() {
        updateLayout();
    });

    // Main timeline
    m_trimTimeline = new TrimTimeline(this);
    m_trimTimeline->setFixedHeight(32);
    connect(m_trimTimeline, &TrimTimeline::seekRequested, this,
            &VideoAnnotationEditor::onTimelineSeekRequested);
    mainLayout->addWidget(m_trimTimeline);

    // Annotation timeline
    m_annotationTimeline = new AnnotationTimelineWidget(this);
    m_annotationTimeline->setTrack(m_track);
    m_annotationTimeline->setFixedHeight(60);
    connect(m_annotationTimeline, &AnnotationTimelineWidget::selectionChanged, this,
            [this](const QString &id) { m_track->setSelectedId(id); });
    connect(m_annotationTimeline, &AnnotationTimelineWidget::seekRequested, this,
            &VideoAnnotationEditor::onTimelineSeekRequested);
    connect(m_annotationTimeline, &AnnotationTimelineWidget::timingChanged, this,
            &VideoAnnotationEditor::onAnnotationTimingChanged);
    connect(m_annotationTimeline, &AnnotationTimelineWidget::timingDragging, this,
            [this](const QString &, qint64, qint64) { updateAnnotationDisplay(); });
    mainLayout->addWidget(m_annotationTimeline);

    // Playback controls
    m_controlsWidget = new GlassPanel(this);
    m_controlsWidget->setFixedHeight(36);
    QHBoxLayout *controlsLayout = new QHBoxLayout(m_controlsWidget);
    controlsLayout->setContentsMargins(8, 4, 8, 4);

    m_playButton = new QPushButton("▶", m_controlsWidget);
    m_playButton->setFixedWidth(40);
    m_playButton->setCheckable(true);
    connect(m_playButton, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) {
            play();
            m_playButton->setText("⏸");
        } else {
            pause();
            m_playButton->setText("▶");
        }
    });
    controlsLayout->addWidget(m_playButton);

    controlsLayout->addStretch();

    mainLayout->addWidget(m_controlsWidget);

    setMouseTracking(true);
    m_videoContainer->setMouseTracking(true);
    if (m_annotationCanvas) {
        m_annotationCanvas->setMouseTracking(true);
    }

    applyTheme();

    connect(m_track, &AnnotationTrack::annotationAdded,
            this, &VideoAnnotationEditor::updateAnnotationDisplay);
    connect(m_track, &AnnotationTrack::annotationRemoved,
            this, &VideoAnnotationEditor::updateAnnotationDisplay);
    connect(m_track, &AnnotationTrack::annotationUpdated,
            this, &VideoAnnotationEditor::updateAnnotationDisplay);
    connect(m_track, &AnnotationTrack::selectionChanged,
            this, &VideoAnnotationEditor::updateAnnotationDisplay);
}

void VideoAnnotationEditor::setVideoPath(const QString &path)
{
    m_videoPath = path;
    if (m_videoWidget) {
        m_videoWidget->loadVideo(path);
    }
}

QString VideoAnnotationEditor::videoPath() const
{
    return m_videoPath;
}

void VideoAnnotationEditor::applyTheme()
{
    ToolbarStyleConfig config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());

    QString baseToolStyle = toolButtonStyle(config, config.textColor);
    for (auto *btn : m_toolButtons) {
        if (btn) {
            btn->setStyleSheet(baseToolStyle);
        }
    }

    if (m_colorButton) {
        m_colorButton->setStyleSheet(toolButtonStyle(config, m_annotationColor));
    }

    // IconButton handles its own styling, no stylesheet needed for undo/redo

    if (m_playButton) {
        m_playButton->setStyleSheet(playButtonStyle(config));
    }

    if (m_widthSlider) {
        m_widthSlider->setStyleSheet(sliderStyle(config));
    }
}

void VideoAnnotationEditor::renderAnnotations(QPainter &painter, const QRect &targetRect)
{
    if (!m_videoWidget || !m_track) {
        return;
    }

    QSize videoSize = m_videoWidget->videoSize();
    if (videoSize.isEmpty()) {
        return;
    }

    m_renderer.renderToWidget(painter, m_track, m_currentTimeMs, m_durationMs,
                              targetRect, videoSize, m_track->selectedId());

    if (!m_isDrawing) {
        return;
    }

    painter.save();

    qreal scaleX = static_cast<qreal>(targetRect.width()) / videoSize.width();
    qreal scaleY = static_cast<qreal>(targetRect.height()) / videoSize.height();
    qreal scale = qMin(scaleX, scaleY);
    qreal scaledWidth = videoSize.width() * scale;
    qreal scaledHeight = videoSize.height() * scale;
    qreal offsetX = targetRect.x() + (targetRect.width() - scaledWidth) / 2.0;
    qreal offsetY = targetRect.y() + (targetRect.height() - scaledHeight) / 2.0;

    painter.translate(offsetX, offsetY);
    painter.scale(scale, scale);
    m_renderer.renderAnnotation(painter, m_currentAnnotation, QSizeF(videoSize),
                                m_currentTimeMs, m_durationMs, false);

    painter.restore();
}

void VideoAnnotationEditor::handleCanvasMousePress(const QPoint &pos)
{
    // Handle inline text editor first
    if (handleTextEditorPress(pos)) {
        return;
    }

    if (!m_track || m_videoRect.isEmpty() || !m_videoRect.contains(pos)) {
        return;
    }

    if (m_currentTool != Tool::Select) {
        beginAnnotation(pos);
        return;
    }

    QPointF localPos = pos - m_videoRect.topLeft();
    QSizeF renderSize(m_videoRect.width(), m_videoRect.height());
    QString hitId;
    for (const auto &ann : m_track->allAnnotations()) {
        if (m_renderer.hitTest(ann, localPos, renderSize)) {
            hitId = ann.id;
            break;
        }
    }
    m_track->setSelectedId(hitId);
    updateAnnotationDisplay();
}

void VideoAnnotationEditor::handleCanvasMouseMove(const QPoint &pos)
{
    // Handle text editor dragging in confirm mode
    if (m_textEditor && m_textEditor->isEditing() && m_textEditor->isConfirmMode()) {
        m_textEditor->handleMouseMove(pos);
        if (m_annotationCanvas) {
            m_annotationCanvas->setCursor(m_textEditor->contains(pos) ? Qt::SizeAllCursor
                                                                      : Qt::ArrowCursor);
        }
        return;
    }

    if (m_isDrawing) {
        updateAnnotation(pos);
    }
}

void VideoAnnotationEditor::handleCanvasMouseRelease(const QPoint &pos)
{
    // Handle text editor drag release
    if (m_textEditor && m_textEditor->isEditing() && m_textEditor->isConfirmMode()) {
        m_textEditor->handleMouseRelease(pos);
        return;
    }

    Q_UNUSED(pos);
    if (m_isDrawing) {
        finishAnnotation();
    }
}

AnnotationTrack *VideoAnnotationEditor::track() const
{
    return m_track;
}

void VideoAnnotationEditor::setTrack(AnnotationTrack *track)
{
    // 1. Disconnect all signals from old track
    if (m_track) {
        disconnect(m_track, nullptr, this, nullptr);
        if (m_undoButton) {
            disconnect(m_undoButton, nullptr, m_track, nullptr);
        }
        if (m_redoButton) {
            disconnect(m_redoButton, nullptr, m_track, nullptr);
        }
    }

    // 2. Update timeline BEFORE deleting old track (prevents dangling pointer)
    if (m_annotationTimeline) {
        m_annotationTimeline->setTrack(nullptr);
    }

    // 3. Save old track and update to new track
    AnnotationTrack *oldTrack = m_track;
    m_track = track;
    m_ownsTrack = false;

    // 4. Now safe to delete old track
    if (oldTrack && oldTrack != track) {
        // Remove parent to avoid Qt auto-delete issues
        oldTrack->setParent(nullptr);
        delete oldTrack;
    }

    // 5. Calculate next step number from existing annotations
    m_nextStepNumber = 1;
    if (m_track) {
        for (const auto &ann : m_track->allAnnotations()) {
            if (ann.type == VideoAnnotationType::StepBadge) {
                m_nextStepNumber = qMax(m_nextStepNumber, ann.stepNumber + 1);
            }
        }
    }

    // 6. Set new track to timeline
    if (m_annotationTimeline && m_track) {
        m_annotationTimeline->setTrack(m_track);
    }

    // 7. Connect signals from new track
    if (m_track) {
        connect(m_track->undoStack(), &QUndoStack::indexChanged, this,
                &VideoAnnotationEditor::onUndoStackIndexChanged);
        connect(m_track, &AnnotationTrack::annotationAdded,
                this, &VideoAnnotationEditor::updateAnnotationDisplay);
        connect(m_track, &AnnotationTrack::annotationRemoved,
                this, &VideoAnnotationEditor::updateAnnotationDisplay);
        connect(m_track, &AnnotationTrack::annotationUpdated,
                this, &VideoAnnotationEditor::updateAnnotationDisplay);
        connect(m_track, &AnnotationTrack::selectionChanged,
                this, &VideoAnnotationEditor::updateAnnotationDisplay);

        // Reconnect undo/redo buttons to new track
        if (m_undoButton) {
            connect(m_undoButton, &QAbstractButton::clicked, m_track, &AnnotationTrack::undo);
        }
        if (m_redoButton) {
            connect(m_redoButton, &QAbstractButton::clicked, m_track, &AnnotationTrack::redo);
        }
    }

    updateAnnotationDisplay();
}

void VideoAnnotationEditor::setCurrentTool(Tool tool)
{
    if (m_currentTool != tool) {
        m_currentTool = tool;
        emit toolChanged(tool);

        // Cancel any in-progress drawing
        if (m_isDrawing) {
            cancelAnnotation();
        }
    }
}

VideoAnnotationEditor::Tool VideoAnnotationEditor::currentTool() const
{
    return m_currentTool;
}

void VideoAnnotationEditor::setAnnotationColor(const QColor &color)
{
    if (m_annotationColor != color) {
        m_annotationColor = color;
        emit colorChanged(color);
        applyTheme();
    }
}

QColor VideoAnnotationEditor::annotationColor() const
{
    return m_annotationColor;
}

void VideoAnnotationEditor::setAnnotationLineWidth(int width)
{
    int newWidth = qBound(1, width, 20);
    if (m_annotationLineWidth != newWidth) {
        m_annotationLineWidth = newWidth;
        emit lineWidthChanged(newWidth);
    }
}

int VideoAnnotationEditor::annotationLineWidth() const
{
    return m_annotationLineWidth;
}

void VideoAnnotationEditor::setDefaultDuration(qint64 durationMs)
{
    m_defaultDurationMs = qMax(100LL, durationMs);
}

qint64 VideoAnnotationEditor::defaultDuration() const
{
    return m_defaultDurationMs;
}

void VideoAnnotationEditor::play()
{
    if (m_videoWidget) {
        m_videoWidget->play();
        emit playbackStateChanged(true);
    }
}

void VideoAnnotationEditor::pause()
{
    if (m_videoWidget) {
        m_videoWidget->pause();
        emit playbackStateChanged(false);
    }
}

void VideoAnnotationEditor::seek(qint64 timeMs)
{
    if (m_videoWidget) {
        m_videoWidget->seek(timeMs);
    }
}

bool VideoAnnotationEditor::isPlaying() const
{
    return m_videoWidget &&
           m_videoWidget->state() == IVideoPlayer::State::Playing;
}

qint64 VideoAnnotationEditor::currentTime() const
{
    return m_currentTimeMs;
}

qint64 VideoAnnotationEditor::duration() const
{
    return m_durationMs;
}

bool VideoAnnotationEditor::exportWithAnnotations(const QString &outputPath, int format,
                                                  std::function<void(int)> progressCallback)
{
    if (m_videoPath.isEmpty()) {
        qWarning() << "VideoAnnotationEditor::exportWithAnnotations: No video loaded";
        return false;
    }

    // Create a dedicated video player for frame extraction
    IVideoPlayer *exportPlayer = IVideoPlayer::create(this);
    if (!exportPlayer) {
        qWarning() << "VideoAnnotationEditor::exportWithAnnotations: Failed to create video player";
        return false;
    }

    if (!exportPlayer->load(m_videoPath)) {
        qWarning() << "VideoAnnotationEditor::exportWithAnnotations: Failed to load video";
        delete exportPlayer;
        return false;
    }

    // Wait for media to load using a scope guard to ensure connections are cleaned up
    QEventLoop loadLoop;
    bool mediaLoaded = false;
    {
        // Scope guard - connections are auto-disconnected when loadScope is destroyed
        QObject loadScope;
        connect(exportPlayer, &IVideoPlayer::mediaLoaded, &loadScope, [&mediaLoaded, &loadLoop]() {
            mediaLoaded = true;
            loadLoop.quit();
        });
        connect(exportPlayer, &IVideoPlayer::error, &loadScope, [&loadLoop](const QString &msg) {
            qWarning() << "VideoAnnotationEditor::exportWithAnnotations: Player error:" << msg;
            loadLoop.quit();
        });
        QTimer::singleShot(5000, &loadLoop, &QEventLoop::quit); // 5 second timeout
        loadLoop.exec();
    }

    if (!mediaLoaded || !exportPlayer->hasVideo()) {
        qWarning() << "VideoAnnotationEditor::exportWithAnnotations: Media load failed";
        delete exportPlayer;
        return false;
    }

    QSize videoSize = exportPlayer->videoSize();
    qint64 videoDuration = exportPlayer->duration();

    if (videoSize.isEmpty() || videoDuration <= 0) {
        qWarning() << "VideoAnnotationEditor::exportWithAnnotations: Invalid video dimensions or duration";
        delete exportPlayer;
        return false;
    }

    // Create encoder based on format
    EncoderFactory::Format encoderFormat;
    switch (format) {
    case 0:
        encoderFormat = EncoderFactory::Format::MP4;
        break;
    case 1:
        encoderFormat = EncoderFactory::Format::GIF;
        break;
    case 2:
        encoderFormat = EncoderFactory::Format::WebP;
        break;
    default:
        encoderFormat = EncoderFactory::Format::MP4;
        break;
    }

    EncoderFactory::EncoderConfig config;
    config.format = encoderFormat;
    config.frameSize = videoSize;
    config.frameRate = static_cast<int>(exportPlayer->frameRate());
    config.outputPath = outputPath;
    config.quality = 80;

    auto result = EncoderFactory::create(config, this);
    if (!result.success) {
        qWarning() << "VideoAnnotationEditor::exportWithAnnotations: Failed to create encoder:"
                   << result.errorMessage;
        delete exportPlayer;
        return false;
    }

    IVideoEncoder *videoEncoder = result.nativeEncoder;
    NativeGifEncoder *gifEncoder = result.gifEncoder;
    WebPAnimationEncoder *webpEncoder = result.webpEncoder;

    // Heap-allocated export state to avoid UAF from [&] captures
    struct ExportState {
        bool exportSuccess = false;
        bool exportFinished = false;
        qint64 currentPosition = 0;
        qint64 seekPosition = 0;
        int frameCount = 0;
        int frameIntervalMs = 0;
        int totalFrames = 1;
        qint64 videoDuration = 0;
        std::function<void(int)> progressCallback;
        IVideoPlayer *exportPlayer = nullptr;
        IVideoEncoder *videoEncoder = nullptr;
        NativeGifEncoder *gifEncoder = nullptr;
        WebPAnimationEncoder *webpEncoder = nullptr;
        AnnotationTrack *track = nullptr;
        VideoAnnotationRenderer *renderer = nullptr;
        QEventLoop *exportLoop = nullptr;
    };

    auto state = std::make_shared<ExportState>();
    state->frameIntervalMs = exportPlayer->frameIntervalMs();
    state->totalFrames = static_cast<int>(videoDuration / state->frameIntervalMs);
    if (state->totalFrames <= 0) state->totalFrames = 1;
    state->videoDuration = videoDuration;
    state->progressCallback = progressCallback;
    state->exportPlayer = exportPlayer;
    state->videoEncoder = videoEncoder;
    state->gifEncoder = gifEncoder;
    state->webpEncoder = webpEncoder;
    state->track = m_track;
    state->renderer = &m_renderer;

    // Event loop and scope guard for async export
    QEventLoop exportLoop;
    state->exportLoop = &exportLoop;
    QObject exportScope;  // All connections use this as context

    // Connect encoder finished signals
    auto onEncodingFinished = [state](bool success, const QString &) {
        state->exportSuccess = success;
        state->exportFinished = true;
        if (state->exportLoop) {
            state->exportLoop->quit();
        }
    };

    if (videoEncoder) {
        connect(videoEncoder, &IVideoEncoder::finished, &exportScope, onEncodingFinished);
    } else if (gifEncoder) {
        connect(gifEncoder, &NativeGifEncoder::finished, &exportScope, onEncodingFinished);
    } else if (webpEncoder) {
        connect(webpEncoder, &WebPAnimationEncoder::finished, &exportScope, onEncodingFinished);
    }

    // Frame processing - using shared_ptr to avoid UAF
    std::shared_ptr<std::function<void()>> processNextFrame = std::make_shared<std::function<void()>>();

    auto onFrameReady = [state, processNextFrame](const QImage &frame) {
        if (state->exportFinished) return;

        // Create a copy to render effects on
        QImage compositeFrame = frame.convertToFormat(QImage::Format_ARGB32_Premultiplied);

        // Calculate timestamp for this frame (relative to start)
        qint64 frameTimeMs = state->seekPosition;

        // Render annotations
        if (state->track && state->renderer) {
            state->renderer->renderToFrame(compositeFrame, state->track, frameTimeMs, state->videoDuration);
        }

        // Write frame to encoder
        if (state->videoEncoder) {
            state->videoEncoder->writeFrame(compositeFrame, frameTimeMs);
        } else if (state->gifEncoder) {
            state->gifEncoder->writeFrame(compositeFrame, frameTimeMs);
        } else if (state->webpEncoder) {
            state->webpEncoder->writeFrame(compositeFrame, frameTimeMs);
        }

        state->frameCount++;

        // Report progress
        if (state->progressCallback) {
            int percent = (state->frameCount * 100) / state->totalFrames;
            percent = qBound(0, percent, 99);
            state->progressCallback(percent);
        }

        // Schedule next frame with scope guard context
        if (*processNextFrame) {
            QTimer::singleShot(0, *processNextFrame);
        }
    };

    connect(exportPlayer, &IVideoPlayer::frameReady, &exportScope, onFrameReady);

    *processNextFrame = [state]() {
        if (state->exportFinished) return;

        state->currentPosition += state->frameIntervalMs;

        if (state->currentPosition >= state->videoDuration) {
            // Finished extracting frames
            if (state->videoEncoder) {
                state->videoEncoder->finish();
            } else if (state->gifEncoder) {
                state->gifEncoder->finish();
            } else if (state->webpEncoder) {
                state->webpEncoder->finish();
            }
            return;
        }

        state->seekPosition = state->currentPosition;
        if (state->exportPlayer) {
            state->exportPlayer->seek(state->currentPosition);
        }
    };

    // Start export by seeking to first frame
    state->seekPosition = 0;
    exportPlayer->seek(0);

    // Wait for export to complete
    exportLoop.exec();

    // Clear the loop pointer to prevent access after scope exit
    state->exportLoop = nullptr;

    // Cleanup
    delete exportPlayer;
    state->exportPlayer = nullptr;

    // Encoders are parented to this, they'll be cleaned up automatically
    // but we should disconnect signals
    if (videoEncoder) {
        videoEncoder->deleteLater();
    }
    if (gifEncoder) {
        gifEncoder->deleteLater();
    }
    if (webpEncoder) {
        webpEncoder->deleteLater();
    }

    if (progressCallback && state->exportSuccess) {
        progressCallback(100);
    }

    return state->exportSuccess;
}

bool VideoAnnotationEditor::saveProject(const QString &projectPath)
{
    QJsonObject project;
    project["version"] = 1;
    project["videoPath"] = m_videoPath;
    project["annotations"] = m_track->toJson();

    QJsonDocument doc(project);
    QFile file(projectPath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

bool VideoAnnotationEditor::loadProject(const QString &projectPath)
{
    QFile file(projectPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull() || !doc.isObject()) {
        return false;
    }

    QJsonObject project = doc.object();
    m_videoPath = project["videoPath"].toString();
    m_track->fromJson(project["annotations"].toObject());

    if (!m_videoPath.isEmpty() && m_videoWidget) {
        m_videoWidget->loadVideo(m_videoPath);
    }

    return true;
}

void VideoAnnotationEditor::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    // Main widget painting is handled by child widgets
}

void VideoAnnotationEditor::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateLayout();
}

void VideoAnnotationEditor::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    applyTheme();
    // Ensure layout is updated when widget becomes visible
    // Use singleShot to defer until after the widget is fully shown
    QPointer<VideoAnnotationEditor> safeThis(this);
    QTimer::singleShot(0, this, [safeThis]() {
        if (safeThis) {
            safeThis->updateLayout();
        }
    });
}

void VideoAnnotationEditor::mousePressEvent(QMouseEvent *event)
{
    if (m_videoContainer) {
        QPoint mappedPos = m_videoContainer->mapFrom(this, event->pos());
        if (m_videoContainer->rect().contains(mappedPos)) {
            handleCanvasMousePress(mappedPos);
            event->accept();
            return;
        }
    }

    QWidget::mousePressEvent(event);
}

void VideoAnnotationEditor::mouseMoveEvent(QMouseEvent *event)
{
    if (m_videoContainer) {
        QPoint mappedPos = m_videoContainer->mapFrom(this, event->pos());
        if (m_videoContainer->rect().contains(mappedPos) || m_isDrawing) {
            handleCanvasMouseMove(mappedPos);
            event->accept();
            return;
        }
    }

    QWidget::mouseMoveEvent(event);
}

void VideoAnnotationEditor::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_videoContainer) {
        QPoint mappedPos = m_videoContainer->mapFrom(this, event->pos());
        if (m_videoContainer->rect().contains(mappedPos) || m_isDrawing) {
            handleCanvasMouseRelease(mappedPos);
            event->accept();
            return;
        }
    }

    QWidget::mouseReleaseEvent(event);
}

void VideoAnnotationEditor::keyPressEvent(QKeyEvent *event)
{
    // Handle inline text editing keys first
    if (m_textEditor && m_textEditor->isEditing()) {
        if (event->key() == Qt::Key_Escape) {
            m_textEditor->cancelEditing();
            event->accept();
            return;
        }

        if (m_textEditor->isConfirmMode()) {
            // In confirm mode: Enter finishes editing
            if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
                m_textEditor->finishEditing();
                event->accept();
                return;
            }
        } else {
            // In typing mode: Ctrl+Enter or Shift+Enter enters confirm mode
            if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
                (event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
                if (!m_textEditor->textEdit()->toPlainText().trimmed().isEmpty()) {
                    m_textEditor->enterConfirmMode();
                }
                event->accept();
                return;
            }
        }
        // Let other keys pass through to the text editor
        return;
    }

    bool handled = false;
    if (event->key() == Qt::Key_Escape) {
        if (m_isDrawing) {
            cancelAnnotation();
        } else {
            m_track->clearSelection();
        }
        handled = true;
    } else if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        QString selectedId = m_track->selectedId();
        if (!selectedId.isEmpty()) {
            m_track->removeAnnotation(selectedId);
        }
        handled = true;
    } else if (event->matches(QKeySequence::Undo)) {
        m_track->undo();
        handled = true;
    } else if (event->matches(QKeySequence::Redo)) {
        m_track->redo();
        handled = true;
    } else if (event->key() == Qt::Key_Space) {
        if (isPlaying()) {
            pause();
        } else {
            play();
        }
        handled = true;
    }

    if (handled) {
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void VideoAnnotationEditor::onVideoPositionChanged(qint64 position)
{
    m_currentTimeMs = position;
    if (m_trimTimeline) {
        m_trimTimeline->setPosition(position);
    }
    if (m_annotationTimeline) {
        m_annotationTimeline->setCurrentTime(position);
    }
    emit positionChanged(position);
    updateAnnotationDisplay();
}

void VideoAnnotationEditor::onVideoDurationChanged(qint64 duration)
{
    m_durationMs = duration;
    if (m_trimTimeline) {
        m_trimTimeline->setDuration(duration);
    }
    if (m_annotationTimeline) {
        m_annotationTimeline->setDuration(duration);
    }
    emit durationChanged(duration);
    updateAnnotationDisplay();
}

void VideoAnnotationEditor::onTimelineSeekRequested(qint64 timeMs)
{
    seek(timeMs);
}

void VideoAnnotationEditor::onAnnotationTimingChanged(const QString &id, qint64 startMs,
                                                      qint64 endMs)
{
    m_track->setAnnotationTiming(id, startMs, endMs);
}

void VideoAnnotationEditor::onUndoStackIndexChanged()
{
    emit modifiedChanged(m_track->undoStack()->count() > 0);
    updateAnnotationDisplay();
}

void VideoAnnotationEditor::updateLayout()
{
    if (m_videoWidget && m_videoContainer) {
        m_videoWidget->setGeometry(m_videoContainer->rect());
    }
    if (m_annotationCanvas && m_videoContainer) {
        m_annotationCanvas->setGeometry(m_videoContainer->rect());
        m_annotationCanvas->raise();
    }
    updateVideoRect();
    updateAnnotationDisplay();
}

void VideoAnnotationEditor::updateVideoRect()
{
    if (!m_videoWidget || !m_videoContainer) {
        return;
    }

    // Calculate where video is actually displayed within the container
    QSize videoSize = m_videoWidget->videoSize();
    if (videoSize.isEmpty()) {
        m_videoRect = m_videoContainer->rect();
        return;
    }

    QSize containerSize = m_videoContainer->size();
    qreal scaleX = static_cast<qreal>(containerSize.width()) / videoSize.width();
    qreal scaleY = static_cast<qreal>(containerSize.height()) / videoSize.height();
    qreal scale = qMin(scaleX, scaleY);

    int scaledWidth = static_cast<int>(videoSize.width() * scale);
    int scaledHeight = static_cast<int>(videoSize.height() * scale);
    int offsetX = (containerSize.width() - scaledWidth) / 2;
    int offsetY = (containerSize.height() - scaledHeight) / 2;

    m_videoRect = QRect(QPoint(offsetX, offsetY), QSize(scaledWidth, scaledHeight));
}

void VideoAnnotationEditor::updateAnnotationDisplay()
{
    if (m_annotationCanvas) {
        m_annotationCanvas->update();
    } else {
        update();
    }
}

QPointF VideoAnnotationEditor::widgetToRelative(const QPoint &widgetPos) const
{
    if (m_videoRect.isEmpty()) {
        return QPointF(0.5, 0.5);
    }

    QPoint localPos = widgetPos - m_videoRect.topLeft();
    qreal x = static_cast<qreal>(localPos.x()) / m_videoRect.width();
    qreal y = static_cast<qreal>(localPos.y()) / m_videoRect.height();
    return QPointF(qBound(0.0, x, 1.0), qBound(0.0, y, 1.0));
}

QPoint VideoAnnotationEditor::relativeToWidget(const QPointF &relativePos) const
{
    return m_videoRect.topLeft() +
           QPoint(static_cast<int>(relativePos.x() * m_videoRect.width()),
                  static_cast<int>(relativePos.y() * m_videoRect.height()));
}

void VideoAnnotationEditor::beginAnnotation(const QPoint &pos)
{
    m_isDrawing = true;
    m_currentPath.clear();

    QPointF relPos = widgetToRelative(pos);
    m_currentAnnotation = VideoAnnotation();
    m_currentAnnotation.id = VideoAnnotation::generateId();
    m_currentAnnotation.startTimeMs = m_currentTimeMs;
    m_currentAnnotation.endTimeMs = m_currentTimeMs + m_defaultDurationMs;
    m_currentAnnotation.color = m_annotationColor;
    m_currentAnnotation.lineWidth = m_annotationLineWidth;
    m_currentAnnotation.startPoint = relPos;
    m_currentAnnotation.endPoint = relPos;

    // Set type based on current tool
    switch (m_currentTool) {
    case Tool::Arrow:
        m_currentAnnotation.type = VideoAnnotationType::Arrow;
        break;
    case Tool::Line:
        m_currentAnnotation.type = VideoAnnotationType::Line;
        break;
    case Tool::Rectangle:
        m_currentAnnotation.type = VideoAnnotationType::Rectangle;
        break;
    case Tool::Ellipse:
        m_currentAnnotation.type = VideoAnnotationType::Ellipse;
        break;
    case Tool::Pencil:
        m_currentAnnotation.type = VideoAnnotationType::Pencil;
        m_currentPath.append(relPos);
        break;
    case Tool::Marker:
        m_currentAnnotation.type = VideoAnnotationType::Marker;
        m_currentPath.append(relPos);
        break;
    case Tool::Text:
        // Use inline text editor instead of modal dialog
        startTextEditing(pos);
        m_isDrawing = false;
        return;
    case Tool::StepBadge:
        m_currentAnnotation.type = VideoAnnotationType::StepBadge;
        m_currentAnnotation.stepNumber = m_nextStepNumber++;
        break;
    case Tool::Blur:
        m_currentAnnotation.type = VideoAnnotationType::Blur;
        break;
    case Tool::Highlight:
        m_currentAnnotation.type = VideoAnnotationType::Highlight;
        break;
    default:
        m_isDrawing = false;
        return;
    }

    updateAnnotationDisplay();
}

void VideoAnnotationEditor::updateAnnotation(const QPoint &pos)
{
    if (!m_isDrawing) {
        return;
    }

    QPointF relPos = widgetToRelative(pos);

    if (m_currentAnnotation.type == VideoAnnotationType::Pencil ||
        m_currentAnnotation.type == VideoAnnotationType::Marker) {
        m_currentPath.append(relPos);
        m_currentAnnotation.path = m_currentPath;
    } else {
        m_currentAnnotation.endPoint = relPos;
    }

    updateAnnotationDisplay();
}

void VideoAnnotationEditor::finishAnnotation()
{
    if (!m_isDrawing) {
        return;
    }

    m_isDrawing = false;

    // Validate annotation has some size
    bool valid = true;
    if (m_currentAnnotation.type == VideoAnnotationType::Pencil ||
        m_currentAnnotation.type == VideoAnnotationType::Marker) {
        valid = m_currentAnnotation.path.size() >= 2;
    } else if (m_currentAnnotation.type == VideoAnnotationType::StepBadge ||
               m_currentAnnotation.type == VideoAnnotationType::Text) {
        valid = true; // Single point is fine
    } else {
        QPointF delta = m_currentAnnotation.endPoint - m_currentAnnotation.startPoint;
        valid = (qAbs(delta.x()) > 0.01 || qAbs(delta.y()) > 0.01);
    }

    if (valid) {
        m_track->addAnnotation(m_currentAnnotation);
        m_track->setSelectedId(m_currentAnnotation.id);
    }

    m_currentAnnotation = VideoAnnotation();
    m_currentPath.clear();
    updateAnnotationDisplay();
}

void VideoAnnotationEditor::cancelAnnotation()
{
    m_isDrawing = false;
    m_currentAnnotation = VideoAnnotation();
    m_currentPath.clear();
    updateAnnotationDisplay();
}

void VideoAnnotationEditor::startTextEditing(const QPoint &pos)
{
    if (!m_textEditor || !m_videoRect.contains(pos)) {
        return;
    }

    m_textEditor->setColor(m_annotationColor);
    m_textEditor->startEditing(pos, m_videoRect);
}

bool VideoAnnotationEditor::handleTextEditorPress(const QPoint &pos)
{
    if (!m_textEditor || !m_textEditor->isEditing()) {
        return false;
    }

    if (m_textEditor->isConfirmMode()) {
        // In confirm mode: click outside finishes, click inside starts drag
        if (!m_textEditor->contains(pos)) {
            m_textEditor->finishEditing();
            return false; // Allow processing next action
        }
        m_textEditor->handleMousePress(pos);
        return true;
    }

    // In typing mode: click outside finishes
    if (!m_textEditor->contains(pos)) {
        if (!m_textEditor->textEdit()->toPlainText().trimmed().isEmpty()) {
            m_textEditor->finishEditing();
        } else {
            m_textEditor->cancelEditing();
        }
        return false;
    }

    return true; // Click is inside text editor
}

void VideoAnnotationEditor::onTextEditingFinished(const QString &text, const QPoint &position)
{
    if (text.isEmpty() || !m_track) {
        return;
    }

    // Convert widget position to relative coordinates
    QPointF relPos = widgetToRelative(position);

    // Create the video annotation
    VideoAnnotation annotation;
    annotation.id = VideoAnnotation::generateId();
    annotation.type = VideoAnnotationType::Text;
    annotation.startTimeMs = m_currentTimeMs;
    annotation.endTimeMs = m_currentTimeMs + m_defaultDurationMs;
    annotation.startPoint = relPos;
    annotation.endPoint = relPos;
    annotation.text = text;
    annotation.color = m_annotationColor;

    m_track->addAnnotation(annotation);
    m_track->setSelectedId(annotation.id);
    updateAnnotationDisplay();
}

void VideoAnnotationEditor::onTextEditingCancelled()
{
    updateAnnotationDisplay();
}
