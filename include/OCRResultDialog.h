#ifndef OCRRESULTDIALOG_H
#define OCRRESULTDIALOG_H

#include <QDialog>

class QTextEdit;
class QPushButton;
class QLabel;

/**
 * @brief Dialog for displaying and editing OCR recognition results.
 *
 * Allows users to review, edit, and copy OCR-recognized text
 * before it goes to the clipboard.
 */
class OCRResultDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OCRResultDialog(QWidget* parent = nullptr);
    ~OCRResultDialog() override = default;

    /**
     * @brief Set the OCR result text to display.
     * @param text The recognized text from OCR
     */
    void setResultText(const QString& text);

    /**
     * @brief Get the current text (possibly edited by user).
     * @return The text currently in the editor
     */
    QString resultText() const;

signals:
    /**
     * @brief Emitted when user clicks Copy button.
     * @param text The final text to copy to clipboard
     */
    void copyRequested(const QString& text);

protected:
    void showEvent(QShowEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onCopyClicked();
    void onTextChanged();

private:
    void setupUi();
    void updateCharacterCount();

    QTextEdit* m_textEdit;
    QPushButton* m_copyBtn;
    QPushButton* m_cancelBtn;
    QLabel* m_charCountLabel;
};

#endif // OCRRESULTDIALOG_H
