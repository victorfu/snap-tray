#include "ui/SharePasswordDialog.h"

#include "platform/WindowLevel.h"
#include "share/ShareUploadClient.h"
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
    const SnapTray::DialogTheme::Palette palette = SnapTray::DialogTheme::paletteForToolbarStyle();

    setStyleSheet(QStringLiteral(R"(
        SharePasswordDialog {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 10px;
        }

        #titleBar {
            background-color: %3;
            border-top-left-radius: 10px;
            border-top-right-radius: 10px;
            border-bottom: 1px solid %2;
        }

        #iconLabel {
            background-color: %4;
            border: 1px solid %5;
            border-radius: 4px;
            color: %6;
            font-size: 16px;
            font-weight: bold;
        }

        #titleLabel {
            color: %7;
            font-size: 14px;
            font-weight: 700;
        }

        #subtitleLabel {
            color: %6;
            font-size: 12px;
        }

        #contentPanel {
            background-color: %1;
        }

        #passwordEdit {
            background-color: %8;
            color: %7;
            border: 1px solid %9;
            border-radius: 6px;
            padding: 8px;
            font-size: 13px;
            selection-background-color: %10;
        }

        #hintLabel {
            color: %6;
            font-size: 12px;
        }

        #buttonBar {
            background-color: %3;
            border-bottom-left-radius: 10px;
            border-bottom-right-radius: 10px;
        }

        QPushButton {
            background-color: %11;
            color: %12;
            border: 1px solid %5;
            border-radius: 6px;
            font-size: 13px;
            font-weight: 500;
            padding: 8px 16px;
        }

        QPushButton:hover {
            background-color: %13;
            border-color: %2;
        }

        QPushButton:pressed {
            background-color: %14;
        }

        #shareButton {
            background-color: %15;
            border-color: %15;
            color: %16;
        }

        #shareButton:hover {
            background-color: %17;
            border-color: %17;
        }

        #shareButton:pressed {
            background-color: %18;
            border-color: %18;
        }
    )")
        .arg(SnapTray::DialogTheme::toCssColor(palette.windowBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.border))
        .arg(SnapTray::DialogTheme::toCssColor(palette.titleBarBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.panelBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.controlBorder))
        .arg(SnapTray::DialogTheme::toCssColor(palette.textSecondary))
        .arg(SnapTray::DialogTheme::toCssColor(palette.textPrimary))
        .arg(SnapTray::DialogTheme::toCssColor(palette.inputBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.inputBorder))
        .arg(SnapTray::DialogTheme::toCssColor(palette.selectionBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.buttonBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.buttonText))
        .arg(SnapTray::DialogTheme::toCssColor(palette.buttonHoverBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.buttonPressedBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.accentBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.successText))
        .arg(SnapTray::DialogTheme::toCssColor(palette.accentHoverBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.accentPressedBackground)));
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
