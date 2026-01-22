#include "QRCodeResultDialog.h"

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
#include <QScreen>
#include <QTimer>
#include <QRegularExpression>

QRCodeResultDialog::QRCodeResultDialog(QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint)
    , m_thumbnailLabel(nullptr)
    , m_formatLabel(nullptr)
    , m_characterCountLabel(nullptr)
    , m_textEdit(nullptr)
    , m_copyButton(nullptr)
    , m_openUrlButton(nullptr)
    , m_isDragging(false)
    , m_isTextEdited(false)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_TranslucentBackground, false);

    setupUi();
    applyDarkTheme();

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

void QRCodeResultDialog::applyDarkTheme()
{
    setStyleSheet(R"(
        QRCodeResultDialog {
            background-color: #2b2b2b;
            border: 1px solid #3d3d3d;
            border-radius: 12px;
        }

        #titleBar {
            background-color: #2b2b2b;
            border-top-left-radius: 12px;
            border-top-right-radius: 12px;
            border-bottom: 1px solid #3d3d3d;
        }

        #thumbnail {
            background-color: #3a3a3a;
            border: 1px solid #4a4a4a;
            border-radius: 4px;
        }

        #formatLabel {
            color: #e0e0e0;
            font-size: 14px;
            font-weight: bold;
        }

        #characterCountLabel {
            color: #9e9e9e;
            font-size: 12px;
        }

        #textEdit {
            background-color: #1e1e1e;
            color: #e0e0e0;
            border: 1px solid #3d3d3d;
            border-radius: 6px;
            padding: 8px;
            font-family: 'Consolas', 'Courier New', monospace;
            font-size: 13px;
            selection-background-color: #264f78;
        }

        #buttonBar {
            background-color: #2b2b2b;
            border-bottom-left-radius: 12px;
            border-bottom-right-radius: 12px;
        }

        QPushButton {
            background-color: #3a3a3a;
            color: #e0e0e0;
            border: 1px solid #4a4a4a;
            border-radius: 6px;
            font-size: 13px;
            font-weight: 500;
            padding: 8px 16px;
        }

        QPushButton:hover {
            background-color: #4a4a4a;
            border-color: #5a5a5a;
        }

        QPushButton:pressed {
            background-color: #333333;
        }

        QPushButton:disabled {
            background-color: #2a2a2a;
            color: #666666;
            border-color: #3a3a3a;
        }
    )");
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
    } else {
        m_thumbnailLabel->setText("QR");
        m_thumbnailLabel->setStyleSheet(m_thumbnailLabel->styleSheet() +
            "QLabel { color: #9e9e9e; font-size: 18px; font-weight: bold; }");
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

void QRCodeResultDialog::onCopyClicked()
{
    QString text = currentText();
    if (text.isEmpty()) {
        return;
    }

    QApplication::clipboard()->setText(text);
    emit textCopied(text);

    showCopyFeedback();
}

void QRCodeResultDialog::onOpenUrlClicked()
{
    QString url = detectUrl(currentText());
    if (url.isEmpty()) {
        return;
    }

    QDesktopServices::openUrl(QUrl(url));
    emit urlOpened(url);
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
    QString originalText = m_copyButton->text();
    m_copyButton->setText("✓ Copied!");
    m_copyButton->setStyleSheet(
        "QPushButton { background-color: #4caf50; border-color: #66bb6a; }"
    );

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
        return match.captured(0);
    }

    return QString();
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
