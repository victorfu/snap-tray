#pragma once

#include <QObject>
#include <QPointer>
#include <QRect>

class QQuickItem;
class QQuickView;
class QEvent;
class QScreen;

namespace SnapTray {

class QmlRecordingRegionSelector : public QObject
{
    Q_OBJECT

public:
    explicit QmlRecordingRegionSelector(QObject* parent = nullptr);
    ~QmlRecordingRegionSelector() override;

    void setGeometry(const QRect& geometry);
    void initializeWithRegion(QScreen* screen, const QRect& region);

    void show();
    void close();
    bool isVisible() const;
    void activateWindow();
    void raise();
    void raiseAboveMenuBar();

signals:
    void regionSelected(const QRect& region, QScreen* screen);
    void cancelled();
    void cancelledWithRegion(const QRect& region, QScreen* screen);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onConfirmSelection(double x, double y, double width, double height);
    void onCancelSelection(bool hasSelection, double x, double y,
                           double width, double height);

private:
    void ensureView();
    void applyPlatformWindowFlags();
    void updateTextProperties();
    void syncSelectionToQml();
    QRect toGlobalRect(double x, double y, double width, double height) const;

    QQuickView* m_view = nullptr;
    QQuickItem* m_rootItem = nullptr;
    QPointer<QScreen> m_currentScreen;
    QRect m_windowGeometry;
    QRect m_selectionRect;
    bool m_selectionComplete = false;
};

} // namespace SnapTray
