#include "OCRResultDialog.h"
#include "detection/TableDetector.h"
#include "platform/WindowLevel.h"
#include "utils/DialogThemeUtils.h"

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
#include <QShowEvent>
#include <QPointer>

OCRResultDialog::OCRResultDialog(QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_iconLabel(nullptr)
    , m_titleLabel(nullptr)
    , m_charCountLabel(nullptr)
    , m_textEdit(nullptr)
    , m_closeButton(nullptr)
    , m_copyButton(nullptr)
    , m_copyAsTsvButton(nullptr)
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
    m_iconLabel = new QLabel(tr("OCR"), titleBar);
    m_iconLabel->setFixedSize(56, 56);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setObjectName("iconLabel");
    titleLayout->addWidget(m_iconLabel);

    // Title and character count
    auto *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(4);

    m_titleLabel = new QLabel(tr("OCR Result"), titleBar);
    m_titleLabel->setObjectName("titleLabel");
    infoLayout->addWidget(m_titleLabel);

    m_charCountLabel = new QLabel(tr("0 characters"), titleBar);
    m_charCountLabel->setObjectName("charCountLabel");
    infoLayout->addWidget(m_charCountLabel);

    titleLayout->addLayout(infoLayout);
    titleLayout->addStretch();

    mainLayout->addWidget(titleBar);

    // Text edit area
    m_textEdit = new QTextEdit(this);
    m_textEdit->setObjectName("textEdit");
    m_textEdit->setAcceptRichText(false);
    m_textEdit->setPlaceholderText(tr("No text recognized"));
    connect(m_textEdit, &QTextEdit::textChanged, this, &OCRResultDialog::onTextChanged);
    mainLayout->addWidget(m_textEdit, 1);

    // Button bar
    auto *buttonBar = new QWidget(this);
    buttonBar->setFixedHeight(56);
    buttonBar->setObjectName("buttonBar");

    auto *buttonLayout = new QHBoxLayout(buttonBar);
    buttonLayout->setContentsMargins(12, 8, 12, 8);
    buttonLayout->setSpacing(8);

    m_closeButton = new QPushButton(tr("Close"), buttonBar);
    m_closeButton->setObjectName("closeButton");
    m_closeButton->setFixedHeight(40);
    connect(m_closeButton, &QPushButton::clicked, this, &OCRResultDialog::onCloseClicked);
    buttonLayout->addWidget(m_closeButton, 1);

    m_copyButton = new QPushButton(tr("Copy"), buttonBar);
    m_copyButton->setObjectName("copyButton");
    m_copyButton->setFixedHeight(40);
    connect(m_copyButton, &QPushButton::clicked, this, &OCRResultDialog::onCopyClicked);
    buttonLayout->addWidget(m_copyButton, 1);

    m_copyAsTsvButton = new QPushButton(tr("Copy as TSV"), buttonBar);
    m_copyAsTsvButton->setObjectName("copyAsTsvButton");
    m_copyAsTsvButton->setFixedHeight(40);
    m_copyAsTsvButton->setVisible(false);
    connect(m_copyAsTsvButton, &QPushButton::clicked, this, &OCRResultDialog::onCopyAsTsvClicked);
    buttonLayout->addWidget(m_copyAsTsvButton, 1);

    mainLayout->addWidget(buttonBar);
}

void OCRResultDialog::applyTheme()
{
    const SnapTray::DialogTheme::Palette palette = SnapTray::DialogTheme::paletteForToolbarStyle();

    setStyleSheet(QStringLiteral(R"(
        OCRResultDialog {
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

        #iconLabel {
            background-color: %4;
            border: 1px solid %5;
            border-radius: 4px;
            color: %6;
            font-size: 18px;
            font-weight: bold;
        }

        #titleLabel {
            color: %7;
            font-size: 14px;
            font-weight: bold;
        }

        #charCountLabel {
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

void OCRResultDialog::setResultText(const QString &text)
{
    m_originalText = text;
    m_detectedTsv.clear();
    m_isTextEdited = false;

    m_textEdit->setPlainText(text);
    updateCharacterCount();

    if (m_copyAsTsvButton) {
        m_copyAsTsvButton->setVisible(false);
    }

    // Select all for easy editing
    m_textEdit->selectAll();
    m_textEdit->setFocus();
}

void OCRResultDialog::setOCRResult(const OCRResult &result)
{
    setResultText(result.text);

    const TableDetectionResult detection = TableDetector::detect(result.blocks);
    m_detectedTsv = detection.tsv;

    if (m_copyAsTsvButton) {
        const bool showTsvButton = detection.isTable && !m_detectedTsv.isEmpty();
        m_copyAsTsvButton->setVisible(showTsvButton);
        m_copyAsTsvButton->setEnabled(showTsvButton);
    }
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

void OCRResultDialog::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Ensure dialog appears above RegionSelector/ScreenCanvas on macOS
    raiseWindowAboveOverlays(this);
}

void OCRResultDialog::onCopyClicked()
{
    QString text = resultText();
    if (text.isEmpty()) {
        return;
    }

    QApplication::clipboard()->setText(text);
    emit textCopied(text);

    showCopyFeedback(m_copyButton, tr("✓ Copied!"));

    // Close dialog after brief delay to show feedback
    QTimer::singleShot(500, this, [this]() {
        close();
    });
}

void OCRResultDialog::onCopyAsTsvClicked()
{
    if (m_detectedTsv.isEmpty()) {
        return;
    }

    QApplication::clipboard()->setText(m_detectedTsv);
    emit textCopied(m_detectedTsv);

    showCopyFeedback(m_copyAsTsvButton, tr("✓ Copied TSV!"));

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
    const int count = m_textEdit->toPlainText().length();
    QString countText = (count == 1)
        ? tr("%1 character").arg(count)
        : tr("%1 characters").arg(count);

    if (m_isTextEdited) {
        countText = tr("%1 (edited)").arg(countText);
    }

    m_charCountLabel->setText(countText);
}

void OCRResultDialog::showCopyFeedback(QPushButton *button, const QString &feedbackText)
{
    if (!button) {
        return;
    }

    const SnapTray::DialogTheme::Palette palette = SnapTray::DialogTheme::paletteForToolbarStyle();
    const QString originalText = button->text();
    QPointer<QPushButton> safeButton = button;
    safeButton->setText(feedbackText);
    safeButton->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; border-color: %2; color: %3; }")
        .arg(SnapTray::DialogTheme::toCssColor(palette.successBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.successBorder))
        .arg(SnapTray::DialogTheme::toCssColor(palette.successText)));

    // Reset after 1.5 seconds
    QTimer::singleShot(1500, this, [safeButton, originalText]() {
        if (safeButton) {
            safeButton->setText(originalText);
            safeButton->setStyleSheet("");
        }
    });
}
