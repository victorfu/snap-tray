#include "capture/PostCaptureCard.h"

#include "GlassRenderer.h"
#include "platform/WindowInput.h"
#include "platform/WindowLevel.h"

#include <QApplication>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QToolButton>
#include <QVBoxLayout>

PostCaptureCard::PostCaptureCard(const QPixmap& pixmap, QScreen* sourceScreen)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_style(ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle()))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_QuitOnClose, false);
    setWindowTitle(QCoreApplication::translate("HistoryWindow", "Screenshot"));

    auto* layout = new QVBoxLayout(this);
    auto* preview = new QLabel(this);
    constexpr int previewWidth = 192;
    constexpr int previewHeight = 72;
    QScreen* target = sourceScreen ? sourceScreen : QApplication::primaryScreen();
    setScreen(target);
    const qreal dpr = target->devicePixelRatio();
    QPixmap thumb = pixmap.scaled(QSize(previewWidth, previewHeight) * dpr,
                                  Qt::KeepAspectRatio, Qt::SmoothTransformation);
    thumb.setDevicePixelRatio(dpr);
    preview->setPixmap(thumb);
    preview->setAlignment(Qt::AlignCenter);
    layout->addWidget(preview);

    auto* row = new QHBoxLayout;
    layout->addLayout(row);
    m_copy = new QPushButton(QCoreApplication::translate("HistoryWindow", "Copy"), this);
    m_save = new QPushButton(QCoreApplication::translate("BeautifyPanel", "Save"), this);
    row->addWidget(m_copy);
    row->addWidget(m_save);
    connect(m_copy, &QPushButton::clicked, this, [this] { emit actionRequested(Action::Copy); });
    connect(m_save, &QPushButton::clicked, this, [this] { emit actionRequested(Action::Save); });

    auto* more = new QToolButton(this);
    more->setText(QStringLiteral("…"));
    more->setAccessibleName(QCoreApplication::translate("FloatingToolbar", "More"));
    more->setFocusPolicy(Qt::StrongFocus);
    more->setPopupMode(QToolButton::InstantPopup);
    m_menu = new QMenu(more);
    more->setMenu(m_menu);
    auto addAction = [this](const QString& label, Action action) {
        return m_menu->addAction(label, this, [this, action] { emit actionRequested(action); });
    };
    addAction(QCoreApplication::translate("HistoryWindow", "Pin"), Action::Pin);
    m_edit = addAction(QCoreApplication::translate("HistoryWindow", "Edit"), Action::Edit);
    m_edit->setEnabled(false);
    addAction(QCoreApplication::translate("BeautifyPanel", "Beautify"), Action::Beautify);
    row->addWidget(more);

    auto* dismiss = new QToolButton(this);
    dismiss->setText(QStringLiteral("×"));
    dismiss->setAccessibleName(QCoreApplication::translate("BeautifyPanel", "Close"));
    dismiss->setFocusPolicy(Qt::StrongFocus);
    row->addWidget(dismiss);
    connect(dismiss, &QToolButton::clicked, this, &QWidget::close);
    new QShortcut(QKeySequence(Qt::Key_Escape), this, [this] { close(); });
    connect(m_menu, &QMenu::aboutToShow, this, [this] { m_menuOpen = true; updateCountdown(); });
    connect(m_menu, &QMenu::aboutToHide, this, [this] { m_menuOpen = false; updateCountdown(); });

    m_status = new QLabel(this);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    QPalette palette = m_status->palette();
    palette.setColor(QPalette::WindowText, m_style.textColor);
    m_status->setPalette(palette);
    m_status->hide();
    layout->addWidget(m_status);
    setFixedWidth(sizeHint().width());
    adjustSize();
    constexpr int margin = 16;
    const QRect area = target->availableGeometry();
    move(area.right() - width() - margin + 1, area.bottom() - height() - margin + 1);
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &QWidget::close);
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* current) {
        m_keyboardFocus = current && isAncestorOf(current) && testAttribute(Qt::WA_KeyboardFocusChange);
        updateCountdown();
    });
    show();
    // Some window systems negotiate the initial geometry during show().
    move(area.right() - width() - margin + 1, area.bottom() - height() - margin + 1);
    raise();
    setWindowExcludedFromCapture(this, true);
    preventWindowHideOnDeactivate(this);
    updateCountdown();
}

PostCaptureCard::~PostCaptureCard()
{
    // QWidget can change application focus while destroying its children,
    // after this class's countdown and error state have been destroyed.
    disconnect(qApp, nullptr, this, nullptr);
    disconnect(m_menu, nullptr, this, nullptr);
    hide();
}

void PostCaptureCard::setReplayAvailable(bool available)
{
    m_edit->setEnabled(available);
}

void PostCaptureCard::setBusy(Action action, bool busy)
{
    (action == Action::Copy ? m_copy : m_save)->setEnabled(!busy);
    updateCountdown();
}

void PostCaptureCard::setError(Action action, const QString& message)
{
    if (message.isEmpty())
        m_errors.remove(action);
    else
        m_errors.insert(action, message);
    const int bottom = geometry().bottom();
    m_status->setText(m_errors.values().join(QLatin1Char('\n')));
    m_status->setVisible(!m_errors.isEmpty());
    layout()->activate();
    adjustSize();
    move(x(), qMax(screen()->availableGeometry().top(), bottom - height() + 1));
    updateCountdown();
}

void PostCaptureCard::updateCountdown()
{
    const bool paused = m_hovered || m_dragging || m_menuOpen || m_keyboardFocus || !m_errors.isEmpty()
        || !m_copy->isEnabled() || !m_save->isEnabled();
    if (paused && m_timer.isActive()) {
        m_remainingMs = m_timer.remainingTime();
        m_timer.stop();
    } else if (!paused && !m_timer.isActive() && isVisible()) {
        m_timer.start(m_remainingMs);
    }
}

bool PostCaptureCard::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    if (m_dragging) {
        if (const auto position = SnapTray::nativeLeftButtonReleasePosition(eventType, message))
            m_nativeDropPosition = position;
    }
    return QWidget::nativeEvent(eventType, message, result);
}

void PostCaptureCard::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    GlassRenderer::drawGlassPanel(painter, rect(), m_style);
}

void PostCaptureCard::enterEvent(QEnterEvent*)
{
    m_hovered = true;
    updateCountdown();
}

void PostCaptureCard::leaveEvent(QEvent*)
{
    m_hovered = false;
    updateCountdown();
}

void PostCaptureCard::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragOffset = event->globalPosition().toPoint() - pos();
        m_dragGrabPosition = event->position();
        m_nativeDropPosition.reset();
        m_dragging = true;
        updateCountdown();
    }
}

void PostCaptureCard::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && !SnapTray::moveFramelessWindowWithNativeCursor(this, m_dragGrabPosition))
        move(event->globalPosition().toPoint() - m_dragOffset);
}

void PostCaptureCard::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_dragging || event->button() != Qt::LeftButton)
        return;
    m_dragging = false;
    if (m_nativeDropPosition)
        SnapTray::moveFramelessWindowWithNativeCursor(this, m_dragGrabPosition, m_nativeDropPosition);
    m_nativeDropPosition.reset();
    updateCountdown();
}

void PostCaptureCard::hideEvent(QHideEvent*)
{
    m_timer.stop();
    m_menu->hide();
}
