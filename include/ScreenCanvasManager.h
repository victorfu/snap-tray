#ifndef SCREENCANVASMANAGER_H
#define SCREENCANVASMANAGER_H

#include <QObject>
#include <QPointer>

class ScreenCanvasSession;

class ScreenCanvasManager : public QObject
{
    Q_OBJECT

public:
    explicit ScreenCanvasManager(QObject* parent = nullptr);
    ~ScreenCanvasManager() override;

    bool isActive() const;

public slots:
    void toggle();

signals:
    void canvasOpened();
    void canvasClosed();

private slots:
    void onSessionClosed();

private:
    QPointer<ScreenCanvasSession> m_session;
};

#endif // SCREENCANVASMANAGER_H
