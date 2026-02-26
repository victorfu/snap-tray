#include "scroll/ScrollCaptureControlBar.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QVBoxLayout>
#include <QFontMetrics>
#include <QtGlobal>

namespace {

constexpr int kMargin = 12;
constexpr int kGap = 10;
constexpr int kPreviewWidth = 320;
constexpr int kPreviewHeight = 180;

QPoint boundedTopLeft(const QPoint &candidate, const QSize &size, const QRect &screenRect)
{
    const int minX = screenRect.left() + kMargin;
    const int minY = screenRect.top() + kMargin;
    const int maxX = qMax(minX, screenRect.right() - size.width() - kMargin + 1);
    const int maxY = qMax(minY, screenRect.bottom() - size.height() - kMargin + 1);
    return QPoint(qBound(minX, candidate.x(), maxX),
                  qBound(minY, candidate.y(), maxY));
}

} // namespace

ScrollCaptureControlBar::ScrollCaptureControlBar(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus)
{
    setObjectName(QStringLiteral("scrollCaptureControlBar"));
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);

#ifdef Q_OS_MACOS
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
#endif

    setStyleSheet(
        "QWidget#scrollCaptureControlBar {"
        "  background: rgba(18, 22, 29, 232);"
        "  border: 1px solid rgba(255,255,255,35);"
        "  border-radius: 8px;"
        "}"
        "QWidget#scrollCaptureControlBar QLabel { color: #E5E7EB; }"
        "QWidget#scrollCaptureControlBar QPushButton {"
        "  background: rgba(255,255,255,16);"
        "  color: #F8FAFC;"
        "  border: 1px solid rgba(255,255,255,30);"
        "  border-radius: 6px;"
        "  padding: 3px 8px;"
        "}"
        "QWidget#scrollCaptureControlBar QPushButton:hover { background: rgba(255,255,255,24); }"
        "QWidget#scrollCaptureControlBar QPushButton:disabled { color: #94A3B8; }");

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(8, 8, 8, 8);
    rootLayout->setSpacing(6);

    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(6);

    m_statusLabel = new QLabel(tr("Manual"), this);
    m_statusLabel->setMinimumWidth(160);
    m_statusLabel->setMaximumWidth(320);
    m_statusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    headerLayout->addWidget(m_statusLabel, 1);

    m_autoAssistButton = new QPushButton(tr("Start Auto Assist"), this);
    m_autoAssistButton->setObjectName(QStringLiteral("scrollInterruptAutoButton"));
    connect(m_autoAssistButton, &QPushButton::clicked, this, &ScrollCaptureControlBar::autoAssistRequested);
    headerLayout->addWidget(m_autoAssistButton);

    m_focusButton = new QPushButton(tr("Focus Target"), this);
    m_focusButton->setObjectName(QStringLiteral("scrollFocusTargetButton"));
    connect(m_focusButton, &QPushButton::clicked, this, &ScrollCaptureControlBar::focusRequested);
    headerLayout->addWidget(m_focusButton);

    m_detailsButton = new QPushButton(tr("Details"), this);
    connect(m_detailsButton, &QPushButton::clicked, this, [this]() {
        const bool expanded = !m_detailsWidget->isVisible();
        setDetailsExpanded(expanded);
        emit detailsExpandedChanged(expanded);
    });
    headerLayout->addWidget(m_detailsButton);

    m_finishButton = new QPushButton(tr("Finish"), this);
    connect(m_finishButton, &QPushButton::clicked, this, &ScrollCaptureControlBar::finishRequested);
    headerLayout->addWidget(m_finishButton);

    m_cancelButton = new QPushButton(tr("Cancel"), this);
    connect(m_cancelButton, &QPushButton::clicked, this, &ScrollCaptureControlBar::cancelRequested);
    headerLayout->addWidget(m_cancelButton);

    rootLayout->addLayout(headerLayout);

    m_detailsWidget = new QWidget(this);
    auto *detailsLayout = new QVBoxLayout(m_detailsWidget);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    detailsLayout->setSpacing(4);

    m_metaLabel = new QLabel(m_detailsWidget);
    m_metaLabel->setObjectName(QStringLiteral("scrollProgressLabel"));
    m_metaLabel->setWordWrap(true);
    m_metaLabel->setStyleSheet(QStringLiteral("QLabel { color: #94A3B8; font-size: 11px; }"));
    detailsLayout->addWidget(m_metaLabel);

    m_previewLabel = new QLabel(m_detailsWidget);
    m_previewLabel->setFixedSize(kPreviewWidth, kPreviewHeight);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setStyleSheet(QStringLiteral(
        "QLabel { background: rgba(15, 23, 42, 200); border: 1px solid rgba(255,255,255,25); border-radius: 6px; }"));
    detailsLayout->addWidget(m_previewLabel, 0, Qt::AlignCenter);

    rootLayout->addWidget(m_detailsWidget);

    setDetailsExpanded(false);

    m_escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    m_escapeShortcut->setContext(Qt::ApplicationShortcut);
    connect(m_escapeShortcut, &QShortcut::activated, this, &ScrollCaptureControlBar::cancelRequested);
}

