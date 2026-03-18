#pragma once

#include "history/HistoryStore.h"

#include <QObject>
#include <QPointer>
#include <QPixmap>
#include <QWindow>
#include <functional>

class PinWindowManager;

namespace SnapTray {

class HistoryModel;

class HistoryBackend : public QObject
{
    Q_OBJECT

public:
    explicit HistoryBackend(HistoryModel* model,
                            PinWindowManager* pinWindowManager,
                            std::function<bool(const QString&)> startReplay,
                            QWindow* hostWindow = nullptr,
                            QObject* parent = nullptr);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void edit(int index);
    Q_INVOKABLE void pin(int index);
    Q_INVOKABLE void copy(int index);
    Q_INVOKABLE void saveAs(int index);
    Q_INVOKABLE void deleteEntry(int index);
    Q_INVOKABLE void openHistoryFolder();

signals:
    void toastRequested(int level, const QString& title, const QString& message);
    void closeRequested();

private:
    bool loadPixmap(const HistoryEntry& entry, QPixmap* pixmapOut) const;
    void showError(const QString& title, const QString& message);

    HistoryModel* m_model = nullptr;
    PinWindowManager* m_pinWindowManager = nullptr;
    std::function<bool(const QString&)> m_startReplay;
    QPointer<QWindow> m_hostWindow;
};

} // namespace SnapTray
