#include "QRCodeResultDialog.h"
#include "platform/WindowLevel.h"
#include "utils/DialogThemeUtils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QClipboard>
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QShowEvent>
#include <QScreen>
#include <QTimer>
#include <QRegularExpression>

QRCodeResultDialog::QRCodeResultDialog(QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_thumbnailLabel(nullptr)
    , m_formatLabel(nullptr)
    , m_characterCountLabel(nullptr)
    , m_textEdit(nullptr)
    , m_closeButton(nullptr)
    , m_copyButton(nullptr)
    , m_openUrlButton(nullptr)
    , m_isDragging(false)
    , m_isTextEdited(false)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_TranslucentBackground, false);

    setupUi();
    applyTheme();

    setFixedWidth(420);
    setMinimumHeight(260);
    setMaximumHeight(480);
}

QRCodeResultDialog::~QRCodeResultDialog() = default;

void QRCodeResultDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Title bar (draggable area)
    auto *titleBar = new QWidget(this);
    titleBar->setFixedHeight(72);
    titleBar->setObjectName("titleBar");

    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(12, 12, 12, 12);
    titleLayout->setSpacing(12);

    // Thumbnail (56x56)
    m_thumbnailLabel = new QLabel(titleBar);
    m_thumbnailLabel->setFixedSize(56, 56);
    m_thumbnailLabel->setScaledContents(false);
    m_thumbnailLabel->setAlignment(Qt::AlignCenter);
    m_thumbnailLabel->setObjectName("thumbnail");
    titleLayout->addWidget(m_thumbnailLabel);

    // Format and character count
    auto *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(4);

    m_formatLabel = new QLabel("QR Code", titleBar);
    m_formatLabel->setObjectName("formatLabel");
    infoLayout->addWidget(m_formatLabel);

    m_characterCountLabel = new QLabel("0 characters", titleBar);
    m_characterCountLabel->setObjectName("characterCountLabel");
    infoLayout->addWidget(m_characterCountLabel);

    titleLayout->addLayout(infoLayout);
    titleLayout->addStretch();

    mainLayout->addWidget(titleBar);

    // Text edit area
    m_textEdit = new QTextEdit(this);
    m_textEdit->setObjectName("textEdit");
    m_textEdit->setAcceptRichText(false);
    m_textEdit->setPlaceholderText("No content");
    connect(m_textEdit, &QTextEdit::textChanged, this, &QRCodeResultDialog::onTextChanged);
    mainLayout->addWidget(m_textEdit, 1);

    // Button bar
    auto *buttonBar = new QWidget(this);
    buttonBar->setFixedHeight(56);
    buttonBar->setObjectName("buttonBar");

    auto *buttonLayout = new QHBoxLayout(buttonBar);
    buttonLayout->setContentsMargins(12, 8, 12, 8);
    buttonLayout->setSpacing(8);

    m_closeButton = new QPushButton("Close", buttonBar);
    m_closeButton->setObjectName("closeButton");
    m_closeButton->setFixedHeight(40);
    connect(m_closeButton, &QPushButton::clicked, this, &QRCodeResultDialog::onCloseClicked);
    buttonLayout->addWidget(m_closeButton, 1);

    m_copyButton = new QPushButton("Copy", buttonBar);
    m_copyButton->setObjectName("copyButton");
    m_copyButton->setFixedHeight(40);
    connect(m_copyButton, &QPushButton::clicked, this, &QRCodeResultDialog::onCopyClicked);
    buttonLayout->addWidget(m_copyButton, 1);

    m_openUrlButton = new QPushButton("Open URL", buttonBar);
    m_openUrlButton->setObjectName("openUrlButton");
    m_openUrlButton->setFixedHeight(40);
    m_openUrlButton->setVisible(false);
    connect(m_openUrlButton, &QPushButton::clicked, this, &QRCodeResultDialog::onOpenUrlClicked);
    buttonLayout->addWidget(m_openUrlButton, 1);

    mainLayout->addWidget(buttonBar);
}

