#include "RecordingControlBar.h"
#include "GlassRenderer.h"
#include "ToolbarStyle.h"
#include "IconRenderer.h"
#include "cursor/CursorManager.h"
#include "platform/WindowLevel.h"

#include <QLabel>
#include <QHBoxLayout>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QConicalGradient>
#include <QLinearGradient>
#include <QtMath>

namespace {
QColor dimmedColor(const QColor &color)
{
    QColor dimmed = color;
    dimmed.setAlpha((color.alpha() * 50) / 100);
    return dimmed;
}

// Recording control bar layout constants
constexpr int kEffectsToolbarGap = 6;
constexpr int kEffectsButtonPaddingX = 8;
constexpr int kEffectsButtonSpacing = 4;
constexpr int kEffectsToolbarPaddingX = 4;
constexpr int kEffectsToolbarPaddingY = 2;
}

class GlassTooltipWidget : public QWidget
{
public:
    explicit GlassTooltipWidget(QWidget *parent = nullptr)
        : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setText(const QString &text, const ToolbarStyleConfig &style)
    {
        m_text = text;
        m_style = style;
        QFont font = this->font();
        font.setPointSize(11);
        setFont(font);
        updateGeometry();
        update();
    }

    QSize sizeHint() const override
    {
        if (m_text.isEmpty()) {
            return QSize(0, 0);
        }
        QFontMetrics fm(font());
        QRect textRect = fm.boundingRect(m_text);
        textRect.adjust(-8, -4, 8, 4);
        return textRect.size();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        if (m_text.isEmpty()) {
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        ToolbarStyleConfig config = m_style;
        config.shadowOffsetY = 2;
        config.shadowBlurRadius = 6;
        GlassRenderer::drawGlassPanel(painter, rect(), config, 6);

        painter.setPen(config.tooltipText);
        painter.drawText(rect(), Qt::AlignCenter, m_text);
    }

private:
    QString m_text;
    ToolbarStyleConfig m_style;
};

RecordingControlBar::RecordingControlBar(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_audioIndicator(nullptr)
    , m_sizeLabel(nullptr)
    , m_fpsLabel(nullptr)
    , m_layout(nullptr)
    , m_audioEnabled(false)
    , m_hoveredButton(ButtonNone)
    , m_mode(Mode::Recording)
    , m_previewDuration(0)
    , m_previewPosition(0)
    , m_isPlaying(false)
    , m_volume(1.0f)
    , m_isMuted(false)
    , m_selectedFormat(OutputFormat::MP4)
    , m_isScrubbing(false)
    , m_scrubPosition(0)
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

    // Load preview mode icons
    iconRenderer.loadIcon("save", ":/icons/icons/save.svg");
    iconRenderer.loadIcon("volume", ":/icons/icons/volume.svg");
    iconRenderer.loadIcon("mute", ":/icons/icons/mute.svg");

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
    m_audioIndicator->setToolTip("Audio recording enabled");
    m_audioIndicator->hide();
    m_layout->addWidget(m_audioIndicator);

    // Get theme colors for labels
    ToolbarStyleConfig styleConfig = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    QString textColor = styleConfig.textColor.name();
    QString separatorColor = styleConfig.separatorColor.name();

    // Duration label
    m_durationLabel = new QLabel("00:00:00", this);
    m_durationLabel->setStyleSheet(
        QString("color: %1; font-size: 11px; font-weight: 600; font-family: 'SF Mono', Menlo, Monaco, monospace;").arg(textColor));
    m_durationLabel->setMinimumWidth(55);
    m_layout->addWidget(m_durationLabel);

    // Separator
    m_separator1 = new QLabel("|", this);
    m_separator1->setStyleSheet(QString("color: %1; font-size: 11px;").arg(separatorColor));
    m_layout->addWidget(m_separator1);

    // Size label
    m_sizeLabel = new QLabel("--", this);
    m_sizeLabel->setStyleSheet(
        QString("color: %1; font-size: 10px; font-family: 'SF Mono', Menlo, Monaco, monospace;").arg(textColor));
    m_sizeLabel->setMinimumWidth(70);
    m_layout->addWidget(m_sizeLabel);

    // Separator
    m_separator2 = new QLabel("|", this);
    m_separator2->setStyleSheet(QString("color: %1; font-size: 11px;").arg(separatorColor));
    m_layout->addWidget(m_separator2);

    // FPS label
    m_fpsLabel = new QLabel("-- fps", this);
    m_fpsLabel->setStyleSheet(
        QString("color: %1; font-size: 10px; font-family: 'SF Mono', Menlo, Monaco, monospace;").arg(textColor));
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
        m_effectsButtonRect = QRect();
        m_effectsToolbarRect = QRect();
        m_effectsToolbarItems.clear();
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

    // Effects button (FX) - to the left of pause button
    x -= buttonSize + BUTTON_SPACING;
    m_effectsButtonRect = QRect(x, y, buttonSize, buttonSize);

    // Effects toolbar (appears to the left of the FX button when visible)
    if (m_effectsToolbarVisible) {
        QFont font = effectsToolbarFont();
        QFontMetrics metrics(font);

        // Calculate toolbar items
        m_effectsToolbarItems.clear();
        const auto &items = effectsToolbarItems();
        int toolbarWidth = effectsToolbarButtonsWidth(metrics) + 16;  // padding
        int toolbarHeight = buttonSize;  // Same height as buttons

        // Position to the left of the effects button
        int toolbarX = m_effectsButtonRect.left() - toolbarWidth - BUTTON_SPACING;
        int toolbarY = y;

        // Keep toolbar within widget bounds
        int bgLeft = backgroundRect().left();
        if (toolbarX < bgLeft + 4) {
            toolbarX = bgLeft + 4;
        }

        m_effectsToolbarRect = QRect(toolbarX, toolbarY, toolbarWidth, toolbarHeight);

        // Calculate individual button rects within toolbar
        int itemX = toolbarX + 8;
        int itemY = toolbarY + (toolbarHeight - 20) / 2;
        for (const auto &item : items) {
            int itemWidth = effectsToolbarButtonWidth(item, metrics);
            m_effectsToolbarItems.append({QRect(itemX, itemY, itemWidth, 20), item});
            itemX += itemWidth + 4;
        }
    } else {
        m_effectsToolbarItems.clear();
        m_effectsToolbarRect = QRect();
    }
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

int RecordingControlBar::previewWidth() const
{
    int leftMargin = 10;
    int rightMargin = 10;
    int buttonSize = 24;
    int formatSpacing = 4;
    int webpWidth = 42;
    int gifWidth = 32;
    int mp4Width = 38;
    int annotateWidth = annotateButtonWidth();
    int indicatorSize = 12;
    int durationWidth = 90;
    int timelineMinWidth = 50;

    int leftBlock = leftMargin + indicatorSize + 6 + durationWidth + 8 + timelineMinWidth;
    int rightBlock = rightMargin + buttonSize + BUTTON_SPACING + buttonSize +
        BUTTON_SPACING + annotateWidth +
        SEPARATOR_MARGIN + webpWidth + formatSpacing + gifWidth + formatSpacing + mp4Width +
        SEPARATOR_MARGIN + buttonSize + BUTTON_SPACING + buttonSize;

    int minBackgroundWidth = leftBlock + rightBlock;
    int minWidgetWidth = minBackgroundWidth + SHADOW_MARGIN_X * 2;
    return qMax(minWidgetWidth, 500);
}

int RecordingControlBar::annotateButtonWidth() const
{
    QFont font = effectsToolbarFont();
    QFontMetrics metrics(font);
    return metrics.horizontalAdvance(tr("Annotate")) + 16;
}

void RecordingControlBar::updateFixedWidth()
{
    int targetWidth = 0;
    if (m_mode == Mode::Preview) {
        targetWidth = previewWidth();
    } else if (m_layout) {
        m_layout->invalidate();
        targetWidth = qMax(m_layout->minimumSize().width(), m_layout->sizeHint().width());
    }

    if (targetWidth <= 0) {
        return;
    }
    if (width() == targetWidth) {
        if (m_mode == Mode::Preview) {
            updatePreviewButtonRects();
        } else {
            updateButtonRects();
        }
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

    if (m_mode == Mode::Preview) {
        updatePreviewButtonRects();
    } else {
        updateButtonRects();
    }
}

void RecordingControlBar::updateButtonSpacerWidth()
{
    if (!m_buttonSpacer) {
        return;
    }

    // Base: 3 control buttons (pause, stop, cancel)
    int baseButtonsWidth = 3 * BUTTON_WIDTH + 2 * BUTTON_SPACING + 4;

    // Effects button (FX)
    int effectsWidth = BUTTON_WIDTH + BUTTON_SPACING;

    // Effects toolbar (when visible)
    int toolbarWidth = 0;
    if (m_effectsToolbarVisible) {
        toolbarWidth = effectsToolbarWidth() + BUTTON_SPACING;
    }

    int totalWidth = baseButtonsWidth + effectsWidth + toolbarWidth;
    m_buttonSpacer->setFixedWidth(totalWidth);
}

QRect RecordingControlBar::anchorRectForButton(int button) const
{
    if (button == ButtonEffectLaser || button == ButtonEffectCursorHighlight ||
        button == ButtonEffectSpotlight || button == ButtonEffectClickRipple ||
        button == ButtonEffectComposite) {
        for (const auto &entry : m_effectsToolbarItems) {
            if (effectsToolbarButtonId(entry.second) == button) {
                return entry.first;
            }
        }
    }

    switch (button) {
    case ButtonPause: return m_pauseRect;
    case ButtonStop: return m_stopRect;
    case ButtonCancel: return m_cancelRect;
    case ButtonEffects: return m_effectsButtonRect;
    case ButtonPlayPause: return m_playPauseRect;
    case ButtonVolume: return m_volumeRect;
    case ButtonFormatMP4: return m_formatMP4Rect;
    case ButtonFormatGIF: return m_formatGIFRect;
    case ButtonFormatWebP: return m_formatWebPRect;
    case ButtonAnnotate: return m_annotateRect;
    case ButtonSave: return m_saveRect;
    case ButtonDiscard: return m_discardRect;
    case AreaTimeline: return m_timelineRect;
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
        m_tooltip = new GlassTooltipWidget(nullptr);
        // Exclude tooltip from screen capture to prevent it from appearing in recordings
        setWindowExcludedFromCapture(m_tooltip, true);
        connect(m_tooltip, &QObject::destroyed, this, [this]() {
            m_tooltip = nullptr;
        });
    }

    ToolbarStyleConfig style = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    m_tooltip->setText(text, style);
    QSize size = m_tooltip->sizeHint();
    m_tooltip->resize(size);

    QPoint anchorCenter = anchorRect.center();
    QPoint globalAnchorCenter = mapToGlobal(anchorCenter);

    int x = globalAnchorCenter.x() - size.width() / 2;
    int y;

    // In Recording mode, show tooltip below to avoid being captured in recording
    // In Preview mode, show tooltip above (default behavior)
    if (m_mode == Mode::Recording) {
        y = mapToGlobal(anchorRect.bottomLeft()).y() + 6;
    } else {
        y = mapToGlobal(anchorRect.topLeft()).y() - size.height() - 6;
    }

    QScreen *screen = QGuiApplication::screenAt(globalAnchorCenter);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    if (screen) {
        QRect bounds = screen->availableGeometry();
        x = qBound(bounds.left() + 5, x, bounds.right() - size.width() - 5);
        // Fallback: if tooltip goes off screen, flip direction
        if (m_mode == Mode::Recording) {
            if (y + size.height() > bounds.bottom() - 5) {
                y = mapToGlobal(anchorRect.topLeft()).y() - size.height() - 6;
            }
        } else {
            if (y < bounds.top() + 5) {
                y = mapToGlobal(anchorRect.bottomLeft()).y() + 6;
            }
        }
        if (y + size.height() > bounds.bottom() - 5) {
            y = bounds.bottom() - size.height() - 5;
        }
    }

    m_tooltip->move(x, y);
    m_tooltip->show();
}

void RecordingControlBar::hideTooltip()
{
    if (m_tooltip) {
        m_tooltip->hide();
    }
}

void RecordingControlBar::updateIndicatorGradient()
{
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    if (m_mode == Mode::Preview) {
        // Preview mode indicator
        if (m_isPlaying) {
            // Pulsing green when playing
            int alpha = 180 + static_cast<int>(75 * qSin(m_gradientOffset * 2 * M_PI));
            painter.setBrush(QColor(52, 199, 89, alpha));  // Green
        } else {
            // Static amber when paused
            painter.setBrush(QColor(255, 184, 0));  // Amber
        }
    } else if (m_isPreparing) {
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
    QString textColor = config.textColor.name();

    if (preparing) {
        m_durationLabel->setText(tr("Preparing..."));
        // Use faded text color for preparing state
        QColor fadedColor = config.textColor;
        fadedColor.setAlpha(180);  // ~70% opacity
        m_durationLabel->setStyleSheet(
            QString("color: %1; font-size: 11px; font-weight: 600; font-family: 'SF Mono', Menlo, Monaco, monospace;")
                .arg(fadedColor.name(QColor::HexArgb)));
    } else {
        m_durationLabel->setText("00:00:00");
        m_durationLabel->setStyleSheet(
            QString("color: %1; font-size: 11px; font-weight: 600; font-family: 'SF Mono', Menlo, Monaco, monospace;")
                .arg(textColor));
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
        m_fpsLabel->setText(QString("%1 fps").arg(qRound(fps)));
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

    if (m_mode == Mode::Preview) {
        updatePreviewButtonRects();
        drawPreviewModeUI(painter);
    } else {
        updateButtonRects();
        drawButtons(painter);

        // Draw effects button (always visible in recording mode)
        drawEffectsButton(painter);

        // Draw effects sub-toolbar if visible
        drawEffectsToolbar(painter);
    }

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
    // Recording mode buttons
    case ButtonPause:
        return m_isPaused ? tr("Resume Recording") : tr("Pause Recording");
    case ButtonStop:
        return tr("Stop Recording");
    case ButtonCancel:
        return tr("Cancel Recording (Esc)");
    case ButtonEffects:
        return tr("Visual Effects");
    case ButtonEffectLaser:
        return effectsToolbarTooltip(EffectsMenuItem::LaserPointer);
    case ButtonEffectCursorHighlight:
        return effectsToolbarTooltip(EffectsMenuItem::CursorHighlight);
    case ButtonEffectSpotlight:
        return effectsToolbarTooltip(EffectsMenuItem::Spotlight);
    case ButtonEffectClickRipple:
        return effectsToolbarTooltip(EffectsMenuItem::ClickRipple);
    case ButtonEffectComposite:
        return effectsToolbarTooltip(EffectsMenuItem::CompositeToVideo);
    // Preview mode buttons
    case ButtonPlayPause:
        return m_isPlaying ? tr("Pause (Space)") : tr("Play (Space)");
    case ButtonVolume:
        return m_isMuted ? tr("Unmute (M)") : tr("Mute (M)");
    case ButtonFormatMP4:
        return tr("MP4 - Best quality");
    case ButtonFormatGIF:
        return tr("GIF - Universal support");
    case ButtonFormatWebP:
        return tr("WebP - Small size");
    case ButtonAnnotate:
        return tr("Annotate Video");
    case ButtonSave:
        return tr("Save (Enter)");
    case ButtonDiscard:
        return tr("Discard (Esc)");
    case AreaTimeline:
        return tr("Drag to seek");
    default:
        return QString();
    }
}

int RecordingControlBar::buttonAtPosition(const QPoint &pos) const
{
    // Ensure button rects are up-to-date (they're calculated based on current widget size)
    const_cast<RecordingControlBar *>(this)->updateButtonRects();

    if (m_mode == Mode::Preview) {
        // Preview mode buttons
        if (m_playPauseRect.contains(pos)) return ButtonPlayPause;
        if (m_volumeRect.contains(pos)) return ButtonVolume;
        if (m_formatMP4Rect.contains(pos)) return ButtonFormatMP4;
        if (m_formatGIFRect.contains(pos)) return ButtonFormatGIF;
        if (m_formatWebPRect.contains(pos)) return ButtonFormatWebP;
        if (m_annotateRect.contains(pos)) return ButtonAnnotate;
        if (m_saveRect.contains(pos)) return ButtonSave;
        if (m_discardRect.contains(pos)) return ButtonDiscard;

        // Check if in timeline area (with some vertical tolerance)
        QRect timelineHitArea = m_timelineRect.adjusted(0, -8, 0, 8);
        if (timelineHitArea.contains(pos)) return AreaTimeline;

        return ButtonNone;
    }

    // Recording mode buttons
    if (!m_isPreparing && m_pauseRect.contains(pos)) return ButtonPause;
    if (!m_isPreparing && m_stopRect.contains(pos)) return ButtonStop;
    if (m_cancelRect.contains(pos)) return ButtonCancel;

    if (m_effectsToolbarVisible && !m_isPreparing) {
        for (const auto &entry : m_effectsToolbarItems) {
            if (entry.first.contains(pos)) {
                return effectsToolbarButtonId(entry.second);
            }
        }
    }

    // Effects button
    if (!m_isPreparing && m_effectsButtonRect.contains(pos)) return ButtonEffects;

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
            if (m_mode == Mode::Preview) {
                // Preview mode button handling
                switch (button) {
                case ButtonPlayPause:
                    if (m_isPlaying) {
                        emit pauseRequested();
                    } else {
                        emit playRequested();
                    }
                    break;
                case ButtonVolume:
                    emit volumeToggled();
                    break;
                case ButtonFormatMP4:
                    setSelectedFormat(OutputFormat::MP4);
                    break;
                case ButtonFormatGIF:
                    setSelectedFormat(OutputFormat::GIF);
                    break;
                case ButtonFormatWebP:
                    setSelectedFormat(OutputFormat::WebP);
                    break;
                case ButtonAnnotate:
                    emit annotateRequested();
                    break;
                case ButtonSave:
                    emit savePreviewRequested();
                    break;
                case ButtonDiscard:
                    emit discardPreviewRequested();
                    break;
                case AreaTimeline:
                    // Start timeline scrubbing
                    m_isScrubbing = true;
                    m_scrubPosition = positionFromTimelineX(event->pos().x());
                    // Update time display during scrub
                    QString timeText = QString("%1 / %2")
                        .arg(formatPreviewTime(m_scrubPosition))
                        .arg(formatPreviewTime(m_previewDuration));
                    m_durationLabel->setText(timeText);
                    update();
                    break;
                }
                return;
            } else {
                // Recording mode button handling
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
                case ButtonEffects:
                    toggleEffectsToolbar();
                    break;
                case ButtonEffectLaser:
                    setLaserPointerEnabled(!m_laserEnabled);
                    break;
                case ButtonEffectCursorHighlight:
                    setCursorHighlightEnabled(!m_cursorHighlightEnabled);
                    break;
                case ButtonEffectSpotlight:
                    setSpotlightEnabled(!m_spotlightEnabled);
                    break;
                case ButtonEffectClickRipple:
                    setClickHighlightEnabled(!m_clickHighlightEnabled);
                    break;
                case ButtonEffectComposite:
                    setCompositeIndicatorsEnabled(!m_compositeIndicators);
                    break;
                }
                return;
            }
        }

        // Start dragging if not on a button
        m_isDragging = true;
        m_dragStartPos = event->globalPosition().toPoint();
        m_dragStartWidgetPos = pos();
        CursorManager::instance().pushCursorForWidget(this, CursorContext::Drag, Qt::ClosedHandCursor);
    }
    QWidget::mousePressEvent(event);
}

void RecordingControlBar::mouseMoveEvent(QMouseEvent *event)
{
    // Handle timeline scrubbing
    if (m_isScrubbing && m_mode == Mode::Preview) {
        m_scrubPosition = positionFromTimelineX(event->pos().x());
        // Update time display during scrub
        QString timeText = QString("%1 / %2")
            .arg(formatPreviewTime(m_scrubPosition))
            .arg(formatPreviewTime(m_previewDuration));
        m_durationLabel->setText(timeText);
        hideTooltip();
        update();
        return;
    }

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
        int newHovered = buttonAtPosition(event->pos());
        if (newHovered != m_hoveredButton) {
            m_hoveredButton = newHovered;
            auto& cm = CursorManager::instance();
            if (m_hoveredButton != ButtonNone) {
                cm.pushCursorForWidget(this, CursorContext::Hover, Qt::ArrowCursor);
            } else {
                cm.popCursorForWidget(this, CursorContext::Hover);
            }
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
        // Complete timeline scrubbing
        if (m_isScrubbing && m_mode == Mode::Preview) {
            m_isScrubbing = false;
            emit seekRequested(m_scrubPosition);
            m_previewPosition = m_scrubPosition;
            update();
        }

        if (m_isDragging) {
            m_isDragging = false;
            CursorManager::instance().popCursorForWidget(this, CursorContext::Drag);
            int button = buttonAtPosition(event->pos());
            auto& cm = CursorManager::instance();
            if (button != ButtonNone) {
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
        CursorManager::instance().clearAllForWidget(this);
        hideTooltip();
        update();
    }
    QWidget::leaveEvent(event);
}

void RecordingControlBar::keyPressEvent(QKeyEvent *event)
{
    if (m_mode == Mode::Preview) {
        // Preview mode key handling
        switch (event->key()) {
        case Qt::Key_Space:
            if (m_isPlaying) {
                emit pauseRequested();
            } else {
                emit playRequested();
            }
            break;
        case Qt::Key_M:
            emit volumeToggled();
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            emit savePreviewRequested();
            break;
        case Qt::Key_Escape:
            emit discardPreviewRequested();
            break;
        case Qt::Key_Left:
            // Seek backward 5 seconds
            if (m_previewDuration > 0) {
                qint64 newPos = qMax(0LL, m_previewPosition - 5000);
                emit seekRequested(newPos);
            }
            break;
        case Qt::Key_Right:
            // Seek forward 5 seconds
            if (m_previewDuration > 0) {
                qint64 newPos = qMin(m_previewDuration, m_previewPosition + 5000);
                emit seekRequested(newPos);
            }
            break;
        default:
            QWidget::keyPressEvent(event);
        }
    } else {
        // Recording mode key handling
        if (event->key() == Qt::Key_Escape) {
            emit stopRequested();
        } else {
            QWidget::keyPressEvent(event);
        }
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

const QVector<RecordingControlBar::EffectsMenuItem> &RecordingControlBar::effectsToolbarItems() const
{
    static const QVector<EffectsMenuItem> kItems = {
        EffectsMenuItem::LaserPointer,
        EffectsMenuItem::CursorHighlight,
        EffectsMenuItem::Spotlight,
        EffectsMenuItem::ClickRipple,
        EffectsMenuItem::CompositeToVideo
    };
    return kItems;
}

QString RecordingControlBar::effectsToolbarLabel(EffectsMenuItem item) const
{
    switch (item) {
    case EffectsMenuItem::LaserPointer:
        return tr("Laser");
    case EffectsMenuItem::CursorHighlight:
        return tr("Cursor");
    case EffectsMenuItem::Spotlight:
        return tr("Spot");
    case EffectsMenuItem::ClickRipple:
        return tr("Click");
    case EffectsMenuItem::CompositeToVideo:
        return tr("Embed");
    }
    return QString();
}

QString RecordingControlBar::effectsToolbarTooltip(EffectsMenuItem item) const
{
    switch (item) {
    case EffectsMenuItem::LaserPointer:
        return tr("Laser Pointer");
    case EffectsMenuItem::CursorHighlight:
        return tr("Cursor Highlight");
    case EffectsMenuItem::Spotlight:
        return tr("Spotlight");
    case EffectsMenuItem::ClickRipple:
        return tr("Click Ripple");
    case EffectsMenuItem::CompositeToVideo:
        return tr("Embed in Video");
    }
    return QString();
}

int RecordingControlBar::effectsToolbarButtonId(EffectsMenuItem item) const
{
    switch (item) {
    case EffectsMenuItem::LaserPointer:
        return ButtonEffectLaser;
    case EffectsMenuItem::CursorHighlight:
        return ButtonEffectCursorHighlight;
    case EffectsMenuItem::Spotlight:
        return ButtonEffectSpotlight;
    case EffectsMenuItem::ClickRipple:
        return ButtonEffectClickRipple;
    case EffectsMenuItem::CompositeToVideo:
        return ButtonEffectComposite;
    }
    return ButtonNone;
}

QFont RecordingControlBar::effectsToolbarFont() const
{
    QFont font = this->font();
    font.setPointSize(9);
    font.setWeight(QFont::Medium);
    return font;
}

int RecordingControlBar::effectsToolbarButtonWidth(EffectsMenuItem item, const QFontMetrics &metrics) const
{
    return metrics.horizontalAdvance(effectsToolbarLabel(item)) + kEffectsButtonPaddingX * 2;
}

int RecordingControlBar::effectsToolbarButtonsWidth(const QFontMetrics &metrics) const
{
    const auto &items = effectsToolbarItems();
    if (items.isEmpty()) {
        return 0;
    }

    int total = 0;
    for (EffectsMenuItem item : items) {
        total += effectsToolbarButtonWidth(item, metrics);
    }
    total += kEffectsButtonSpacing * (items.size() - 1);
    return total;
}

int RecordingControlBar::effectsToolbarWidth() const
{
    if (!m_effectsToolbarVisible) {
        return 0;
    }

    QFontMetrics metrics(effectsToolbarFont());
    int buttonsWidth = effectsToolbarButtonsWidth(metrics);
    return buttonsWidth > 0 ? (kEffectsToolbarGap + buttonsWidth) : 0;
}

bool RecordingControlBar::isEffectEnabled(EffectsMenuItem item) const
{
    switch (item) {
    case EffectsMenuItem::LaserPointer:
        return m_laserEnabled;
    case EffectsMenuItem::CursorHighlight:
        return m_cursorHighlightEnabled;
    case EffectsMenuItem::Spotlight:
        return m_spotlightEnabled;
    case EffectsMenuItem::ClickRipple:
        return m_clickHighlightEnabled;
    case EffectsMenuItem::CompositeToVideo:
        return m_compositeIndicators;
    }
    return false;
}

// ============================================================================
// Visual Effects Implementation
// ============================================================================

void RecordingControlBar::setLaserPointerEnabled(bool enabled)
{
    if (m_laserEnabled != enabled) {
        m_laserEnabled = enabled;
        update();
        emit laserPointerToggled(enabled);
    }
}

void RecordingControlBar::setCursorHighlightEnabled(bool enabled)
{
    if (m_cursorHighlightEnabled != enabled) {
        m_cursorHighlightEnabled = enabled;
        update();
        emit cursorHighlightToggled(enabled);
    }
}

void RecordingControlBar::setSpotlightEnabled(bool enabled)
{
    if (m_spotlightEnabled != enabled) {
        m_spotlightEnabled = enabled;
        update();
        emit spotlightToggled(enabled);
    }
}

void RecordingControlBar::setCompositeIndicatorsEnabled(bool enabled)
{
    if (m_compositeIndicators != enabled) {
        m_compositeIndicators = enabled;
        update();
        emit compositeIndicatorsToggled(enabled);
    }
}

void RecordingControlBar::setClickHighlightEnabled(bool enabled)
{
    if (m_clickHighlightEnabled != enabled) {
        m_clickHighlightEnabled = enabled;
        update();
        // Note: Click highlight is handled through setClickHighlightEnabled on overlay
    }
}

void RecordingControlBar::toggleEffectsToolbar()
{
    m_effectsToolbarVisible = !m_effectsToolbarVisible;
    if (!m_effectsToolbarVisible &&
        (m_hoveredButton == ButtonEffectLaser ||
         m_hoveredButton == ButtonEffectCursorHighlight ||
         m_hoveredButton == ButtonEffectSpotlight ||
         m_hoveredButton == ButtonEffectClickRipple ||
         m_hoveredButton == ButtonEffectComposite)) {
        m_hoveredButton = ButtonNone;
        hideTooltip();
    }

    updateButtonSpacerWidth();
    updateFixedWidth();
    update();
}

void RecordingControlBar::drawEffectsButton(QPainter &painter)
{
    if (m_effectsButtonRect.isNull()) return;

    ToolbarStyleConfig config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    const bool enabled = !m_isPreparing;

    bool isHovered = enabled && (m_hoveredButton == ButtonEffects);
    bool hasActiveEffect = m_laserEnabled || m_cursorHighlightEnabled ||
                          m_spotlightEnabled || m_clickHighlightEnabled ||
                          m_compositeIndicators;

    // Draw background
    if (enabled && (hasActiveEffect || isHovered || m_effectsToolbarVisible)) {
        QColor bgColor = hasActiveEffect || m_effectsToolbarVisible
                       ? QColor(0, 122, 255, 60)
                       : config.hoverBackgroundColor;
        painter.setPen(Qt::NoPen);
        painter.setBrush(bgColor);
        painter.drawRoundedRect(m_effectsButtonRect.adjusted(2, 2, -2, -2), 6, 6);
    }

    // Draw icon (using a simple "fx" text or effect icon)
    QColor iconColor = enabled ? (hasActiveEffect ? QColor(0, 122, 255) : config.iconNormalColor)
                               : dimmedColor(config.iconNormalColor);

    painter.setPen(iconColor);
    QFont smallFont = painter.font();
    smallFont.setPointSize(9);
    smallFont.setBold(true);
    painter.setFont(smallFont);
    painter.drawText(m_effectsButtonRect, Qt::AlignCenter, "FX");
}

void RecordingControlBar::drawEffectsToolbar(QPainter &painter)
{
    if (!m_effectsToolbarVisible || m_effectsToolbarItems.isEmpty()) {
        return;
    }

    ToolbarStyleConfig config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    if (!m_effectsToolbarRect.isNull()) {
        GlassRenderer::drawGlassPanel(painter, m_effectsToolbarRect, config, 6);
    }

    QFont font = effectsToolbarFont();
    painter.setFont(font);

    const bool enabled = !m_isPreparing;
    for (const auto &entry : m_effectsToolbarItems) {
        const QRect &rect = entry.first;
        EffectsMenuItem item = entry.second;

        int buttonId = effectsToolbarButtonId(item);
        bool isHovered = enabled && (m_hoveredButton == buttonId);
        bool isActive = enabled && isEffectEnabled(item);

        if (isActive || isHovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(isActive ? config.buttonActiveColor : config.hoverBackgroundColor);
            painter.drawRoundedRect(rect, 4, 4);
        }

        QColor textColor = isActive ? config.textActiveColor : config.textColor;
        if (!enabled) {
            textColor = dimmedColor(textColor);
        }
        painter.setPen(textColor);
        painter.drawText(rect, Qt::AlignCenter, effectsToolbarLabel(item));
    }
}

// ============================================================================
// Preview Mode Implementation
// ============================================================================

void RecordingControlBar::setMode(Mode mode)
{
    if (m_mode == mode) {
        return;
    }

    m_mode = mode;
    m_hoveredButton = ButtonNone;
    hideTooltip();

    if (mode == Mode::Preview) {
        // Hide ALL QLabel widgets - we draw everything custom in preview mode
        if (m_sizeLabel) m_sizeLabel->hide();
        if (m_fpsLabel) m_fpsLabel->hide();
        if (m_audioIndicator) m_audioIndicator->hide();
        if (m_separator1) m_separator1->hide();
        if (m_separator2) m_separator2->hide();
        if (m_durationLabel) m_durationLabel->hide();
        if (m_recordingIndicator) m_recordingIndicator->hide();
        if (m_buttonSpacer) m_buttonSpacer->hide();

        // Reset preview state
        m_isPlaying = false;
        m_previewPosition = 0;
        m_selectedFormat = OutputFormat::MP4;
        m_isMuted = false;
        m_effectsToolbarVisible = false;
        m_effectsToolbarItems.clear();
        m_effectsToolbarRect = QRect();
    } else {
        // Show recording-specific widgets
        if (m_sizeLabel) m_sizeLabel->show();
        if (m_fpsLabel) m_fpsLabel->show();
        if (m_separator1) m_separator1->show();
        if (m_separator2) m_separator2->show();
        if (m_durationLabel) m_durationLabel->show();
        if (m_recordingIndicator) m_recordingIndicator->show();
        if (m_buttonSpacer) m_buttonSpacer->show();
    }

    updateButtonSpacerWidth();
    updateFixedWidth();
    update();
}

void RecordingControlBar::updatePreviewButtonRects()
{
    const QRect bgRect = backgroundRect();
    if (bgRect.width() <= 0) {
        m_playPauseRect = QRect();
        m_timelineRect = QRect();
        m_volumeRect = QRect();
        m_formatMP4Rect = QRect();
        m_formatGIFRect = QRect();
        m_formatWebPRect = QRect();
        m_annotateRect = QRect();
        m_saveRect = QRect();
        m_discardRect = QRect();
        m_indicatorRect = QRect();
        m_durationRect = QRect();
        return;
    }

    int leftMargin = 10;
    int rightMargin = 10;
    int buttonSize = 24;
    int y = bgRect.top() + (bgRect.height() - buttonSize) / 2;
    int bgLeft = bgRect.left();
    int bgRight = bgRect.left() + bgRect.width();

    // === RIGHT SIDE (position from right to left) ===

    // Discard button (rightmost)
    int x = bgRight - rightMargin - buttonSize;
    m_discardRect = QRect(x, y, buttonSize, buttonSize);

    // Save button
    x -= buttonSize + BUTTON_SPACING;
    m_saveRect = QRect(x, y, buttonSize, buttonSize);

    // Annotate button (text)
    int annotateWidth = annotateButtonWidth();
    x -= annotateWidth + BUTTON_SPACING;
    m_annotateRect = QRect(x, y, annotateWidth, buttonSize);

    // Separator margin before format buttons
    x -= SEPARATOR_MARGIN;

    // Format buttons with proper widths for text (WebP, GIF, MP4 from right to left)
    int webpWidth = 42;
    int gifWidth = 32;
    int mp4Width = 38;
    int formatSpacing = 4;

    x -= webpWidth;
    m_formatWebPRect = QRect(x, y, webpWidth, buttonSize);

    x -= gifWidth + formatSpacing;
    m_formatGIFRect = QRect(x, y, gifWidth, buttonSize);

    x -= mp4Width + formatSpacing;
    m_formatMP4Rect = QRect(x, y, mp4Width, buttonSize);

    // Separator margin before playback controls
    x -= SEPARATOR_MARGIN;

    // Volume button
    x -= buttonSize;
    m_volumeRect = QRect(x, y, buttonSize, buttonSize);

    // Play/Pause button
    x -= buttonSize + BUTTON_SPACING;
    m_playPauseRect = QRect(x, y, buttonSize, buttonSize);

    // === LEFT SIDE (custom-drawn elements) ===

    // Indicator (orange circle)
    int indicatorSize = 12;
    m_indicatorRect = QRect(bgLeft + leftMargin,
                            bgRect.top() + (bgRect.height() - indicatorSize) / 2,
                            indicatorSize, indicatorSize);

    // Duration label rect
    int durationX = bgLeft + leftMargin + indicatorSize + 6;
    int durationWidth = 90;
    m_durationRect = QRect(durationX, bgRect.top(), durationWidth, bgRect.height());

    // Timeline fills the gap between duration and play button
    int timelineLeft = durationX + durationWidth + 8;
    int timelineRight = m_playPauseRect.left() - 8;
    int timelineHeight = 8;
    int timelineY = bgRect.top() + (bgRect.height() - timelineHeight) / 2;

    m_timelineRect = QRect(timelineLeft, timelineY, qMax(50, timelineRight - timelineLeft), timelineHeight);
}

void RecordingControlBar::updatePreviewPosition(qint64 positionMs)
{
    m_previewPosition = positionMs;
    if (!m_isScrubbing) {
        // Update duration label to show position / duration
        QString timeText = QString("%1 / %2")
            .arg(formatPreviewTime(m_previewPosition))
            .arg(formatPreviewTime(m_previewDuration));
        m_durationLabel->setText(timeText);
    }
    update();
}

void RecordingControlBar::updatePreviewDuration(qint64 durationMs)
{
    m_previewDuration = durationMs;
    // Update duration label
    QString timeText = QString("%1 / %2")
        .arg(formatPreviewTime(m_previewPosition))
        .arg(formatPreviewTime(m_previewDuration));
    m_durationLabel->setText(timeText);
    update();
}

void RecordingControlBar::setPlaying(bool playing)
{
    m_isPlaying = playing;
    updateIndicatorGradient();
    update();
}

void RecordingControlBar::setVolume(float volume)
{
    m_volume = qBound(0.0f, volume, 1.0f);
    update();
}

void RecordingControlBar::setMuted(bool muted)
{
    m_isMuted = muted;
    update();
}

void RecordingControlBar::setSelectedFormat(OutputFormat format)
{
    if (m_selectedFormat != format) {
        m_selectedFormat = format;
        update();
        emit formatSelected(format);
    }
}

QString RecordingControlBar::formatPreviewTime(qint64 ms) const
{
    int minutes = (ms / 60000) % 60;
    int seconds = (ms / 1000) % 60;
    return QString("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

void RecordingControlBar::drawPreviewModeUI(QPainter &painter)
{
    ToolbarStyleConfig config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());

    // Draw indicator (orange circle for preview mode)
    {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 149, 0));  // Orange
        painter.drawEllipse(m_indicatorRect);
    }

    // Draw duration label
    {
        QFont font("SF Mono", 11);
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.setPen(config.textColor);

        QString timeText = QString("%1 / %2")
            .arg(formatPreviewTime(m_previewPosition))
            .arg(formatPreviewTime(m_previewDuration));
        painter.drawText(m_durationRect, Qt::AlignVCenter | Qt::AlignLeft, timeText);
    }

    // Draw Play/Pause button
    {
        bool isHovered = (m_hoveredButton == ButtonPlayPause);
        if (isHovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(config.hoverBackgroundColor);
            painter.drawRoundedRect(m_playPauseRect.adjusted(2, 2, -2, -2), 6, 6);
        }
        QString iconKey = m_isPlaying ? "pause" : "play";
        QColor iconColor = isHovered ? config.iconActiveColor : config.iconNormalColor;
        IconRenderer::instance().renderIcon(painter, m_playPauseRect, iconKey, iconColor);
    }

    // Draw timeline
    drawTimeline(painter);

    // Draw Volume button
    {
        bool isHovered = (m_hoveredButton == ButtonVolume);
        if (isHovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(config.hoverBackgroundColor);
            painter.drawRoundedRect(m_volumeRect.adjusted(2, 2, -2, -2), 6, 6);
        }

        // Draw volume icon or mute indicator
        if (m_isMuted) {
            // Draw muted indicator (simple crossed-out speaker)
            QRect iconRect = m_volumeRect.adjusted(4, 4, -4, -4);
            painter.setPen(QPen(config.iconNormalColor, 1.5));
            painter.drawLine(iconRect.topLeft(), iconRect.bottomRight());
            painter.drawLine(iconRect.topRight(), iconRect.bottomLeft());
        } else {
            // Draw volume bars
            QRect iconRect = m_volumeRect.adjusted(6, 6, -6, -6);
            painter.setPen(Qt::NoPen);
            QColor volColor = isHovered ? config.iconActiveColor : config.iconNormalColor;
            painter.setBrush(volColor);

            int barWidth = 3;
            int spacing = 2;
            int barCount = 3;
            int totalWidth = barCount * barWidth + (barCount - 1) * spacing;
            int startX = iconRect.center().x() - totalWidth / 2;

            for (int i = 0; i < barCount; i++) {
                int barHeight = 4 + i * 3;
                int barY = iconRect.bottom() - barHeight;
                painter.drawRoundedRect(startX + i * (barWidth + spacing), barY, barWidth, barHeight, 1, 1);
            }
        }
    }

    // Draw format selection buttons
    drawFormatButtons(painter);

    // Draw separator before annotate/save/discard
    {
        int sepX = m_annotateRect.left() - SEPARATOR_MARGIN / 2;
        painter.setPen(QPen(config.hairlineBorderColor, 1));
        painter.drawLine(sepX, 6, sepX, TOOLBAR_HEIGHT - 6);
    }

    // Draw Annotate button
    {
        bool isHovered = (m_hoveredButton == ButtonAnnotate);
        QColor bgColor = config.hoverBackgroundColor;
        if (isHovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(bgColor);
            painter.drawRoundedRect(m_annotateRect, 4, 4);
        } else {
            QColor inactiveColor = config.hoverBackgroundColor;
            inactiveColor.setAlpha(40);
            painter.setPen(Qt::NoPen);
            painter.setBrush(inactiveColor);
            painter.drawRoundedRect(m_annotateRect, 4, 4);
        }

        QFont font = painter.font();
        font.setPointSize(9);
        font.setWeight(QFont::Medium);
        painter.setFont(font);
        painter.setPen(config.textColor);
        painter.drawText(m_annotateRect, Qt::AlignCenter, tr("Annotate"));
    }

    // Draw Save button
    {
        bool isHovered = (m_hoveredButton == ButtonSave);
        if (isHovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(config.hoverBackgroundColor);
            painter.drawRoundedRect(m_saveRect.adjusted(2, 2, -2, -2), 6, 6);
        }
        QColor iconColor = QColor(52, 199, 89);  // Green
        IconRenderer::instance().renderIcon(painter, m_saveRect, "save", iconColor);
    }

    // Draw Discard button
    {
        bool isHovered = (m_hoveredButton == ButtonDiscard);
        if (isHovered) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(config.hoverBackgroundColor);
            painter.drawRoundedRect(m_discardRect.adjusted(2, 2, -2, -2), 6, 6);
        }
        QColor iconColor = isHovered ? config.iconCancelColor : config.iconNormalColor;
        IconRenderer::instance().renderIcon(painter, m_discardRect, "cancel", iconColor);
    }
}

void RecordingControlBar::drawTimeline(QPainter &painter)
{
    if (m_timelineRect.isEmpty() || m_previewDuration <= 0) {
        return;
    }

    ToolbarStyleConfig config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());

    // Draw track background - use theme-aware color
    painter.setPen(Qt::NoPen);
    QColor trackColor = config.hoverBackgroundColor;
    trackColor.setAlpha(80);
    painter.setBrush(trackColor);
    painter.drawRoundedRect(m_timelineRect, 4, 4);

    // Draw progress
    qint64 displayPosition = m_isScrubbing ? m_scrubPosition : m_previewPosition;
    double progress = static_cast<double>(displayPosition) / m_previewDuration;
    progress = qBound(0.0, progress, 1.0);

    int progressWidth = static_cast<int>(m_timelineRect.width() * progress);
    if (progressWidth > 0) {
        QRect progressRect(m_timelineRect.left(), m_timelineRect.top(),
                          progressWidth, m_timelineRect.height());
        painter.setBrush(QColor(0, 122, 255));
        painter.drawRoundedRect(progressRect, 4, 4);
    }

    // Draw playhead
    int playheadX = m_timelineRect.left() + progressWidth;
    int playheadSize = 12;
    QRect playheadRect(playheadX - playheadSize / 2,
                       m_timelineRect.center().y() - playheadSize / 2,
                       playheadSize, playheadSize);

    // Playhead shadow
    painter.setBrush(QColor(0, 0, 0, 30));
    painter.drawEllipse(playheadRect.adjusted(1, 1, 1, 1));

    // Playhead
    painter.setBrush(Qt::white);
    painter.setPen(QPen(QColor(0, 0, 0, 40), 1));
    painter.drawEllipse(playheadRect);
}

void RecordingControlBar::drawFormatButtons(QPainter &painter)
{
    ToolbarStyleConfig config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());

    auto drawFormatButton = [&](const QRect &rect, const QString &label, OutputFormat format, int buttonId) {
        bool isSelected = (m_selectedFormat == format);
        bool isHovered = (m_hoveredButton == buttonId);

        // Background
        painter.setPen(Qt::NoPen);
        if (isSelected) {
            painter.setBrush(config.buttonActiveColor);
        } else if (isHovered) {
            painter.setBrush(config.hoverBackgroundColor);
        } else {
            QColor inactiveColor = config.hoverBackgroundColor;
            inactiveColor.setAlpha(40);
            painter.setBrush(inactiveColor);
        }
        painter.drawRoundedRect(rect, 4, 4);

        // Text
        QFont font = painter.font();
        font.setPointSize(9);
        font.setWeight(QFont::Medium);
        painter.setFont(font);

        if (isSelected) {
            painter.setPen(config.textActiveColor);  // White on blue background
        } else {
            painter.setPen(config.textColor);
        }
        painter.drawText(rect, Qt::AlignCenter, label);
    };

    drawFormatButton(m_formatMP4Rect, "MP4", OutputFormat::MP4, ButtonFormatMP4);
    drawFormatButton(m_formatGIFRect, "GIF", OutputFormat::GIF, ButtonFormatGIF);
    drawFormatButton(m_formatWebPRect, "WebP", OutputFormat::WebP, ButtonFormatWebP);
}

qint64 RecordingControlBar::positionFromTimelineX(int x) const
{
    if (m_timelineRect.isEmpty() || m_previewDuration <= 0) {
        return 0;
    }

    int relativeX = x - m_timelineRect.left();
    double progress = static_cast<double>(relativeX) / m_timelineRect.width();
    progress = qBound(0.0, progress, 1.0);

    return static_cast<qint64>(progress * m_previewDuration);
}
