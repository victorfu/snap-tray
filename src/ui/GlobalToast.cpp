#include "ui/GlobalToast.h"
#include "platform/WindowLevel.h"

#include <QApplication>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

GlobalToast& GlobalToast::instance()
{
    static GlobalToast instance;
    return instance;
}

GlobalToast::GlobalToast()
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_titleLabel(nullptr)
    , m_messageLabel(nullptr)
    , m_hideTimer(nullptr)
    , m_contentWidget(nullptr)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);

    setupUi();

    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, &QTimer::timeout, this, &QWidget::hide);
}

void GlobalToast::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_contentWidget = new QWidget(this);
    m_contentWidget->setObjectName("toastContent");

    auto* contentLayout = new QVBoxLayout(m_contentWidget);
    contentLayout->setContentsMargins(16, 12, 16, 12);
    contentLayout->setSpacing(4);

    m_titleLabel = new QLabel(m_contentWidget);
    m_titleLabel->setObjectName("toastTitle");
    m_titleLabel->setWordWrap(true);

    m_messageLabel = new QLabel(m_contentWidget);
    m_messageLabel->setObjectName("toastMessage");
    m_messageLabel->setWordWrap(true);

    contentLayout->addWidget(m_titleLabel);
    contentLayout->addWidget(m_messageLabel);

    mainLayout->addWidget(m_contentWidget);

    setFixedWidth(320);
}

void GlobalToast::showToast(Type type, const QString& title, const QString& message, int durationMs)
{
    m_titleLabel->setText(title);
    m_messageLabel->setText(message);
    m_messageLabel->setVisible(!message.isEmpty());

    updateStyle(type);

    adjustSize();
    positionOnScreen();

    QWidget::show();
    raise();

    // Set platform-specific floating window level after showing
    setWindowFloatingWithoutFocus(this);

    m_hideTimer->start(durationMs);
}

void GlobalToast::positionOnScreen()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return;
    }

    QRect screenGeometry = screen->availableGeometry();

    // Position in top-right corner, with some margin
    int x = screenGeometry.right() - width() - 20;
    int y = screenGeometry.top() + 20;

    move(x, y);
}

void GlobalToast::updateStyle(Type type)
{
    QString bgColor;
    switch (type) {
    case Success:
        bgColor = "rgba(34, 139, 34, 230)";
        break;
    case Error:
        bgColor = "rgba(200, 60, 60, 230)";
        break;
    case Info:
    default:
        bgColor = "rgba(59, 130, 246, 230)";
        break;
    }

    m_contentWidget->setStyleSheet(QString(
        "#toastContent {"
        "  background-color: %1;"
        "  border-radius: 8px;"
        "}"
    ).arg(bgColor));

    m_titleLabel->setStyleSheet(
        "#toastTitle {"
        "  color: white;"
        "  font-size: 13px;"
        "  font-weight: bold;"
        "}"
    );

    m_messageLabel->setStyleSheet(
        "#toastMessage {"
        "  color: rgba(255, 255, 255, 200);"
        "  font-size: 12px;"
        "}"
    );
}
