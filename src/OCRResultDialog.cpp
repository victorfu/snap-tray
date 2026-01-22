#include "OCRResultDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QApplication>
#include <QClipboard>
#include <QScreen>
#include <QTimer>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCloseEvent>

OCRResultDialog::OCRResultDialog(QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint)
    , m_iconLabel(nullptr)
    , m_titleLabel(nullptr)
    , m_charCountLabel(nullptr)
    , m_textEdit(nullptr)
    , m_closeButton(nullptr)
    , m_copyButton(nullptr)
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

OCRResultDialog::~OCRResultDialog() = default;

void OCRResultDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Title bar (draggable area) - 72px
    auto *titleBar = new QWidget(this);
    titleBar->setFixedHeight(72);
    titleBar->setObjectName("titleBar");

    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(12, 12, 12, 12);
    titleLayout->setSpacing(12);

    // Icon placeholder (56x56) - shows "OCR" text
    m_iconLabel = new QLabel("OCR", titleBar);
    m_iconLabel->setFixedSize(56, 56);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setObjectName("iconLabel");
    titleLayout->addWidget(m_iconLabel);

    // Title and character count
    auto *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(4);

    m_titleLabel = new QLabel("OCR Result", titleBar);
    m_titleLabel->setObjectName("titleLabel");
    infoLayout->addWidget(m_titleLabel);

    m_charCountLabel = new QLabel("0 characters", titleBar);
    m_charCountLabel->setObjectName("charCountLabel");
    infoLayout->addWidget(m_charCountLabel);

    titleLayout->addLayout(infoLayout);
    titleLayout->addStretch();

    mainLayout->addWidget(titleBar);

    // Text edit area
    m_textEdit = new QTextEdit(this);
    m_textEdit->setObjectName("textEdit");
    m_textEdit->setAcceptRichText(false);
    m_textEdit->setPlaceholderText("No text recognized");
    connect(m_textEdit, &QTextEdit::textChanged, this, &OCRResultDialog::onTextChanged);
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
    connect(m_closeButton, &QPushButton::clicked, this, &OCRResultDialog::onCloseClicked);
    buttonLayout->addWidget(m_closeButton, 1);

    m_copyButton = new QPushButton("Copy", buttonBar);
    m_copyButton->setObjectName("copyButton");
    m_copyButton->setFixedHeight(40);
    connect(m_copyButton, &QPushButton::clicked, this, &OCRResultDialog::onCopyClicked);
    buttonLayout->addWidget(m_copyButton, 1);

    mainLayout->addWidget(buttonBar);
}

void OCRResultDialog::applyDarkTheme()
{
    setStyleSheet(R"(
        OCRResultDialog {
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

        #iconLabel {
            background-color: #3a3a3a;
            border: 1px solid #4a4a4a;
            border-radius: 4px;
            color: #9e9e9e;
            font-size: 18px;
            font-weight: bold;
        }

        #titleLabel {
            color: #e0e0e0;
            font-size: 14px;
            font-weight: bold;
        }

        #charCountLabel {
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

void OCRResultDialog::setResultText(const QString &text)
{
    m_originalText = text;
    m_isTextEdited = false;

    m_textEdit->setPlainText(text);
    updateCharacterCount();

    // Select all for easy editing
    m_textEdit->selectAll();
    m_textEdit->setFocus();
}

QString OCRResultDialog::resultText() const
{
    return m_textEdit->toPlainText();
}

void OCRResultDialog::showAt(const QPoint &pos)
{
    if (pos.isNull()) {
        // Center on screen
        QScreen *screen = QApplication::primaryScreen();
        if (screen) {
            QRect screenGeometry = screen->geometry();
            move(screenGeometry.center() - rect().center());
        }
    } else {
        move(pos);
    }

    show();
    raise();
    activateWindow();
    m_textEdit->setFocus();
}

void OCRResultDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // Check if click is in title bar area (top 72px)
        if (event->pos().y() < 72) {
            m_isDragging = true;
            m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void OCRResultDialog::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void OCRResultDialog::keyPressEvent(QKeyEvent *event)
{
    // Escape - close dialog
    if (event->key() == Qt::Key_Escape) {
        close();
        event->accept();
        return;
    }

    // Ctrl+C - copy all text (if no selection)
    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_C) {
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

void OCRResultDialog::closeEvent(QCloseEvent *event)
{
    emit dialogClosed();
    QWidget::closeEvent(event);
}

void OCRResultDialog::onCopyClicked()
{
    QString text = resultText();
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

void OCRResultDialog::onCloseClicked()
{
    close();
}

void OCRResultDialog::onTextChanged()
{
    QString currentText = m_textEdit->toPlainText();
    m_isTextEdited = (currentText != m_originalText);
    updateCharacterCount();
}

void OCRResultDialog::updateCharacterCount()
{
    int count = m_textEdit->toPlainText().length();
    QString countText = QString("%1 character%2").arg(count).arg(count != 1 ? "s" : "");

    if (m_isTextEdited) {
        countText += " (edited)";
    }

    m_charCountLabel->setText(countText);
}

void OCRResultDialog::showCopyFeedback()
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
