#include "qml/ScreenPickerViewModel.h"

#include "qml/DialogImageProvider.h"
#include "recording/ScreenSourceService.h"

#include <QScreen>
#include <QVariantMap>
#include <utility>

namespace {

QString tooltipTextForSource(const SnapTray::ScreenSource& source)
{
    if (source.name.isEmpty()) {
        return source.resolutionText;
    }
    if (source.resolutionText.isEmpty()) {
        return source.name;
    }

    return QStringLiteral("%1 (%2)").arg(source.name, source.resolutionText);
}

} // namespace

namespace SnapTray {

ScreenPickerViewModel::ScreenPickerViewModel(QObject* parent)
    : QObject(parent)
{
}

ScreenPickerViewModel::ScreenPickerViewModel(const QVector<ScreenSourceInfo>& sources,
                                             QObject* parent)
    : QObject(parent)
{
    setSources(sources);
}

ScreenPickerViewModel::~ScreenPickerViewModel()
{
    clearThumbnails();
}

void ScreenPickerViewModel::setSources(const QVector<ScreenSourceInfo>& sources)
{
    clearThumbnails();
    m_screens.clear();
    m_screenRefs.clear();
    m_screenIds.clear();
    m_thumbnailIds.clear();

    for (int index = 0; index < sources.size(); ++index) {
        const auto& source = sources.at(index);
        if (!source.screen) {
            continue;
        }

        const QString thumbnailId = QStringLiteral("screen_picker_%1").arg(index);
        if (!source.thumbnail.isNull()) {
            DialogImageProvider::setImage(thumbnailId, source.thumbnail);
            m_thumbnailIds.append(thumbnailId);
        }

        QVariantMap map;
        map[QStringLiteral("name")] = source.name;
        map[QStringLiteral("resolution")] = source.resolutionText;
        map[QStringLiteral("tooltip")] = tooltipTextForSource(source);
        map[QStringLiteral("thumbnailId")] = thumbnailId;
        map[QStringLiteral("hasThumbnail")] = !source.thumbnail.isNull();
        map[QStringLiteral("isPrimary")] = source.isPrimary;
        m_screens.append(map);
        m_screenRefs.append(source.screen);
        m_screenIds.append(source.id);
    }

    ++m_thumbnailCacheBuster;
    emit thumbnailCacheBusterChanged();
    emit screensChanged();
}

void ScreenPickerViewModel::chooseScreen(int index)
{
    if (index < 0 || index >= m_screenRefs.size()) {
        return;
    }

    QScreen* screen = m_screenRefs.at(index).data();
    if (!screen && index < m_screenIds.size()) {
        screen = ScreenSourceService::screenForId(m_screenIds.at(index));
        if (screen) {
            m_screenRefs[index] = screen;
        }
    }

    emit screenChosen(screen);
}

void ScreenPickerViewModel::cancel()
{
    emit cancelled();
}

void ScreenPickerViewModel::clearThumbnails()
{
    for (const QString& thumbnailId : std::as_const(m_thumbnailIds)) {
        DialogImageProvider::removeImage(thumbnailId);
    }
    m_thumbnailIds.clear();
}

} // namespace SnapTray
