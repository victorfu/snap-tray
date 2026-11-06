#pragma once

#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QString>
#include <QUrl>

class QEvent;
class QQuickView;
class QScreen;
class QWidget;

namespace SnapTray {

class QmlDialog : public QObject
{
    Q_OBJECT

public:
    enum class ViewModelOwnership { Owned, Borrowed };

    QmlDialog(const QUrl& qmlSource, QObject* viewModel,
              const QString& contextPropertyName, QObject* parent = nullptr,
              ViewModelOwnership ownership = ViewModelOwnership::Owned);
    ~QmlDialog() override;

    virtual void showAt(const QPoint& pos = QPoint());
    virtual void showCenteredOnScreen(QScreen* screen);
    virtual void close();
    void setModal(bool modal);

    static QPoint centeredTopLeftForBounds(const QRect& bounds, const QSize& viewSize);
    // topLeft, moved just enough for a window of viewSize to fit in bounds.
    // A window larger than bounds keeps its top-left corner inside them.
    static QPoint keptInsideBounds(const QPoint& topLeft, const QSize& viewSize, const QRect& bounds);

signals:
    void closed();

private:
    friend class TestQmlDialogModality;
    void ensureView();
    void showPreparedView();
    QWidget* hostWidget() const;
    void syncTransientParent();
    void applyPlatformWindowFlags();
    void syncCursorSurface();
    void placeView(const QPoint& topLeft);
    void keepViewOnScreen();
    bool eventFilter(QObject* watched, QEvent* event) override;

    QUrl m_qmlSource;
    QPointer<QObject> m_viewModel;
    QString m_contextPropertyName;
    QQuickView* m_view = nullptr;
    bool m_modal = false;
    QString m_cursorSurfaceId;
    QString m_cursorOwnerId;
    // The screen a centered dialog stays centered on when its content grows
    // after it is shown; null once it is placed at a point or moved.
    QPointer<QScreen> m_centeredOn;
    QPoint m_placedAt; // last requested placement, before any window-manager constraint
    QSize m_placedSize;
};

} // namespace SnapTray
