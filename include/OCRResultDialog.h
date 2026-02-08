#ifndef OCRRESULTDIALOG_H
#define OCRRESULTDIALOG_H

#include <QWidget>
#include <QString>
#include <QPoint>

class QTextEdit;
class QPushButton;
class QLabel;
struct OCRResult;

/**
 * @brief Dialog displaying OCR recognition results with interactive features.
 *
 * Features:
 * - Editable text area showing recognized text
 * - Copy button with visual feedback ("✓ Copied!")
 * - Optional "Copy as TSV" button when table structure is detected
 * - Character count with "(edited)" indicator
 * - Theme-aware UI matching SnapTray style (light/dark)
 * - Draggable title bar
 * - Keyboard shortcuts: Escape, Ctrl+C, Ctrl+Enter
 */
class OCRResultDialog : public QWidget
{
    Q_OBJECT

public:
    explicit OCRResultDialog(QWidget *parent = nullptr);
    ~OCRResultDialog() override;

    /**
     * @brief Set the OCR result text to display
     * @param text The recognized text from OCR
     */
    void setResultText(const QString &text);
    void setOCRResult(const OCRResult &result);

    /**
     * @brief Get the current text (may be edited by user)
     */
    QString resultText() const;

    /**
     * @brief Show dialog at specified position
     * @param pos Screen position, centers on screen if not specified
     */
    void showAt(const QPoint &pos = QPoint());

signals:
    /**
     * @brief Emitted when text is copied to clipboard
     */
    void textCopied(const QString &text);

    /**
     * @brief Emitted when dialog is closed
     */
    void dialogClosed();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void onCopyClicked();
    void onCopyAsTsvClicked();
    void onCloseClicked();
    void onTextChanged();

private:
    void setupUi();
    void applyTheme();
    void updateCharacterCount();
    void showCopyFeedback(QPushButton *button, const QString &feedbackText);

    // UI Components
    QLabel *m_iconLabel;
    QLabel *m_titleLabel;
    QLabel *m_charCountLabel;
    QTextEdit *m_textEdit;
    QPushButton *m_closeButton;
    QPushButton *m_copyButton;
    QPushButton *m_copyAsTsvButton;

    // State
    QString m_originalText;
    QString m_detectedTsv;
    QPoint m_dragPosition;
    bool m_isDragging;
    bool m_isTextEdited;
};

#endif // OCRRESULTDIALOG_H
