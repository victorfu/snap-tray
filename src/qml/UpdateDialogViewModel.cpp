#include "qml/UpdateDialogViewModel.h"

UpdateDialogViewModel::UpdateDialogViewModel(QObject* parent)
    : QObject(parent)
{
}

UpdateDialogViewModel* UpdateDialogViewModel::createError(const QString& message, QObject* parent)
{
    auto* vm = new UpdateDialogViewModel(parent);
    vm->m_mode = Error;
    vm->m_errorMessage = message;
    return vm;
}

UpdateDialogViewModel* UpdateDialogViewModel::createInfo(const QString& message, QObject* parent)
{
    auto* vm = new UpdateDialogViewModel(parent);
    vm->m_mode = Info;
    vm->m_errorMessage = message;
    return vm;
}

int UpdateDialogViewModel::mode() const { return static_cast<int>(m_mode); }
QString UpdateDialogViewModel::errorMessage() const { return m_errorMessage; }

void UpdateDialogViewModel::retry()
{
    emit retryRequested();
    emit closeRequested();
}

void UpdateDialogViewModel::close()
{
    emit closeRequested();
}