void QRCodeResultDialog::applyTheme()
{
    const SnapTray::DialogTheme::Palette palette = SnapTray::DialogTheme::paletteForToolbarStyle();

    setStyleSheet(QStringLiteral(R"(
        QRCodeResultDialog {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 12px;
        }

        #titleBar {
            background-color: %3;
            border-top-left-radius: 12px;
            border-top-right-radius: 12px;
            border-bottom: 1px solid %2;
        }

        #thumbnail {
            background-color: %4;
            border: 1px solid %5;
            border-radius: 4px;
            color: %6;
            font-size: 18px;
            font-weight: bold;
        }

        #formatLabel {
            color: %7;
            font-size: 14px;
            font-weight: bold;
        }

        #characterCountLabel {
            color: %6;
            font-size: 12px;
        }

        #textEdit {
            background-color: %8;
            color: %7;
            border: 1px solid %9;
            border-radius: 6px;
            padding: 8px;
            font-family: 'Consolas', 'Courier New', monospace;
            font-size: 13px;
            selection-background-color: %10;
        }

        #buttonBar {
            background-color: %3;
            border-bottom-left-radius: 12px;
            border-bottom-right-radius: 12px;
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

        QPushButton:disabled {
            background-color: %15;
            color: %16;
            border-color: %17;
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
        .arg(SnapTray::DialogTheme::toCssColor(palette.buttonDisabledBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.buttonDisabledText))
        .arg(SnapTray::DialogTheme::toCssColor(palette.buttonDisabledBorder)));
}

void QRCodeResultDialog::setResult(const QString &text, const QString &format, const QPixmap &sourceImage)
{
    m_originalText = text;
    m_format = format;
    m_isTextEdited = false;

    // Set text
    m_textEdit->setPlainText(text);

    // Set format display
    m_formatLabel->setText(getFormatDisplayName(format));

    // Generate thumbnail from source image
    if (!sourceImage.isNull()) {
        m_thumbnail = sourceImage.scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_thumbnailLabel->setPixmap(m_thumbnail);
        m_thumbnailLabel->setText(QString());
    } else {
        m_thumbnailLabel->setPixmap(QPixmap());
        m_thumbnailLabel->setText("QR");
    }

    // Update character count
    updateCharacterCount();

    // Check for URL
    QString url = detectUrl(text);
    m_openUrlButton->setVisible(!url.isEmpty());
}

QString QRCodeResultDialog::currentText() const
{
    return m_textEdit->toPlainText();
}

void QRCodeResultDialog::showAt(const QPoint &pos)
{
    if (pos.isNull()) {
        // Center on screen
        QScreen *screen = QApplication::primaryScreen();
        if (screen) {
            QRect screenGeometry = screen->geometry();
            move(screenGeometry.center() - rect().center());
        }
    } else {
        // Show at specified position
        move(pos);
    }

    show();
    raise();
    activateWindow();
    m_textEdit->setFocus();
}

void QRCodeResultDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // Check if click is in title bar area
        if (event->pos().y() < 72) {
            m_isDragging = true;
            m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void QRCodeResultDialog::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void QRCodeResultDialog::keyPressEvent(QKeyEvent *event)
{
    // Escape - close dialog
    if (event->key() == Qt::Key_Escape) {
        close();
        event->accept();
        return;
    }

    // Ctrl+C - copy all text
    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_C) {
        // Only handle if no text is selected (otherwise let QTextEdit handle it)
        if (!m_textEdit->textCursor().hasSelection()) {
            onCopyClicked();
            event->accept();
            return;
        }
    }

    // Ctrl+Enter - copy and close
    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_Return) {
        onCopyClicked();
        close();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void QRCodeResultDialog::closeEvent(QCloseEvent *event)
{
    emit dialogClosed();
    QWidget::closeEvent(event);
}

void QRCodeResultDialog::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Ensure dialog appears above RegionSelector/ScreenCanvas on macOS
    raiseWindowAboveOverlays(this);
}

