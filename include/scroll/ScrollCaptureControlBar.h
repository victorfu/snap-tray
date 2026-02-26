#ifndef SCROLLCAPTURECONTROLBAR_H
#define SCROLLCAPTURECONTROLBAR_H

#include <QWidget>
#include <QPoint>

class QLabel;
class QPushButton;
class QScreen;
class QShortcut;
class QImage;
class QRect;
class QMouseEvent;

class ScrollCaptureControlBar : public QWidget
{
    Q_OBJECT

public:
    explicit ScrollCaptureControlBar(QWidget *parent = nullptr);

    void setStatusText(const QString &text);
    void setAutoAssistText(const QString &text);
    void setAutoAssistEnabled(bool enabled);

    void setFocusText(const QString &text);
    void setFocusVisible(bool visible);
    void setFocusEnabled(bool enabled);

    void setMetaText(const QString &text);
    void setPreviewImage(const QImage &preview);

    bool detailsExpanded() const;
    void setDetailsExpanded(bool expanded);

    bool hasManualPosition() const;
    void positionNear(const QRect &captureRegion, QScreen *screen);

signals:
    void autoAssistRequested();
    void focusRequested();
    void finishRequested();
    void cancelRequested();
    void detailsExpandedChanged(bool expanded);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    bool hitInteractiveChild(const QPoint &pos) const;

    QWidget *m_detailsWidget = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_metaLabel = nullptr;
    QLabel *m_previewLabel = nullptr;
    QPushButton *m_autoAssistButton = nullptr;
    QPushButton *m_focusButton = nullptr;
    QPushButton *m_detailsButton = nullptr;
    QPushButton *m_finishButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QShortcut *m_escapeShortcut = nullptr;

    bool m_dragging = false;
    QPoint m_dragAnchorGlobal;
    QPoint m_dragAnchorPos;
    bool m_manualPositionSet = false;
};

#endif // SCROLLCAPTURECONTROLBAR_H
