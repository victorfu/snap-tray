#include "RecordingControlBar.h"
#include "GlassRenderer.h"
#include "GlassPanelCSS.h"
#include "ToolbarStyle.h"
#include "IconRenderer.h"
#include "ui/GlassTooltip.h"
#include "cursor/CursorManager.h"
#include "platform/WindowLevel.h"

#include <QLabel>
#include <QHBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QConicalGradient>
#include <QtMath>

namespace {
QColor dimmedColor(const QColor &color)
{
    QColor dimmed = color;
    dimmed.setAlpha((color.alpha() * 50) / 100);
    return dimmed;
}

// Recording control bar layout constants
}

RecordingControlBar::RecordingControlBar(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_audioIndicator(nullptr)
    , m_sizeLabel(nullptr)
    , m_fpsLabel(nullptr)
    , m_layout(nullptr)
    , m_audioEnabled(false)
    , m_hoveredButton(ButtonNone)
    , m_isDragging(false)
    , m_gradientOffset(0.0)
    , m_isPaused(false)
    , m_isPreparing(false)
    , m_tooltip(nullptr)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMouseTracking(true);
    setFixedHeight(TOOLBAR_HEIGHT + SHADOW_MARGIN);

    setupUi();

    m_blinkTimer = new QTimer(this);
    connect(m_blinkTimer, &QTimer::timeout, this, [this]() {
        m_gradientOffset += 0.02;
        if (m_gradientOffset > 1.0)
            m_gradientOffset -= 1.0;
        updateIndicatorGradient();
    });
    m_blinkTimer->start(16);

    m_escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    m_escShortcut->setContext(Qt::ApplicationShortcut);
    connect(m_escShortcut, &QShortcut::activated, this, &RecordingControlBar::stopRequested);
}

RecordingControlBar::~RecordingControlBar()
{
    // Clean up per-widget cursor state before destruction (ensures Drag context is cleared)
    CursorManager::instance().clearAllForWidget(this);

    if (m_tooltip) {
        m_tooltip->deleteLater();
        m_tooltip = nullptr;
    }
}

void RecordingControlBar::setupUi()
{
    // Load icons for buttons
    IconRenderer& iconRenderer = IconRenderer::instance();
    iconRenderer.loadIcon("pause", ":/icons/icons/pause.svg");
    iconRenderer.loadIcon("play", ":/icons/icons/play.svg");
    iconRenderer.loadIcon("stop", ":/icons/icons/stop.svg");
    iconRenderer.loadIcon("cancel", ":/icons/icons/cancel.svg");

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(10 + SHADOW_MARGIN_X, 4, 10 + SHADOW_MARGIN_X, 4);
    m_layout->setSpacing(6);

    // Recording indicator (animated gradient circle)
    m_recordingIndicator = new QLabel(this);
    m_recordingIndicator->setFixedSize(12, 12);
    updateIndicatorGradient();
    m_layout->addWidget(m_recordingIndicator);

    // Audio indicator (microphone icon, hidden by default)
    m_audioIndicator = new QLabel(this);
    m_audioIndicator->setFixedSize(12, 12);
    m_audioIndicator->setToolTip(tr("Audio recording enabled"));
    m_audioIndicator->hide();
    m_layout->addWidget(m_audioIndicator);

    // Get theme colors for labels
    ToolbarStyleConfig styleConfig = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());

    using namespace SnapTray::GlassPanelCSS;

    // Duration label
    m_durationLabel = new QLabel("00:00:00", this);
    m_durationLabel->setStyleSheet(monoLabelStylesheet(styleConfig.textColor, 11, true));
    m_durationLabel->setMinimumWidth(55);
    m_layout->addWidget(m_durationLabel);

    // Separator
    m_separator1 = new QLabel("|", this);
    m_separator1->setStyleSheet(separatorStylesheet(styleConfig.separatorColor));
    m_layout->addWidget(m_separator1);

    // Size label
    m_sizeLabel = new QLabel("--", this);
    m_sizeLabel->setStyleSheet(monoLabelStylesheet(styleConfig.textColor, 10));
    m_sizeLabel->setMinimumWidth(70);
    m_layout->addWidget(m_sizeLabel);

    // Separator
    m_separator2 = new QLabel("|", this);
    m_separator2->setStyleSheet(separatorStylesheet(styleConfig.separatorColor));
    m_layout->addWidget(m_separator2);

    // FPS label
    m_fpsLabel = new QLabel(tr("-- fps"), this);
    m_fpsLabel->setStyleSheet(monoLabelStylesheet(styleConfig.textColor, 10));
    m_fpsLabel->setMinimumWidth(45);
    m_layout->addWidget(m_fpsLabel);

    // Add stretch to push buttons to the right
    m_layout->addStretch();

    m_buttonSpacer = new QWidget(this);
    m_buttonSpacer->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_layout->addWidget(m_buttonSpacer);
    updateButtonSpacerWidth();

    updateButtonRects();
    updateFixedWidth();
}

