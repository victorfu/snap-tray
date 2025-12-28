#ifndef FORMATSELECTIONWIDGET_H
#define FORMATSELECTIONWIDGET_H

#include <QWidget>

class QButtonGroup;
class QPushButton;

/**
 * @brief Widget for selecting output format (MP4, GIF, WebP)
 *
 * Displays three toggle buttons for format selection.
 * Tooltips provide format-specific information.
 */
class FormatSelectionWidget : public QWidget
{
    Q_OBJECT

public:
    enum class Format {
        MP4,
        GIF,
        WebP
    };

    explicit FormatSelectionWidget(QWidget *parent = nullptr);

    Format selectedFormat() const;
    void setSelectedFormat(Format format);

signals:
    void formatChanged(Format format);

private slots:
    void onButtonClicked(int id);

private:
    void setupUI();
    void updateButtonStyles();

    QButtonGroup *m_buttonGroup;
    QPushButton *m_mp4Btn;
    QPushButton *m_gifBtn;
    QPushButton *m_webpBtn;

    Format m_currentFormat;

    static constexpr int kButtonWidth = 60;
    static constexpr int kButtonHeight = 28;
};

#endif // FORMATSELECTIONWIDGET_H
