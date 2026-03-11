#pragma once

#include <QObject>
#include <QPointer>
#include <QPixmap>

#include "pinwindow/PinHistoryStore.h"

class PinWindowManager;
class QWindow;

namespace SnapTray {

class PinHistoryModel;

class PinHistoryBackend : public QObject
{
    Q_OBJECT

public:
    explicit PinHistoryBackend(PinHistoryModel* model,
                               PinWindowManager* pinWindowManager,
                               QWindow* hostWindow = nullptr,
                               QObject* parent = nullptr);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void pin(int index);
    Q_INVOKABLE void copy(int index);
    Q_INVOKABLE void saveAs(int index);
    Q_INVOKABLE void deleteEntry(int index);
    Q_INVOKABLE void openCacheFolder();

signals:
    void toastRequested(int level, const QString& title, const QString& message);
    void closeRequested();

private:
    bool loadPixmap(const PinHistoryEntry& entry, QPixmap* pixmapOut) const;
    void showError(const QString& title, const QString& message);

    PinHistoryModel* m_model = nullptr;
    PinWindowManager* m_pinWindowManager = nullptr;
    QPointer<QWindow> m_hostWindow;
};

} // namespace SnapTray
