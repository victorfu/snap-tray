#include "OCRController.h"

#ifdef Q_OS_MACOS

#include "OCRManager.h"
#include <QPointer>
#include <QDebug>

OCRController::OCRController(QObject* parent)
    : QObject(parent)
    , m_ocrManager(nullptr)
    , m_inProgress(false)
{
    if (OCRManager::isAvailable()) {
        m_ocrManager = new OCRManager(this);
    }
}

OCRController::~OCRController()
{
    // m_ocrManager is a child, will be deleted automatically
}

bool OCRController::isAvailable() const
{
    return m_ocrManager != nullptr;
}

void OCRController::performOCR(const QPixmap& region)
{
    if (!m_ocrManager || m_inProgress) {
        return;
    }

    m_inProgress = true;
    emit ocrStarted();

    QPointer<OCRController> self = this;

    m_ocrManager->recognizeText(region, [self](bool success, const QString& text, const QString& error) {
        if (!self) return;

        self->m_inProgress = false;

        if (success && !text.isEmpty()) {
            emit self->ocrCompleted(text);
        } else {
            QString errorMsg = error.isEmpty() ? "OCR failed" : error;
            emit self->ocrFailed(errorMsg);
        }
    });
}

void OCRController::cancel()
{
    m_inProgress = false;
}

#endif // Q_OS_MACOS
