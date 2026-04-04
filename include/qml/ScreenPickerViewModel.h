#pragma once

#include "recording/ScreenSourceService.h"

#include <QObject>
#include <QPointer>
#include <QScreen>
#include <QStringList>
#include <QVariantList>
#include <QVector>

namespace SnapTray {

class ScreenPickerViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList screens READ screens NOTIFY screensChanged)
    Q_PROPERTY(int thumbnailCacheBuster READ thumbnailCacheBuster NOTIFY thumbnailCacheBusterChanged)

public:
    explicit ScreenPickerViewModel(QObject* parent = nullptr);
    explicit ScreenPickerViewModel(const QVector<ScreenSourceInfo>& sources, QObject* parent = nullptr);
    ~ScreenPickerViewModel() override;

    QVariantList screens() const { return m_screens; }
    int thumbnailCacheBuster() const { return m_thumbnailCacheBuster; }

    void setSources(const QVector<ScreenSourceInfo>& sources);

    Q_INVOKABLE void chooseScreen(int index);
    Q_INVOKABLE void cancel();

signals:
    void screenChosen(QScreen* screen);
    void cancelled();
    void screensChanged();
    void thumbnailCacheBusterChanged();

private:
    void clearThumbnails();

    QVector<QPointer<QScreen>> m_screenRefs;
    QStringList m_screenIds;
    QVariantList m_screens;
    QStringList m_thumbnailIds;
    int m_thumbnailCacheBuster = 0;
};

} // namespace SnapTray
