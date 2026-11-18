#pragma once

#include <QObject>
#include <QRect>
#include <QStringList>
#include <functional>

class QImage;
class QWidget;
namespace SnapTray {
class QmlDialog;

class RedactionReview : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList thumbnails READ thumbnails CONSTANT)
public:
    RedactionReview(const QImage& image, const QVector<QRect>& rects);
    ~RedactionReview() override;
    QStringList thumbnails() const;
    Q_INVOKABLE QPoint origin(int index) const { return m_origins.value(index); }
    Q_INVOKABLE bool isSelected(int index) const { return m_selected.value(index); }
    Q_INVOKABLE void select(int index, bool selected);
    QVector<QRect> selected(const QVector<QRect>& regions, int offset) const;

signals:
    void apply();
    void cancel();

private:
    QStringList m_ids;
    QVector<bool> m_selected;
    QVector<QPoint> m_origins;
};

QString redactionSummary(int faces, int textRegions);

using ReviewCallback = std::function<void(bool, QVector<QRect>, QVector<QRect>)>;
QmlDialog* reviewRedactions(QWidget* owner, const QImage& image, const QVector<QRect>& faces,
                            const QVector<QRect>& text, ReviewCallback callback);
}
