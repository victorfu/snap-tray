#include "video/VideoAnnotationEditor.h"
#include "video/AnnotationTimelineWidget.h"
#include "video/TrimTimeline.h"
#include "video/VideoPlaybackWidget.h"
#include "GlassRenderer.h"
#include "ToolbarStyle.h"
#include <QBoxLayout>
#include <QColorDialog>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QShowEvent>
#include <QSlider>
#include <QTimer>
#include <QToolButton>

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

    auto addToolButton = [this, toolbarLayout](const QString &text, const QString &tooltip,
                                               Tool tool) {
        QToolButton *btn = new QToolButton(m_toolbarWidget);
        btn->setText(text);
        btn->setToolTip(tooltip);
        btn->setCheckable(true);
        btn->setMinimumWidth(32);
        connect(btn, &QToolButton::clicked, this, [this, btn, tool]() {
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

    QToolButton *selectBtn = addToolButton("↖", "Select", Tool::Select);
    selectBtn->setChecked(true);
    addToolButton("→", "Arrow", Tool::Arrow);
    addToolButton("—", "Line", Tool::Line);
    addToolButton("□", "Rectangle", Tool::Rectangle);
    addToolButton("○", "Ellipse", Tool::Ellipse);
    addToolButton("✎", "Pencil", Tool::Pencil);
    addToolButton("▬", "Marker", Tool::Marker);
    addToolButton("T", "Text", Tool::Text);
    addToolButton("①", "Step Badge", Tool::StepBadge);
    addToolButton("▒", "Blur", Tool::Blur);
    addToolButton("▓", "Highlight", Tool::Highlight);

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
    m_undoButton = new QPushButton("↶", m_toolbarWidget);
    m_undoButton->setFixedWidth(32);
    m_undoButton->setToolTip("Undo");
    connect(m_undoButton, &QPushButton::clicked, m_track, &AnnotationTrack::undo);
    toolbarLayout->addWidget(m_undoButton);

    m_redoButton = new QPushButton("↷", m_toolbarWidget);
    m_redoButton->setFixedWidth(32);
    m_redoButton->setToolTip("Redo");
    connect(m_redoButton, &QPushButton::clicked, m_track, &AnnotationTrack::redo);
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

void VideoAnnotationEditor::setVideoPlayer(IVideoPlayer *player)
{
    m_videoPlayer = player;
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

    QString actionStyle = actionButtonStyle(config);
    if (m_undoButton) {
        m_undoButton->setStyleSheet(actionStyle);
    }
    if (m_redoButton) {
        m_redoButton->setStyleSheet(actionStyle);
    }

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
    if (m_isDrawing) {
        updateAnnotation(pos);
    }
}

void VideoAnnotationEditor::handleCanvasMouseRelease(const QPoint &pos)
{
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
    if (m_ownsTrack && m_track) {
        delete m_track;
    }

    if (m_track) {
        disconnect(m_track, nullptr, this, nullptr);
    }

    m_track = track;
    m_ownsTrack = false;

    if (m_annotationTimeline) {
        m_annotationTimeline->setTrack(track);
    }

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
    Q_UNUSED(outputPath)
    Q_UNUSED(format)
    Q_UNUSED(progressCallback)
    // TODO: Implement export pipeline
    return false;
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
        m_currentAnnotation.type = VideoAnnotationType::Text;
        m_currentAnnotation.text = "Text"; // TODO: Show text input dialog
        break;
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
