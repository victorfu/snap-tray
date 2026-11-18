#pragma once

#include <QObject>
#include <QRect>
#include <QPointer>
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
    Q_PROPERTY(QString warning MEMBER m_warning CONSTANT)
public:
    RedactionReview(const QImage& image, const QVector<QRect>& faces, const QVector<QRect>& text,
                    const QString& warning = {});
    ~RedactionReview() override;
    QStringList thumbnails() const;
    Q_INVOKABLE QPoint origin(int index) const { return m_entries.value(index).rect.topLeft(); }
    Q_INVOKABLE bool isSelected(int index) const { return index >= 0 && index < m_entries.size() && m_entries[index].selected; }
    Q_INVOKABLE void select(int index, bool selected);
    QVector<QRect> selectedFaces() const;
    QVector<QRect> selectedText() const;

signals:
    void apply();
    void cancel();

private:
    struct Entry {
        QString imageId;
        QRect rect;
        bool face;
        bool selected = true;
    };
    QVector<QRect> selected(bool faces) const;
    QVector<Entry> m_entries;
    QString m_warning;
};

QString redactionSummary(int faces, int textRegions);

using ReviewCallback = std::function<void(bool, QVector<QRect>, QVector<QRect>)>;
QmlDialog* reviewRedactions(QWidget* owner, const QImage& image, const QVector<QRect>& faces,
                            const QVector<QRect>& text, ReviewCallback callback,
                            const QString& warning = {});
void abandonRedactionReview(QPointer<QmlDialog>& dialog);
}