void RecordingControlBar::updateButtonRects()
{
    const QRect bgRect = backgroundRect();
    if (bgRect.width() <= 0) {
        m_pauseRect = QRect();
        m_stopRect = QRect();
        m_cancelRect = QRect();
        return;
    }

    int rightMargin = 10;
    // Use square button size for proper icon rendering
    int buttonSize = BUTTON_WIDTH - 4;  // 24x24 square buttons
    int y = bgRect.top() + (bgRect.height() - buttonSize) / 2;
    int bgRight = bgRect.left() + bgRect.width();

    // Buttons from right to left: cancel, stop, pause
    int x = bgRight - rightMargin - buttonSize;
    m_cancelRect = QRect(x, y, buttonSize, buttonSize);

    x -= buttonSize + BUTTON_SPACING;
    m_stopRect = QRect(x, y, buttonSize, buttonSize);

    x -= buttonSize + BUTTON_SPACING;
    m_pauseRect = QRect(x, y, buttonSize, buttonSize);
}

QRect RecordingControlBar::backgroundRect() const
{
    int inset = SHADOW_MARGIN_X;
    int contentWidth = width() - inset * 2;
    if (contentWidth < 0) {
        contentWidth = 0;
    }
    return QRect(inset, 0, contentWidth, TOOLBAR_HEIGHT);
}

void RecordingControlBar::updateFixedWidth()
{
    if (!m_layout) {
        return;
    }

    m_layout->invalidate();
    int targetWidth = qMax(m_layout->minimumSize().width(), m_layout->sizeHint().width());

    if (targetWidth <= 0) {
        return;
    }
    if (width() == targetWidth) {
        updateButtonRects();
        return;
    }

    applyFixedWidth(targetWidth);
}

void RecordingControlBar::applyFixedWidth(int targetWidth)
{
    if (targetWidth <= 0 || width() == targetWidth) {
        return;
    }

    QPoint center = geometry().center();
    setFixedWidth(targetWidth);

    if (isVisible() && !m_isDragging) {
        move(center.x() - targetWidth / 2, pos().y());
    }

    updateButtonRects();
}

void RecordingControlBar::updateButtonSpacerWidth()
{
    if (!m_buttonSpacer) {
        return;
    }

    // Base: 3 control buttons (pause, stop, cancel)
    int baseButtonsWidth = 3 * BUTTON_WIDTH + 2 * BUTTON_SPACING + 4;

    m_buttonSpacer->setFixedWidth(baseButtonsWidth);
}

QRect RecordingControlBar::anchorRectForButton(int button) const
{
    switch (button) {
    case ButtonPause: return m_pauseRect;
    case ButtonStop: return m_stopRect;
    case ButtonCancel: return m_cancelRect;
    default: return QRect();
    }
}

void RecordingControlBar::showTooltipForButton(int buttonId)
{
    QString tip = tooltipForButton(buttonId);
    if (tip.isEmpty()) {
        hideTooltip();
        return;
    }

    QRect anchorRect = anchorRectForButton(buttonId);
    if (anchorRect.isNull()) {
        hideTooltip();
        return;
    }

    showTooltip(tip, anchorRect);
}