void QRCodeResultDialog::onCopyClicked()
{
    QString text = currentText();
    if (text.isEmpty()) {
        return;
    }

    QApplication::clipboard()->setText(text);
    emit textCopied(text);

    showCopyFeedback();

    // Close dialog after brief delay to show feedback
    QTimer::singleShot(500, this, [this]() {
        close();
    });
}

void QRCodeResultDialog::onOpenUrlClicked()
{
    QString url = detectUrl(currentText());
    if (url.isEmpty()) {
        return;
    }

    QDesktopServices::openUrl(QUrl(url));
    emit urlOpened(url);

    // Close dialog after opening URL
    QTimer::singleShot(200, this, [this]() {
        close();
    });
}

void QRCodeResultDialog::onCloseClicked()
{
    close();
}

void QRCodeResultDialog::onTextChanged()
{
    QString currentText = m_textEdit->toPlainText();
    m_isTextEdited = (currentText != m_originalText);

    updateCharacterCount();

    // Update URL button visibility
    QString url = detectUrl(currentText);
    m_openUrlButton->setVisible(!url.isEmpty());
}

void QRCodeResultDialog::updateCharacterCount()
{
    int count = m_textEdit->toPlainText().length();
    QString countText = QString("%1 character%2").arg(count).arg(count != 1 ? "s" : "");

    if (m_isTextEdited) {
        countText += " (edited)";
    }

    m_characterCountLabel->setText(countText);
}

void QRCodeResultDialog::showCopyFeedback()
{
    const SnapTray::DialogTheme::Palette palette = SnapTray::DialogTheme::paletteForToolbarStyle();
    QString originalText = m_copyButton->text();
    m_copyButton->setText("✓ Copied!");
    m_copyButton->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; border-color: %2; color: %3; }")
        .arg(SnapTray::DialogTheme::toCssColor(palette.successBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.successBorder))
        .arg(SnapTray::DialogTheme::toCssColor(palette.successText)));

    // Reset after 1.5 seconds
    QTimer::singleShot(1500, this, [this, originalText]() {
        if (m_copyButton) {
            m_copyButton->setText(originalText);
            m_copyButton->setStyleSheet("");
        }
    });
}

QString QRCodeResultDialog::detectUrl(const QString &text) const
{
    // Simple URL detection (http/https)
    QRegularExpression urlRegex(
        R"(https?://[^\s<>"{}|\\^`\[\]]+)",
        QRegularExpression::CaseInsensitiveOption
    );

    auto match = urlRegex.match(text);
    if (match.hasMatch()) {
        QString url = match.captured(0);
        // Validate that it's a proper URL
        if (isValidUrl(url)) {
            return url;
        }
    }

    return QString();
}

bool QRCodeResultDialog::isValidUrl(const QString &urlString) const
{
    if (urlString.isEmpty()) {
        return false;
    }

    QUrl url(urlString);

    // Check if URL is valid and has http/https scheme
    if (!url.isValid()) {
        return false;
    }

    QString scheme = url.scheme().toLower();
    if (scheme != "http" && scheme != "https") {
        return false;
    }

    // Check if URL has a valid host
    if (url.host().isEmpty()) {
        return false;
    }

    return true;
}

QString QRCodeResultDialog::getFormatDisplayName(const QString &format) const
{
    static const QMap<QString, QString> formatNames = {
        {"QR_CODE", "QR Code"},
        {"DATA_MATRIX", "Data Matrix"},
        {"AZTEC", "Aztec Code"},
        {"PDF_417", "PDF417"},
        {"EAN_8", "EAN-8"},
        {"EAN_13", "EAN-13"},
        {"UPC_A", "UPC-A"},
        {"UPC_E", "UPC-E"},
        {"CODE_39", "Code 39"},
        {"CODE_93", "Code 93"},
        {"CODE_128", "Code 128"},
        {"ITF", "ITF"},
        {"CODABAR", "Codabar"}
    };

    return formatNames.value(format, "Barcode");
}
