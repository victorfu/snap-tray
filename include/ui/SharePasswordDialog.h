#ifndef SHAREPASSWORDDIALOG_H
#define SHAREPASSWORDDIALOG_H

#include <QDialog>
#include <QPoint>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;
class QKeyEvent;
class QMouseEvent;
class QShowEvent;

class SharePasswordDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SharePasswordDialog(QWidget* parent = nullptr);
    ~SharePasswordDialog() override;

    QString password() const;
    void setPassword(const QString& password);
    void showAt(const QPoint& pos = QPoint());

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onCancelClicked();
    void onShareClicked();

private:
    void setupUi();
    void applyTheme();

    QLabel* m_iconLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_subtitleLabel = nullptr;
    QLabel* m_hintLabel = nullptr;
    QLineEdit* m_passwordEdit = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QPushButton* m_shareButton = nullptr;

    QPoint m_dragOffset;
    bool m_dragging = false;
};

#endif // SHAREPASSWORDDIALOG_H
