#ifndef SHARERESULTDIALOG_H
#define SHARERESULTDIALOG_H

#include <QDateTime>
#include <QPoint>
#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QCloseEvent;
class QKeyEvent;
class QMouseEvent;
class QShowEvent;

class ShareResultDialog : public QWidget
{
    Q_OBJECT

public:
    explicit ShareResultDialog(QWidget* parent = nullptr);
    ~ShareResultDialog() override;

    void setResult(const QString& url,
                   const QDateTime& expiresAt,
                   bool isProtected,
                   const QString& password = QString());
    QString url() const;
    void showAt(const QPoint& pos = QPoint());

signals:
    void dialogClosed();
    void linkCopied(const QString& url);
    void linkOpened(const QString& url);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onCloseClicked();
    void onCopyClicked();
    void onOpenClicked();

private:
    void setupUi();
    void applyTheme();
    QString clipboardText() const;

    QLabel* m_iconLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_subtitleLabel = nullptr;
    QLabel* m_urlCaptionLabel = nullptr;
    QLabel* m_passwordCaptionLabel = nullptr;
    QLabel* m_hintLabel = nullptr;
    QLabel* m_expiresLabel = nullptr;
    QLabel* m_protectedLabel = nullptr;
    QLineEdit* m_urlEdit = nullptr;
    QLineEdit* m_passwordEdit = nullptr;
    QPushButton* m_closeButton = nullptr;
    QPushButton* m_copyButton = nullptr;
    QPushButton* m_openButton = nullptr;

    QString m_sharePassword;
    QPoint m_dragOffset;
    bool m_dragging = false;
};

#endif // SHARERESULTDIALOG_H
