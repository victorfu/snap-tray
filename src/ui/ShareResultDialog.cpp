#include "ui/ShareResultDialog.h"

#include "platform/WindowLevel.h"
#include "utils/DialogThemeUtils.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QUrl>
#include <QVBoxLayout>

ShareResultDialog::ShareResultDialog(QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setupUi();
    applyTheme();

    setFixedWidth(460);
    setMinimumHeight(280);
}

ShareResultDialog::~ShareResultDialog() = default;

void ShareResultDialog::setupUi()
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

    m_titleLabel = new QLabel(tr("Share URL Ready"), titleBar);
    m_titleLabel->setObjectName("titleLabel");
    infoLayout->addWidget(m_titleLabel);

    m_subtitleLabel = new QLabel(tr("Upload completed"), titleBar);
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

    m_urlCaptionLabel = new QLabel(tr("Share Link"), contentPanel);
    m_urlCaptionLabel->setObjectName("fieldLabel");
    contentLayout->addWidget(m_urlCaptionLabel);

    m_urlEdit = new QLineEdit(contentPanel);
    m_urlEdit->setObjectName("urlEdit");
    m_urlEdit->setReadOnly(true);
    m_urlEdit->setPlaceholderText(tr("No URL"));
    m_urlEdit->setClearButtonEnabled(false);
    contentLayout->addWidget(m_urlEdit);

    m_passwordCaptionLabel = new QLabel(tr("Password"), contentPanel);
    m_passwordCaptionLabel->setObjectName("fieldLabel");
    m_passwordCaptionLabel->setVisible(false);
    contentLayout->addWidget(m_passwordCaptionLabel);

    m_passwordEdit = new QLineEdit(contentPanel);
    m_passwordEdit->setObjectName("passwordEdit");
    m_passwordEdit->setReadOnly(true);
    m_passwordEdit->setClearButtonEnabled(false);
    m_passwordEdit->setVisible(false);
    contentLayout->addWidget(m_passwordEdit);

    auto* badgeRow = new QHBoxLayout();
    badgeRow->setContentsMargins(0, 0, 0, 0);
    badgeRow->setSpacing(8);

    m_expiresLabel = new QLabel(contentPanel);
    m_expiresLabel->setObjectName("metaBadge");
    m_expiresLabel->setVisible(false);
    badgeRow->addWidget(m_expiresLabel);

    m_protectedLabel = new QLabel(contentPanel);
    m_protectedLabel->setObjectName("metaBadge");
    m_protectedLabel->setVisible(false);
    badgeRow->addWidget(m_protectedLabel);
    badgeRow->addStretch();

    contentLayout->addLayout(badgeRow);

    m_hintLabel = new QLabel(tr("Use Copy to share now, or Open to verify in browser."), contentPanel);
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

    m_closeButton = new QPushButton(tr("Close"), buttonBar);
    m_closeButton->setObjectName("closeButton");
    m_closeButton->setFixedHeight(40);
    connect(m_closeButton, &QPushButton::clicked, this, &ShareResultDialog::onCloseClicked);
    buttonLayout->addWidget(m_closeButton, 1);

    m_copyButton = new QPushButton(tr("Copy"), buttonBar);
    m_copyButton->setObjectName("copyButton");
    m_copyButton->setFixedHeight(40);
    connect(m_copyButton, &QPushButton::clicked, this, &ShareResultDialog::onCopyClicked);
    buttonLayout->addWidget(m_copyButton, 1);

    m_openButton = new QPushButton(tr("Open"), buttonBar);
    m_openButton->setObjectName("openButton");
    m_openButton->setFixedHeight(40);
    connect(m_openButton, &QPushButton::clicked, this, &ShareResultDialog::onOpenClicked);
    buttonLayout->addWidget(m_openButton, 1);

    mainLayout->addWidget(buttonBar);
}

