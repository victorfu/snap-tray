#include "PlatformFeatures.h"
#include "OCRManager.h"
#include "WindowDetector.h"

PlatformFeatures& PlatformFeatures::instance()
{
    static PlatformFeatures instance;
    return instance;
}

PlatformFeatures::PlatformFeatures()
    : m_ocrAvailable(OCRManager::isAvailable())
    , m_windowDetectionAvailable(true)
{
}

PlatformFeatures::~PlatformFeatures() = default;

bool PlatformFeatures::isOCRAvailable() const
{
    return m_ocrAvailable;
}

bool PlatformFeatures::isWindowDetectionAvailable() const
{
    return m_windowDetectionAvailable;
}

OCRManager* PlatformFeatures::createOCRManager(QObject* parent) const
{
    if (!m_ocrAvailable) {
        return nullptr;
    }
    return new OCRManager(parent);
}

WindowDetector* PlatformFeatures::createWindowDetector(QObject* parent) const
{
    if (!m_windowDetectionAvailable) {
        return nullptr;
    }
    return new WindowDetector(parent);
}

QString PlatformFeatures::platformName() const
{
    return QStringLiteral("macOS");
}