void RecordingControlBar::showTooltip(const QString &text, const QRect &anchorRect)
{
    if (!m_tooltip) {
        m_tooltip = new SnapTray::GlassTooltip(nullptr);
        // Exclude tooltip from screen capture to prevent it from appearing in recordings
        setWindowExcludedFromCapture(m_tooltip, true);
        connect(m_tooltip, &QObject::destroyed, this, [this]() {
            m_tooltip = nullptr;
        });
    }

    const ToolbarStyleConfig& style = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    const QPoint globalAnchorCenter = mapToGlobal(anchorRect.center());

    // Show tooltip below to avoid being captured in recording
    const QPoint anchorEdge(globalAnchorCenter.x(),
        mapToGlobal(anchorRect.bottomLeft()).y());

    m_tooltip->show(text, style, anchorEdge, /*above=*/false, /*showShadow=*/true);

    // Fallback: if tooltip goes off screen, flip to above
    QScreen *screen = QGuiApplication::screenAt(globalAnchorCenter);
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect bounds = screen->availableGeometry();
        const QRect tipGeom(m_tooltip->pos(), m_tooltip->size());
        if (tipGeom.bottom() > bounds.bottom() - 5) {
            const QPoint flippedEdge(globalAnchorCenter.x(),
                mapToGlobal(anchorRect.topLeft()).y());
            m_tooltip->show(text, style, flippedEdge, /*above=*/true, /*showShadow=*/true);
        }
        // Final safety clamp
        const QRect finalGeom(m_tooltip->pos(), m_tooltip->size());
        if (finalGeom.bottom() > bounds.bottom() - 5) {
            m_tooltip->move(m_tooltip->x(), bounds.bottom() - m_tooltip->height() - 5);
        }
    }
}

void RecordingControlBar::hideTooltip()
{
    if (m_tooltip) {
        m_tooltip->hideTooltip();
    }
}

void RecordingControlBar::updateIndicatorGradient()
{
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    if (m_isPreparing) {
        int alpha = 128 + static_cast<int>(64 * qSin(m_gradientOffset * 2 * M_PI));
        painter.setBrush(QColor(180, 180, 180, alpha));
    } else if (m_isPaused) {
        painter.setBrush(QColor(255, 184, 0));
    } else {
        QConicalGradient gradient(6, 6, 360 * m_gradientOffset);
        gradient.setColorAt(0.0, QColor(0, 122, 255));
        gradient.setColorAt(0.33, QColor(88, 86, 214));
        gradient.setColorAt(0.66, QColor(175, 82, 222));
        gradient.setColorAt(1.0, QColor(0, 122, 255));
        painter.setBrush(gradient);
    }

    painter.setPen(Qt::NoPen);
    painter.drawEllipse(0, 0, 12, 12);
    painter.end();

    m_recordingIndicator->setPixmap(pixmap);
}

void RecordingControlBar::setPaused(bool paused)
{
    m_isPaused = paused;
    updateIndicatorGradient();
    update();
}

void RecordingControlBar::setPreparing(bool preparing)
{
    m_isPreparing = preparing;
    updateIndicatorGradient();

    // Get theme-aware colors
    ToolbarStyleConfig config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());

    if (preparing) {
        m_durationLabel->setText(tr("Preparing..."));
        // Use faded text color for preparing state
        QColor fadedColor = config.textColor;
        fadedColor.setAlpha(180);  // ~70% opacity
        m_durationLabel->setStyleSheet(
            SnapTray::GlassPanelCSS::monoLabelStylesheet(fadedColor, 11, true));
    } else {
        m_durationLabel->setText("00:00:00");
        m_durationLabel->setStyleSheet(
            SnapTray::GlassPanelCSS::monoLabelStylesheet(config.textColor, 11, true));
    }

    // Note: Don't call adjustSize() here as it would override setFixedWidth()
    // set by setAnnotationEnabled()
    updateFixedWidth();
    update();
}

void RecordingControlBar::setPreparingStatus(const QString &status)
{
    if (m_isPreparing && m_durationLabel) {
        m_durationLabel->setText(status);
        updateFixedWidth();
    }
}

void RecordingControlBar::updateRegionSize(int width, int height)
{
    if (m_sizeLabel) {
        m_sizeLabel->setText(QString("%1×%2").arg(width).arg(height));
    }
}

void RecordingControlBar::updateFps(double fps)
{
    if (m_fpsLabel) {
        m_fpsLabel->setText(tr("%1 fps").arg(qRound(fps)));
    }
}

void RecordingControlBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    ToolbarStyleConfig config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    // Background rect excludes shadow margin at the bottom
    QRect bgRect = backgroundRect();

    GlassRenderer::drawGlassPanel(painter, bgRect, config, config.cornerRadius);

    updateButtonRects();
    drawButtons(painter);
}

