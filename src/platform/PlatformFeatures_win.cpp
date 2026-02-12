#include "PlatformFeatures.h"
#include "OCRManager.h"
#include "WindowDetector.h"
#include "platform/PathEnvUtils_win.h"
#include <QBuffer>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSettings>

#include <windows.h>

namespace {

void broadcastEnvironmentChange()
{
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
        reinterpret_cast<LPARAM>(L"Environment"), SMTO_ABORTIFHUNG, 5000, nullptr);
}

} // namespace

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

QIcon PlatformFeatures::createTrayIcon() const
{
    const int size = 32;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    // Capsule background - Indigo
    QPainterPath bgPath;
    bgPath.addRoundedRect(0, 0, size, size, size / 2, size / 2);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#6366F1"));
    painter.drawPath(bgPath);

    // Lightning bolt - Amber with light yellow stroke
    QPainterPath lightningPath;
    lightningPath.moveTo(19, 3);
    lightningPath.lineTo(8, 17);
    lightningPath.lineTo(15, 17);
    lightningPath.lineTo(13, 29);
    lightningPath.lineTo(24, 14);
    lightningPath.lineTo(17, 14);
    lightningPath.closeSubpath();

    painter.setPen(QPen(QColor("#FEF3C7"), 1));
    painter.setBrush(QColor("#FBBF24"));
    painter.drawPath(lightningPath);

    return QIcon(pixmap);
}

bool PlatformFeatures::copyImageToClipboard(const QImage& image) const
{
    if (image.isNull()) {
        return false;
    }

    // On Windows, use Qt clipboard with QMimeData
    auto* mimeData = new QMimeData();

    // Add PNG data
    QByteArray pngData;
    QBuffer buffer(&pngData);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    mimeData->setData("image/png", pngData);
    mimeData->setImageData(image);

    QGuiApplication::clipboard()->setMimeData(mimeData);
    return true;
}

QString PlatformFeatures::getAppExecutablePath() const
{
    return QCoreApplication::applicationDirPath();
}

bool PlatformFeatures::isCLIInstalled() const
{
    QSettings env("HKEY_CURRENT_USER\\Environment", QSettings::NativeFormat);
    const QStringList entries = PathEnvUtils::splitPathEntries(env.value("Path").toString());
    return PathEnvUtils::containsPathEntry(entries, getAppExecutablePath());
}

bool PlatformFeatures::installCLI() const
{
    QSettings env("HKEY_CURRENT_USER\\Environment", QSettings::NativeFormat);
    const QString appDir = QDir::toNativeSeparators(getAppExecutablePath());
    const QStringList currentEntries = PathEnvUtils::splitPathEntries(env.value("Path").toString());
    const auto result = PathEnvUtils::installPathEntry(currentEntries, appDir);

    if (result.changed) {
        env.setValue("Path", result.entries.join(';'));
        broadcastEnvironmentChange();
    }
    return true;
}

bool PlatformFeatures::uninstallCLI() const
{
    QSettings env("HKEY_CURRENT_USER\\Environment", QSettings::NativeFormat);
    const QStringList currentEntries = PathEnvUtils::splitPathEntries(env.value("Path").toString());
    const auto result = PathEnvUtils::uninstallPathEntry(currentEntries, getAppExecutablePath());

    if (result.changed) {
        env.setValue("Path", result.entries.join(';'));
        broadcastEnvironmentChange();
    }
    return true;
}
