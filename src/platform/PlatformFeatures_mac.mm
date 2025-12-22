#include "PlatformFeatures.h"
#include "OCRManager.h"
#include "WindowDetector.h"
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

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

QIcon PlatformFeatures::createTrayIcon() const
{
    const int size = 32;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    // Capsule background
    QPainterPath bgPath;
    bgPath.addRoundedRect(0, 0, size, size, size / 2, size / 2);

    // Lightning bolt cutout
    QPainterPath lightningPath;
    lightningPath.moveTo(19, 3);
    lightningPath.lineTo(8, 17);
    lightningPath.lineTo(15, 17);
    lightningPath.lineTo(13, 29);
    lightningPath.lineTo(24, 14);
    lightningPath.lineTo(17, 14);
    lightningPath.closeSubpath();

    QPainterPath finalPath = bgPath.subtracted(lightningPath);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawPath(finalPath);

    return QIcon(pixmap);
}
