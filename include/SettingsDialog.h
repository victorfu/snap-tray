#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

class QKeySequenceEdit;
class QSettings;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog();

    static QString defaultHotkey();
    static QString loadHotkey();
    static void saveHotkey(const QString &keySequence);

public:
    void showHotkeyError(const QString &message);

signals:
    void hotkeyChangeRequested(const QString &newHotkey);

private slots:
    void onSave();
    void onCancel();

private:
    void setupUi();

    QKeySequenceEdit *m_hotkeyEdit;
};

#endif // SETTINGSDIALOG_H