void ScrollCaptureControlBar::setStatusText(const QString &text)
{
    if (m_statusLabel) {
        const QFontMetrics fm(m_statusLabel->font());
        const QString elided = fm.elidedText(text, Qt::ElideRight, m_statusLabel->maximumWidth() - 8);
        m_statusLabel->setText(elided);
        m_statusLabel->setToolTip(text);
    }
}

void ScrollCaptureControlBar::setAutoAssistText(const QString &text)
{
    if (m_autoAssistButton) {
        m_autoAssistButton->setText(text);
    }
}

void ScrollCaptureControlBar::setAutoAssistEnabled(bool enabled)
{
    if (m_autoAssistButton) {
        m_autoAssistButton->setEnabled(enabled);
    }
}

void ScrollCaptureControlBar::setFocusText(const QString &text)
{
    if (m_focusButton) {
        m_focusButton->setText(text);
    }
}

void ScrollCaptureControlBar::setFocusVisible(bool visible)
{
    if (m_focusButton) {
        m_focusButton->setVisible(visible);
    }
}

void ScrollCaptureControlBar::setFocusEnabled(bool enabled)
{
    if (m_focusButton) {
        m_focusButton->setEnabled(enabled);
    }
}

void ScrollCaptureControlBar::setMetaText(const QString &text)
{
    if (m_metaLabel) {
        m_metaLabel->setText(text);
    }
}

void ScrollCaptureControlBar::setPreviewImage(const QImage &preview)
{
    if (!m_previewLabel || preview.isNull()) {
        return;
    }

    const QPixmap pixmap = QPixmap::fromImage(preview);
    m_previewLabel->setPixmap(pixmap.scaled(
        m_previewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

bool ScrollCaptureControlBar::detailsExpanded() const
{
    return m_detailsWidget && m_detailsWidget->isVisible();
}

void ScrollCaptureControlBar::setDetailsExpanded(bool expanded)
{
    if (!m_detailsWidget) {
        return;
    }

    m_detailsWidget->setVisible(expanded);
    if (m_detailsButton) {
        m_detailsButton->setText(expanded ? tr("Hide Details") : tr("Details"));
    }
    adjustSize();
}

bool ScrollCaptureControlBar::hasManualPosition() const
{
    return m_manualPositionSet;
}

void ScrollCaptureControlBar::positionNear(const QRect &captureRegion, QScreen *screen)
{
    if (m_manualPositionSet) {
        return;
    }

    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }

    adjustSize();
    const QSize barSize = size();
    const QRect screenRect = screen->geometry();

    const QList<QPoint> candidates{
        QPoint(captureRegion.center().x() - barSize.width() / 2,
               captureRegion.top() - barSize.height() - kGap),
        QPoint(captureRegion.center().x() - barSize.width() / 2,
               captureRegion.bottom() + kGap),
        QPoint(screenRect.left() + kMargin, screenRect.top() + kMargin),
        QPoint(screenRect.right() - barSize.width() - kMargin + 1,
               screenRect.top() + kMargin),
        QPoint(screenRect.left() + kMargin,
               screenRect.bottom() - barSize.height() - kMargin + 1)
    };

    const QRect avoidRect = captureRegion.adjusted(-8, -8, 8, 8);
    QPoint chosen = boundedTopLeft(candidates.first(), barSize, screenRect);
    for (const QPoint &candidate : candidates) {
        const QPoint bounded = boundedTopLeft(candidate, barSize, screenRect);
        if (!QRect(bounded, barSize).intersects(avoidRect)) {
            chosen = bounded;
            break;
        }
    }

    move(chosen);
}

bool ScrollCaptureControlBar::hitInteractiveChild(const QPoint &pos) const
{
    QWidget *child = childAt(pos);
    while (child) {
        if (qobject_cast<const QPushButton *>(child)) {
            return true;
        }
        child = child->parentWidget();
    }
    return false;
}

void ScrollCaptureControlBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !hitInteractiveChild(event->pos())) {
        m_dragging = true;
        m_dragAnchorGlobal = event->globalPosition().toPoint();
        m_dragAnchorPos = pos();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void ScrollCaptureControlBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        const QPoint delta = event->globalPosition().toPoint() - m_dragAnchorGlobal;
        move(m_dragAnchorPos + delta);
        m_manualPositionSet = true;
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void ScrollCaptureControlBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}
