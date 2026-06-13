#include "OCRManager.h"

OCRManager::OCRManager(QObject* parent)
    : QObject(parent)
{
}

OCRManager::~OCRManager() = default;

QList<OCRLanguageInfo> OCRManager::availableLanguages()
{
    return {};
}

OCRLanguageQueryResult OCRManager::queryAvailableLanguages()
{
    return {};
}

void OCRManager::setRecognitionLanguages(const QStringList& languageCodes)
{
    m_languages = languageCodes;
}

QStringList OCRManager::recognitionLanguages() const
{
    return m_languages;
}

void OCRManager::recognizeText(const QPixmap&, const OCRCallback& callback)
{
    if (callback) {
        callback(OCRResult{});
    }
}

bool OCRManager::isAvailable()
{
    return false;
}

void OCRManager::beginShutdown()
{
    m_shuttingDown = true;
}