void ShareResultDialog::applyTheme()
{
    const SnapTray::DialogTheme::Palette palette = SnapTray::DialogTheme::paletteForToolbarStyle();

    setStyleSheet(QStringLiteral(R"(
        ShareResultDialog {
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

        #fieldLabel {
            color: %6;
            font-size: 12px;
            font-weight: 500;
        }

        #urlEdit {
            background-color: %8;
            color: %7;
            border: 1px solid %9;
            border-radius: 6px;
            padding: 8px;
            font-family: 'Consolas', 'Courier New', monospace;
            font-size: 13px;
            selection-background-color: %10;
        }

        #passwordEdit {
            background-color: %8;
            color: %7;
            border: 1px solid %9;
            border-radius: 6px;
            padding: 8px;
            font-family: 'Consolas', 'Courier New', monospace;
            font-size: 13px;
            selection-background-color: %10;
        }

        #metaBadge {
            background-color: %4;
            color: %6;
            border: 1px solid %5;
            border-radius: 10px;
            font-size: 12px;
            padding: 4px 10px;
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

        #openButton {
            background-color: %15;
            border-color: %15;
            color: %16;
        }

        #openButton:hover {
            background-color: %17;
            border-color: %17;
        }

        #openButton:pressed {
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

void ShareResultDialog::setResult(const QString& url,
                                  const QDateTime& expiresAt,
                                  bool isProtected,
                                  const QString& password)
{
    const QString shareUrl = url.trimmed();
    m_sharePassword = password;
    m_urlEdit->setText(shareUrl);
    m_urlEdit->setCursorPosition(0);

    const bool hasUrl = !shareUrl.isEmpty();
    m_copyButton->setEnabled(hasUrl);
    m_openButton->setEnabled(hasUrl);

    const bool hasPassword = !m_sharePassword.isEmpty();
    if (hasPassword) {
        m_passwordCaptionLabel->setVisible(true);
        m_passwordEdit->setText(m_sharePassword);
        m_passwordEdit->setCursorPosition(0);
        m_passwordEdit->setVisible(true);
    } else {
        m_passwordEdit->clear();
        m_passwordEdit->setVisible(false);
        m_passwordCaptionLabel->setVisible(false);
    }

    if (expiresAt.isValid()) {
        const QString expiresText = QLocale::system().toString(expiresAt.toLocalTime(), QLocale::ShortFormat);
        m_expiresLabel->setText(tr("Expires: %1").arg(expiresText));
        m_expiresLabel->setVisible(true);
        m_subtitleLabel->setText(tr("Valid until %1").arg(expiresText));
    } else {
        m_expiresLabel->clear();
        m_expiresLabel->setVisible(false);
        m_subtitleLabel->setText(tr("Upload completed"));
    }

    if (isProtected && hasPassword) {
        m_protectedLabel->setText(tr("Password protected"));
    } else if (isProtected) {
        m_protectedLabel->setText(tr("Password protected (hidden)"));
    } else {
        m_protectedLabel->setText(tr("No password"));
    }
    m_protectedLabel->setVisible(true);

    m_hintLabel->setText(hasPassword
                             ? tr("Copy includes both link and password.")
                             : tr("Anyone with this link can access it until expiration."));
}

QString ShareResultDialog::url() const
{
    return m_urlEdit ? m_urlEdit->text().trimmed() : QString();
}

void ShareResultDialog::showAt(const QPoint& pos)
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

    show();
    raise();
    activateWindow();
}

void ShareResultDialog::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && event->position().y() <= 72.0) {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ShareResultDialog::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void ShareResultDialog::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void ShareResultDialog::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void ShareResultDialog::closeEvent(QCloseEvent* event)
{
    emit dialogClosed();
    QWidget::closeEvent(event);
}

void ShareResultDialog::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    raiseWindowAboveOverlays(this);
}

void ShareResultDialog::onCloseClicked()
{
    close();
}

void ShareResultDialog::onCopyClicked()
{
    const QString text = clipboardText();
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
        emit linkCopied(url());
    }
    close();
}

void ShareResultDialog::onOpenClicked()
{
    const QString shareUrl = url();
    if (!shareUrl.isEmpty()) {
        QDesktopServices::openUrl(QUrl(shareUrl));
        emit linkOpened(shareUrl);
    }
    close();
}

QString ShareResultDialog::clipboardText() const
{
    const QString shareUrl = url();
    if (shareUrl.isEmpty()) {
        return QString();
    }

    if (!m_sharePassword.isEmpty()) {
        return tr("%1\nPassword: %2").arg(shareUrl, m_sharePassword);
    }
    return shareUrl;
}