void RecordingControlBar::drawButtons(QPainter &painter)
{
    ToolbarStyleConfig config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    const bool controlsEnabled = !m_isPreparing;

    // Draw pause/resume button
    {
        bool isHovered = controlsEnabled && (m_hoveredButton == ButtonPause);
        if (isHovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(config.hoverBackgroundColor);
            painter.drawRoundedRect(m_pauseRect.adjusted(2, 2, -2, -2), 6, 6);
        }
        QString iconKey = m_isPaused ? "play" : "pause";
        QColor iconColor = isHovered ? config.iconActiveColor : config.iconNormalColor;
        if (!controlsEnabled) {
            iconColor = dimmedColor(iconColor);
        }
        IconRenderer::instance().renderIcon(painter, m_pauseRect, iconKey, iconColor);
    }

    // Draw stop button
    {
        bool isHovered = controlsEnabled && (m_hoveredButton == ButtonStop);
        if (isHovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(config.hoverBackgroundColor);
            painter.drawRoundedRect(m_stopRect.adjusted(2, 2, -2, -2), 6, 6);
        }
        QColor iconColor = config.iconRecordColor;
        if (!controlsEnabled) {
            iconColor = dimmedColor(iconColor);
        }
        IconRenderer::instance().renderIcon(painter, m_stopRect, "stop", iconColor);
    }

    // Draw cancel button
    {
        bool isHovered = (m_hoveredButton == ButtonCancel);
        if (isHovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(config.hoverBackgroundColor);
            painter.drawRoundedRect(m_cancelRect.adjusted(2, 2, -2, -2), 6, 6);
        }
        QColor iconColor = isHovered ? config.iconCancelColor : config.iconNormalColor;
        IconRenderer::instance().renderIcon(painter, m_cancelRect, "cancel", iconColor);
    }
}

QString RecordingControlBar::tooltipForButton(int button) const
{
    switch (button) {
    case ButtonPause:
        return m_isPaused ? tr("Resume Recording") : tr("Pause Recording");
    case ButtonStop:
        return tr("Stop Recording");
    case ButtonCancel:
        return tr("Cancel Recording (Esc)");
    default:
        return QString();
    }
}

int RecordingControlBar::buttonAtPosition(const QPoint &pos) const
{
    // Ensure button rects are up-to-date (they're calculated based on current widget size)
    const_cast<RecordingControlBar *>(this)->updateButtonRects();

    if (!m_isPreparing && m_pauseRect.contains(pos)) return ButtonPause;
    if (!m_isPreparing && m_stopRect.contains(pos)) return ButtonStop;
    if (m_cancelRect.contains(pos)) return ButtonCancel;

    return ButtonNone;
}

void RecordingControlBar::positionNear(const QRect &recordingRegion)
{
    QScreen *screen = QGuiApplication::screenAt(recordingRegion.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    QRect screenGeom = screen->geometry();
    int x, y;

    bool isFullScreen = (recordingRegion == screenGeom);

    // Margin between toolbar and recording region boundary
    // 8px visual gap + 4px border width + 3px glow effect = 15px
    const int kToolbarMargin = 15;

    if (isFullScreen) {
        x = screenGeom.center().x() - width() / 2;
        y = screenGeom.bottom() - height() - 60;
    } else {
        x = recordingRegion.center().x() - width() / 2;
        y = recordingRegion.bottom() + kToolbarMargin;

        if (y + height() > screenGeom.bottom() - 10) {
            y = recordingRegion.top() - height() - kToolbarMargin;
        }

        if (y < screenGeom.top() + 10) {
            y = recordingRegion.top() + 10;
        }
    }

    x = qBound(screenGeom.left() + 10, x, screenGeom.right() - width() - 10);

    move(x, y);
}

void RecordingControlBar::updateDuration(qint64 elapsedMs)
{
    m_durationLabel->setText(formatDuration(elapsedMs));
}

QString RecordingControlBar::formatDuration(qint64 ms) const
{
    int hours = ms / 3600000;
    int minutes = (ms % 3600000) / 60000;
    int seconds = (ms % 60000) / 1000;

    return QString("%1:%2:%3")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

void RecordingControlBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        hideTooltip();
        int button = buttonAtPosition(event->pos());
        if (button != ButtonNone) {
            // Keep toolbar cursor behavior consistent: all interactive toolbar areas use ArrowCursor.
            CursorManager::instance().pushCursorForWidget(this, CursorContext::Hover, Qt::ArrowCursor);
            switch (button) {
            case ButtonPause:
                if (m_isPaused) {
                    emit resumeRequested();
                } else {
                    emit pauseRequested();
                }
                break;
            case ButtonStop:
                emit stopRequested();
                break;
            case ButtonCancel:
                emit cancelRequested();
                break;
            }
            return;
        }

        // Start dragging if not on a button
        m_isDragging = true;
        m_dragStartPos = event->globalPosition().toPoint();
        m_dragStartWidgetPos = pos();
        CursorManager::instance().pushCursorForWidget(this, CursorContext::Drag, Qt::ClosedHandCursor);
    }
    QWidget::mousePressEvent(event);
}

