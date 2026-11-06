#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickView>
#include <QString>
#include <functional>

class PinWindowManager;
class QEvent;
class QQuickView;

namespace SnapTray {

class HistoryBackend;
class HistoryModel;
class LibraryModel;
class RecordingLibraryViewModel;

class QmlHistoryWindow : public QObject
{
    Q_OBJECT

public:
    explicit QmlHistoryWindow(PinWindowManager* pinWindowManager,
                              std::function<bool(const QString&)> startReplay,
                              QObject* parent = nullptr);
    ~QmlHistoryWindow() override;

    void show();
    void showNormal();
    void raise();
    void activateWindow();
    bool isVisible() const;
    bool isMinimized() const;

    // Selects a Library section (LibraryModel::Source), e.g. Recordings from
    // the tray's "Recording Library...".
    void showSection(int source);
    // The project open in the recording preview: it cannot be opened again
    // or deleted from the Library.
    void setOpenProjectDir(const QString& dir);
    QQuickView* view() const { return m_view; }
    LibraryModel* libraryModel() const { return m_library; }

signals:
    void openProjectRequested(const QString& projectDir);

private:
    void ensureView();
    void syncCursorSurface();
    bool eventFilter(QObject* watched, QEvent* event) override;

    QPointer<QQuickView> m_view;
    HistoryModel* m_model = nullptr;
    HistoryBackend* m_backend = nullptr;
    RecordingLibraryViewModel* m_recordings = nullptr;
    LibraryModel* m_library = nullptr;
    QString m_openProjectDir;
    PinWindowManager* m_pinWindowManager = nullptr;
    std::function<bool(const QString&)> m_startReplay;
    bool m_hasShownOnce = false;
    QString m_cursorSurfaceId;
    QString m_cursorOwnerId;
};

} // namespace SnapTray
