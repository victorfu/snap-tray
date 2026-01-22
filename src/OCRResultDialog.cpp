#include "OCRResultDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QKeyEvent>
#include <QShortcut>

OCRResultDialog::OCRResultDialog(QWidget* parent)
    : QDialog(parent)
    , m_textEdit(nullptr)
    , m_copyBtn(nullptr)
    , m_cancelBtn(nullptr)
    , m_charCountLabel(nullptr)
{
    setupUi();
}

void OCRResultDialog::setupUi()
{
    setWindowTitle(tr("OCR Result"));
    setMinimumSize(400, 300);
    resize(500, 400);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // Instruction label
    QLabel* instructionLabel = new QLabel(tr("Review and edit the recognized text:"), this);
    mainLayout->addWidget(instructionLabel);

    // Text editor
    m_textEdit = new QTextEdit(this);
    m_textEdit->setAcceptRichText(false);
    m_textEdit->setPlaceholderText(tr("OCR result will appear here..."));
    m_textEdit->setTabChangesFocus(true);
    mainLayout->addWidget(m_textEdit, 1);  // stretch factor 1

    // Character count label
    m_charCountLabel = new QLabel(this);
    m_charCountLabel->setStyleSheet("color: gray; font-size: 12px;");
    mainLayout->addWidget(m_charCountLabel);

    // Button layout
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_cancelBtn->setMinimumWidth(80);
    buttonLayout->addWidget(m_cancelBtn);

    m_copyBtn = new QPushButton(tr("Copy"), this);
    m_copyBtn->setMinimumWidth(80);
    m_copyBtn->setDefault(true);
    buttonLayout->addWidget(m_copyBtn);

    mainLayout->addLayout(buttonLayout);

    // Connections
    connect(m_textEdit, &QTextEdit::textChanged, this, &OCRResultDialog::onTextChanged);
    connect(m_copyBtn, &QPushButton::clicked, this, &OCRResultDialog::onCopyClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    // Keyboard shortcuts
    // Ctrl+Enter to copy (cross-platform: Cmd+Enter on macOS)
    QShortcut* copyShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(copyShortcut, &QShortcut::activated, this, &OCRResultDialog::onCopyClicked);

    // Also support Ctrl+Shift+C as alternative
    QShortcut* copyShortcut2 = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), this);
    connect(copyShortcut2, &QShortcut::activated, this, &OCRResultDialog::onCopyClicked);

    updateCharacterCount();
}

void OCRResultDialog::setResultText(const QString& text)
{
    m_textEdit->setPlainText(text);
    m_textEdit->selectAll();  // Select all for easy replacement
    m_textEdit->setFocus();
}

QString OCRResultDialog::resultText() const
{
    return m_textEdit->toPlainText();
}

void OCRResultDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    m_textEdit->setFocus();
}

void OCRResultDialog::keyPressEvent(QKeyEvent* event)
{
    // Escape to cancel
    if (event->key() == Qt::Key_Escape) {
        reject();
        return;
    }
    QDialog::keyPressEvent(event);
}

void OCRResultDialog::onCopyClicked()
{
    QString text = m_textEdit->toPlainText();
    if (!text.isEmpty()) {
        emit copyRequested(text);
    }
    accept();
}

void OCRResultDialog::onTextChanged()
{
    updateCharacterCount();
}

void OCRResultDialog::updateCharacterCount()
{
    int count = m_textEdit->toPlainText().length();
    m_charCountLabel->setText(tr("%n character(s)", "", count));
}