void RecordingControlBar::enterEvent(QEnterEvent *event)
{
    if (!m_isDragging) {
        m_hoveredButton = buttonAtPosition(event->position().toPoint());
        CursorManager::instance().pushCursorForWidget(this, CursorContext::Hover, Qt::ArrowCursor);

        if (m_hoveredButton != ButtonNone) {
            showTooltipForButton(m_hoveredButton);
        } else {
            hideTooltip();
        }
        update();
    }

    QWidget::enterEvent(event);
}

void RecordingControlBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging) {
        QPoint delta = event->globalPosition().toPoint() - m_dragStartPos;
        QPoint newPos = m_dragStartWidgetPos + delta;

        QScreen *screen = QGuiApplication::screenAt(event->globalPosition().toPoint());
        if (screen) {
            QRect screenGeom = screen->geometry();
            newPos.setX(qBound(screenGeom.left(), newPos.x(), screenGeom.right() - width()));
            newPos.setY(qBound(screenGeom.top(), newPos.y(), screenGeom.bottom() - height()));
        }

        move(newPos);
        hideTooltip();
    } else {
        // Keep ArrowCursor for the whole toolbar surface (not just icon hitboxes).
        auto& cm = CursorManager::instance();
        cm.pushCursorForWidget(this, CursorContext::Hover, Qt::ArrowCursor);

        int newHovered = buttonAtPosition(event->pos());
        if (newHovered != m_hoveredButton) {
            m_hoveredButton = newHovered;
            update();
        }
        if (m_hoveredButton != ButtonNone) {
            showTooltipForButton(m_hoveredButton);
        } else {
            hideTooltip();
        }
    }
    QWidget::mouseMoveEvent(event);
}

void RecordingControlBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_isDragging) {
            m_isDragging = false;
            CursorManager::instance().popCursorForWidget(this, CursorContext::Drag);

            auto& cm = CursorManager::instance();
            if (rect().contains(event->pos())) {
                cm.pushCursorForWidget(this, CursorContext::Hover, Qt::ArrowCursor);
            } else {
                cm.popCursorForWidget(this, CursorContext::Hover);
            }
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void RecordingControlBar::leaveEvent(QEvent *event)
{
    if (m_hoveredButton != ButtonNone) {
        m_hoveredButton = ButtonNone;
        update();
    }
    CursorManager::instance().popCursorForWidget(this, CursorContext::Hover);
    hideTooltip();
    QWidget::leaveEvent(event);
}

void RecordingControlBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        emit stopRequested();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void RecordingControlBar::setAudioEnabled(bool enabled)
{
    m_audioEnabled = enabled;

    if (enabled) {
        QPixmap micIcon(12, 12);
        micIcon.fill(Qt::transparent);

        QPainter painter(&micIcon);
        painter.setRenderHint(QPainter::Antialiasing);

        QPen pen(QColor(52, 199, 89));
        pen.setWidth(1);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);

        painter.drawEllipse(3, 1, 6, 7);
        painter.drawLine(6, 8, 6, 10);
        painter.drawArc(2, 6, 8, 5, 0, -180 * 16);

        m_audioIndicator->setPixmap(micIcon);
        m_audioIndicator->show();
    } else {
        m_audioIndicator->hide();
    }

    updateFixedWidth();
}