#include <QtTest/QtTest>

#include "qml/UpdateDialogViewModel.h"

#include <QSignalSpy>

class tst_UpdateDialogViewModel : public QObject
{
    Q_OBJECT

private slots:
    void testCreateError_PreservesModeAndMessage();
    void testCreateInfo_PreservesModeAndMessage();
    void testRetry_EmitsRetryAndClose();
    void testClose_EmitsCloseOnly();
};

void tst_UpdateDialogViewModel::testCreateError_PreservesModeAndMessage()
{
    UpdateDialogViewModel* viewModel =
        UpdateDialogViewModel::createError(QStringLiteral("Update failed"), this);

    QCOMPARE(viewModel->mode(), static_cast<int>(UpdateDialogViewModel::Error));
    QCOMPARE(viewModel->errorMessage(), QStringLiteral("Update failed"));
}

void tst_UpdateDialogViewModel::testCreateInfo_PreservesModeAndMessage()
{
    UpdateDialogViewModel* viewModel =
        UpdateDialogViewModel::createInfo(QStringLiteral("Managed by Homebrew"), this);

    QCOMPARE(viewModel->mode(), static_cast<int>(UpdateDialogViewModel::Info));
    QCOMPARE(viewModel->errorMessage(), QStringLiteral("Managed by Homebrew"));
}

void tst_UpdateDialogViewModel::testRetry_EmitsRetryAndClose()
{
    UpdateDialogViewModel* viewModel =
        UpdateDialogViewModel::createError(QStringLiteral("Try again"), this);
    QSignalSpy retrySpy(viewModel, &UpdateDialogViewModel::retryRequested);
    QSignalSpy closeSpy(viewModel, &UpdateDialogViewModel::closeRequested);

    viewModel->retry();

    QCOMPARE(retrySpy.count(), 1);
    QCOMPARE(closeSpy.count(), 1);
}

void tst_UpdateDialogViewModel::testClose_EmitsCloseOnly()
{
    UpdateDialogViewModel* viewModel =
        UpdateDialogViewModel::createInfo(QStringLiteral("Info"), this);
    QSignalSpy retrySpy(viewModel, &UpdateDialogViewModel::retryRequested);
    QSignalSpy closeSpy(viewModel, &UpdateDialogViewModel::closeRequested);

    viewModel->close();

    QCOMPARE(retrySpy.count(), 0);
    QCOMPARE(closeSpy.count(), 1);
}

QTEST_MAIN(tst_UpdateDialogViewModel)
#include "tst_UpdateDialogViewModel.moc"
