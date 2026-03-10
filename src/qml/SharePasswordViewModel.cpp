#include "qml/SharePasswordViewModel.h"
#include "share/ShareUploadClient.h"

SharePasswordViewModel::SharePasswordViewModel(QObject* parent)
    : QObject(parent)
{
}

QString SharePasswordViewModel::password() const
{
    return m_password;
}

void SharePasswordViewModel::setPassword(const QString& password)
{
    if (m_password != password) {
        m_password = password;
        emit passwordChanged();
    }
}

int SharePasswordViewModel::maxLength() const
{
    return ShareUploadClient::kMaxPasswordLength;
}

void SharePasswordViewModel::accept()
{
    emit accepted();
}

void SharePasswordViewModel::cancel()
{
    emit rejected();
}
