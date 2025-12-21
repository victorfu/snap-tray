#ifndef SCREENCANVASMANAGER_H
#define SCREENCANVASMANAGER_H

#include <QObject>
#include <QPointer>

class ScreenCanvas;

class ScreenCanvasManager : public QObject
{
    Q_OBJECT

public:
    explicit ScreenCanvasManager(QObject *parent = nullptr);
    ~ScreenCanvasManager();

    bool isActive() const;

public slots:
    void toggle();

signals:
    void canvasOpened();
    void canvasClosed();

private slots:
    void onCanvasClosed();

private:
    QPointer<ScreenCanvas> m_canvas;
};

#endif // SCREENCANVASMANAGER_H
