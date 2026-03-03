#include "ui/SharePasswordDialog.h"

#include "platform/WindowLevel.h"
#include "share/ShareUploadClient.h"
#include "ui/DesignTokens.h"
#include "utils/DialogThemeUtils.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

SharePasswordDialog::SharePasswordDialog(QWidget* parent)
    : QDialog(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setModal(true);
    setWindowModality(Qt::ApplicationModal);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setObjectName("dialogRoot");
#ifdef Q_OS_MACOS
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
#endif

    setupUi();
    applyTheme();

    setFixedWidth(420);
    setMinimumHeight(220);
}

SharePasswordDialog::~SharePasswordDialog() = default;

void SharePasswordDialog::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* titleBar = new QWidget(this);
    titleBar->setObjectName("titleBar");
    titleBar->setFixedHeight(72);

    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(12, 12, 12, 12);
    titleLayout->setSpacing(12);

    m_iconLabel = new QLabel(tr("URL"), titleBar);
    m_iconLabel->setObjectName("iconLabel");
    m_iconLabel->setFixedSize(56, 56);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    titleLayout->addWidget(m_iconLabel);

    auto* infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(4);

    m_titleLabel = new QLabel(tr("Share URL"), titleBar);
    m_titleLabel->setObjectName("titleLabel");
    infoLayout->addWidget(m_titleLabel);

    m_subtitleLabel = new QLabel(tr("Set an optional password"), titleBar);
    m_subtitleLabel->setObjectName("subtitleLabel");
    infoLayout->addWidget(m_subtitleLabel);

    titleLayout->addLayout(infoLayout);
    titleLayout->addStretch();
    mainLayout->addWidget(titleBar);

    auto* contentPanel = new QWidget(this);
    contentPanel->setObjectName("contentPanel");
    auto* contentLayout = new QVBoxLayout(contentPanel);
    contentLayout->setContentsMargins(14, 14, 14, 14);
    contentLayout->setSpacing(10);

    m_passwordEdit = new QLineEdit(contentPanel);
    m_passwordEdit->setObjectName("passwordEdit");
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(tr("No password"));
    m_passwordEdit->setClearButtonEnabled(true);
    m_passwordEdit->setMaxLength(ShareUploadClient::kMaxPasswordLength);
    contentLayout->addWidget(m_passwordEdit);

    m_hintLabel = new QLabel(
        tr("Leave empty for public access. Link expires in 24 hours."),
        contentPanel);
    m_hintLabel->setObjectName("hintLabel");
    m_hintLabel->setWordWrap(true);
    contentLayout->addWidget(m_hintLabel);

    mainLayout->addWidget(contentPanel, 1);

    auto* buttonBar = new QWidget(this);
    buttonBar->setObjectName("buttonBar");
    buttonBar->setFixedHeight(56);

    auto* buttonLayout = new QHBoxLayout(buttonBar);
    buttonLayout->setContentsMargins(12, 8, 12, 8);
    buttonLayout->setSpacing(8);

    m_cancelButton = new QPushButton(tr("Cancel"), buttonBar);
    m_cancelButton->setObjectName("cancelButton");
    m_cancelButton->setFixedHeight(40);
    connect(m_cancelButton, &QPushButton::clicked, this, &SharePasswordDialog::onCancelClicked);
    buttonLayout->addWidget(m_cancelButton, 1);

    m_shareButton = new QPushButton(tr("Share"), buttonBar);
    m_shareButton->setObjectName("shareButton");
    m_shareButton->setFixedHeight(40);
    m_shareButton->setDefault(true);
    connect(m_shareButton, &QPushButton::clicked, this, &SharePasswordDialog::onShareClicked);
    buttonLayout->addWidget(m_shareButton, 1);

    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &SharePasswordDialog::onShareClicked);

    mainLayout->addWidget(buttonBar);
}

void SharePasswordDialog::applyTheme()
{
    using namespace SnapTray;
    auto palette = DialogTheme::paletteForToolbarStyle();

    // Bridge success colors to DesignTokens
    const auto& tokens = DesignTokens::forStyle(
        DialogTheme::isLightToolbarStyle() ? ToolbarStyleType::Light : ToolbarStyleType::Dark);
    palette.successBackground = tokens.successAccent;
    palette.successBorder = tokens.successAccent.lighter(120);

    QString css = DialogTheme::baseStylesheet(palette);
    css += QStringLiteral(R"(
        #iconLabel { font-size: 16px; }
        #contentPanel { background-color: %1; }
        #hintLabel { color: %2; font-size: 12px; }
        #shareButton {
            background-color: %3; border-color: %3; color: %4;
        }
        #shareButton:hover {
            background-color: %5; border-color: %5;
        }
        #shareButton:pressed {
            background-color: %6; border-color: %6;
        }
    )")
    .arg(DialogTheme::toCssColor(palette.windowBackground))      // %1
    .arg(DialogTheme::toCssColor(palette.textSecondary))          // %2
    .arg(DialogTheme::toCssColor(palette.accentBackground))       // %3
    .arg(DialogTheme::toCssColor(palette.successText))            // %4
    .arg(DialogTheme::toCssColor(palette.accentHoverBackground))  // %5
    .arg(DialogTheme::toCssColor(palette.accentPressedBackground)); // %6
    setStyleSheet(css);
}

QString SharePasswordDialog::password() const
{
    return m_passwordEdit ? m_passwordEdit->text() : QString();
}

void SharePasswordDialog::setPassword(const QString& password)
{
    if (m_passwordEdit) {
        m_passwordEdit->setText(password);
    }
}

void SharePasswordDialog::showAt(const QPoint& pos)
{
    adjustSize();

    if (pos.isNull()) {
        QScreen* screen = parentWidget() ? parentWidget()->screen() : QApplication::primaryScreen();
        if (screen) {
            const QRect screenGeometry = screen->geometry();
            move(screenGeometry.center() - rect().center());
        }
    } else {
        move(pos);
    }
}

void SharePasswordDialog::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && event->position().y() <= 72.0) {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QDialog::mousePressEvent(event);
}

void SharePasswordDialog::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }
    QDialog::mouseMoveEvent(event);
}

void SharePasswordDialog::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
    }
    QDialog::mouseReleaseEvent(event);
}

void SharePasswordDialog::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        reject();
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

void SharePasswordDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    auto bringToFront = [this]() {
        raiseWindowAboveOverlays(this);
        raise();
        activateWindow();
    };
    bringToFront();
    QTimer::singleShot(0, this, bringToFront);
    QTimer::singleShot(80, this, bringToFront);
    if (m_passwordEdit) {
        m_passwordEdit->setFocus();
    }
}

void SharePasswordDialog::onCancelClicked()
{
    reject();
}

void SharePasswordDialog::onShareClicked()
{
    accept();
}
