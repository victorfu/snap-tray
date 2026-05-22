#include "PlatformFeatures.h"

#include "OCRManager.h"
#include "WindowDetector.h"

#include <QBuffer>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStandardPaths>
#include <QTextStream>

namespace {

QString cliScriptPath()
{
    const QString binDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
        + QStringLiteral("/.local/bin");
    return QDir(binDir).filePath(QStringLiteral("snaptray"));
}

QString launchableAppPath()
{
    const QString appImagePath = qEnvironmentVariable("APPIMAGE").trimmed();
    return appImagePath.isEmpty()
        ? QCoreApplication::applicationFilePath()
        : appImagePath;
}

QString shellQuote(const QString& value)
{
    QString escaped = value;
    escaped.replace('\'', QStringLiteral("'\"'\"'"));
    return QStringLiteral("'%1'").arg(escaped);
}

} // namespace

PlatformFeatures& PlatformFeatures::instance()
{
    static PlatformFeatures instance;
    return instance;
}

PlatformFeatures::PlatformFeatures()
    : m_capabilities(SnapTray::currentPlatformCapabilities())
    , m_ocrAvailable(false)
    , m_windowDetectionAvailable(m_capabilities.supportsWindowDetection)
{
}

PlatformFeatures::~PlatformFeatures() = default;

const SnapTray::PlatformCapabilities& PlatformFeatures::capabilities() const
{
    return m_capabilities;
}

bool PlatformFeatures::isOCRAvailable() const
{
    return false;
}

OCRManager* PlatformFeatures::createOCRManager(QObject*) const
{
    return nullptr;
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
    constexpr int size = 32;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QPainterPath bgPath;
    bgPath.addRoundedRect(0, 0, size, size, size / 2, size / 2);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#6366F1")));
    painter.drawPath(bgPath);

    QPainterPath lightningPath;
    lightningPath.moveTo(19, 3);
    lightningPath.lineTo(8, 17);
    lightningPath.lineTo(15, 17);
    lightningPath.lineTo(13, 29);
    lightningPath.lineTo(24, 14);
    lightningPath.lineTo(17, 14);
    lightningPath.closeSubpath();

    painter.setPen(QPen(QColor(QStringLiteral("#FEF3C7")), 1));
    painter.setBrush(QColor(QStringLiteral("#FBBF24")));
    painter.drawPath(lightningPath);

    return QIcon(pixmap);
}

bool PlatformFeatures::copyImageToClipboardPersistently(const QImage& image) const
{
    return copyImageToClipboardForGui(image);
}

bool PlatformFeatures::copyImageToClipboardForGui(const QImage& image) const
{
    if (image.isNull()) {
        return false;
    }

    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return false;
    }

    auto* mimeData = new QMimeData();
    QByteArray pngData;
    QBuffer buffer(&pngData);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    mimeData->setData(QStringLiteral("image/png"), pngData);
    mimeData->setImageData(image);
    clipboard->setMimeData(mimeData);
    return true;
}

QString PlatformFeatures::getAppExecutablePath() const
{
    return launchableAppPath();
}

bool PlatformFeatures::isCLIInstalled() const
{
    QFile file(cliScriptPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    return QString::fromUtf8(file.readAll()).contains(getAppExecutablePath());
}

bool PlatformFeatures::installCLI() const
{
    const QFileInfo scriptInfo(cliScriptPath());
    if (!QDir().mkpath(scriptInfo.absolutePath())) {
        return false;
    }

    QFile file(scriptInfo.absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "#!/bin/sh\n";
    stream << "exec " << shellQuote(getAppExecutablePath()) << " \"$@\"\n";
    file.close();

    return QFile::setPermissions(
        scriptInfo.absoluteFilePath(),
        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
            QFile::ReadGroup | QFile::ExeGroup |
            QFile::ReadOther | QFile::ExeOther);
}

bool PlatformFeatures::uninstallCLI() const
{
    return !QFileInfo::exists(cliScriptPath()) || QFile::remove(cliScriptPath());
}
