#include "PlatformFeatures.h"
#include "platform/ClipboardImageFile.h"
#include "platform/LinuxClipboardOwner.h"

#include "OCRManager.h"
#include "WindowDetector.h"

#include <QClipboard>
#include <QMimeData>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeySequence>
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

QString cliExecLine(const QString& appPath)
{
    return QStringLiteral("exec %1 \"$@\"").arg(shellQuote(appPath));
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

QPixmap PlatformFeatures::createTrayPixmap() const
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

    return pixmap;
}

bool PlatformFeatures::copyImageOnlyPersistently(const QImage& image) const
{
    return SnapTray::copyImageToLinuxClipboard(image, launchableAppPath());
}

bool PlatformFeatures::clipboardIsDefinitelyEmpty()
{
    return SnapTray::linuxClipboardOwnerWindow() == 0u;
}

bool PlatformFeatures::clipboardImageIsFileIcon()
{
    return false;   // Explorer and X11 file managers put no icon next to copied files.
}

bool PlatformFeatures::isShortcutReservedBySystem(const QKeySequence& sequence)
{
    // XKB maps exactly Ctrl+Alt+F<n> to XF86Switch_VT_<n>, which the X server
    // handles itself. With Shift added the keys stay plain function keys.
    constexpr auto kSwitchVtModifiers = Qt::ControlModifier | Qt::AltModifier;
    if (sequence.count() != 1) {
        return false;
    }
    const QKeyCombination combination = sequence[0];
    const int key = combination.key();
    return combination.keyboardModifiers() == kSwitchVtModifiers
        && key >= Qt::Key_F1 && key <= Qt::Key_F12;
}

quint64 PlatformFeatures::nativeClipboardRevision()
{
    return 0;
}

PlatformFeatures::ClipboardCopyResult PlatformFeatures::publishClipboardImageFile(
    const QImage& image, SnapTray::ClipboardImageFile& file, bool includePixels, bool persistent) const
{
    if (persistent) {
        if (SnapTray::copyFileToLinuxClipboard(file.path, includePixels, launchableAppPath(),
                                              file.offeredToClipboard))
            return ClipboardCopyResult::Success;
        return file.offeredToClipboard ? ClipboardCopyResult::Incomplete : ClipboardCopyResult::Failed;
    }

    auto* clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return ClipboardCopyResult::Failed;
    auto* data = new QMimeData;
    const auto url = QUrl::fromLocalFile(file.path);
    data->setUrls({url});
    data->setData(QStringLiteral("x-special/gnome-copied-files"), "copy\n" + url.toEncoded());
    if (includePixels) {
        data->setImageData(image);
        data->setData(QStringLiteral("image/png"), file.png);
    }
    clipboard->setMimeData(data);
    file.offeredToClipboard = true;
    const auto owner = SnapTray::linuxClipboardOwnerWindow();
    if (!owner || !*owner || !clipboard->ownsClipboard() || clipboard->mimeData() != data)
        return ClipboardCopyResult::Incomplete;
    // Interactive publication stays on the GUI thread. The owner process is
    // needed only when this GUI is exiting with this exact selection active.
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, data,
        [clipboard, data, path = file.path, includePixels, owner] {
            if (clipboard->ownsClipboard() && clipboard->mimeData() == data) {
                bool started = false;
                if (!SnapTray::copyFileToLinuxClipboard(path, includePixels, launchableAppPath(), started, *owner))
                    qWarning() << "Could not keep the X11 screenshot clipboard after exit";
            }
        });
    return ClipboardCopyResult::Success;
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

    // Match the shell-quoted exec line installCLI() writes; the raw path won't
    // appear verbatim when it contains a single quote.
    return QString::fromUtf8(file.readAll()).contains(cliExecLine(getAppExecutablePath()));
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
    stream << cliExecLine(getAppExecutablePath()) << "\n";
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
