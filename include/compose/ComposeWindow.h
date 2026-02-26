#ifndef COMPOSEWINDOW_H
#define COMPOSEWINDOW_H

#include <QPoint>
#include <QPixmap>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QComboBox;
class QPushButton;
class QHBoxLayout;
class QKeyEvent;
class QCloseEvent;
class QEvent;

class ComposeRichTextEdit;

class ComposeWindow : public QWidget
{
    Q_OBJECT

public:
    struct CaptureContext {
        QPixmap screenshot;
        QString windowTitle;
        QString appName;
        QString timestamp;
        QSize screenResolution;
        QString screenName;
        QRect captureRegion;
    };

    explicit ComposeWindow(QWidget* parent = nullptr);
    ~ComposeWindow() override;

    void setCaptureContext(const CaptureContext& context);
    void showAt(const QPoint& pos = QPoint());

signals:
    void composeCopied(const QString& format);
    void composeCancelled();

private slots:
    void onCopyAsHtml();
    void onCopyAsMarkdown();
    void onTemplateChanged(int index);
    void onInsertImage();
    void onCancel();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void setupUi();
    void setupFormattingToolbar(QHBoxLayout* toolbarLayout);
    void applyTheme();
    void updateEditorDocumentTheme();
    void applyTemplate(const QString& templateId);
    void updateMetadataLabel();
    void applyInlineCodeFormat();
    QString resolveInitialTemplateId() const;
    void insertCaptureImageIfNeeded();
    void applyEditorZoomDelta(int deltaSteps);

    QComboBox* m_templateCombo = nullptr;
    QLineEdit* m_titleEdit = nullptr;
    ComposeRichTextEdit* m_descriptionEdit = nullptr;
    QLabel* m_metadataLabel = nullptr;
    QPushButton* m_copyMarkdownButton = nullptr;
    QPushButton* m_copyHtmlButton = nullptr;

    CaptureContext m_context;
    bool m_hasCopied = false;
    bool m_isApplyingTheme = false;
    bool m_captureImageInserted = false;
    int m_editorZoomSteps = 0;
};

#endif // COMPOSEWINDOW_H
