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
    QmlDialog(const QUrl& qmlSource, QObject* viewModel,
              const QString& contextPropertyName, QObject* parent = nullptr);
    ~QmlDialog() override;

    virtual void showAt(const QPoint& pos = QPoint());
    virtual void showCenteredOnScreen(QScreen* screen);
    virtual void close();
    void setModal(bool modal);

    static QPoint centeredTopLeftForBounds(const QRect& bounds, const QSize& viewSize);

signals:
    void closed();

private:
    void ensureView();
    void showPreparedView();
    QWidget* hostWidget() const;
    void syncTransientParent();
    void applyPlatformWindowFlags();
    void syncCursorSurface();
    bool eventFilter(QObject* watched, QEvent* event) override;

    QUrl m_qmlSource;
    QPointer<QObject> m_viewModel;
    QString m_contextPropertyName;
    QQuickView* m_view = nullptr;
    bool m_modal = false;
    QString m_cursorSurfaceId;
    QString m_cursorOwnerId;
};

} // namespace SnapTray
