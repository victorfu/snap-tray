#ifndef QRCODERESULTDIALOG_H
#define QRCODERESULTDIALOG_H

#include <QWidget>
#include <QString>
#include <QPixmap>

class QTextEdit;
class QPushButton;
class QLabel;

/**
 * @brief Dialog displaying QR Code decode results with interactive features.
 *
 * Features:
 * - Editable text area showing decoded content
 * - Copy button with visual feedback ("✓ Copied!")
 * - Automatic URL detection with "Open URL" button
 * - QR code thumbnail and format display
 * - Character count with "(edited)" indicator
 * - Dark theme UI matching SnapTray style
 * - Draggable title bar
 * - Keyboard shortcuts: Escape, Ctrl+C, Ctrl+Enter
 */
class QRCodeResultDialog : public QWidget
{
    Q_OBJECT

public:
    explicit QRCodeResultDialog(QWidget *parent = nullptr);
    ~QRCodeResultDialog() override;

    /**
     * @brief Set the decode result to display
     * @param text Decoded text content
     * @param format Barcode format (e.g., "QR_CODE", "EAN_13")
     * @param sourceImage Optional source image to generate thumbnail
     */
    void setResult(const QString &text, const QString &format, const QPixmap &sourceImage = QPixmap());

    /**
     * @brief Get current text (may be edited by user)
     */
    QString currentText() const;

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
     * @brief Emitted when URL is opened in browser
     */
    void urlOpened(const QString &url);

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
    void onOpenUrlClicked();
    void onCloseClicked();
    void onTextChanged();

private:
    void setupUi();
    void applyDarkTheme();
    QString detectUrl(const QString &text) const;
    bool isValidUrl(const QString &urlString) const;
    QString getFormatDisplayName(const QString &format) const;
    void updateCharacterCount();
    void showCopyFeedback();

    // UI Components
    QLabel *m_thumbnailLabel;
    QLabel *m_formatLabel;
    QLabel *m_characterCountLabel;
    QTextEdit *m_textEdit;
    QPushButton *m_closeButton;
    QPushButton *m_copyButton;
    QPushButton *m_openUrlButton;

    // State
    QString m_originalText;
    QString m_format;
    QPixmap m_thumbnail;
    QPoint m_dragPosition;
    bool m_isDragging;
    bool m_isTextEdited;
};

#endif // QRCODERESULTDIALOG_H
