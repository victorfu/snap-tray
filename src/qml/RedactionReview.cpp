#include "qml/RedactionReview.h"
#include "qml/DialogImageProvider.h"
#include "qml/QmlDialog.h"

#include <QCoreApplication>
#include <QUuid>
#include <QWidget>

namespace SnapTray {
RedactionReview::RedactionReview(const QImage& image, const QVector<QRect>& rects)
    : m_selected(rects.size(), true)
{
    const QString prefix = QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (const auto& rect : rects) {
        const QString id = prefix + '-' + QString::number(m_ids.size());
        const QImage crop = image.copy(rect.intersected(image.rect()));
        const QSize thumbnail = crop.size().boundedTo(QSize(320, 64));
        DialogImageProvider::setImage(id, crop.scaled(thumbnail, Qt::KeepAspectRatio,
                                                      Qt::SmoothTransformation));
        m_ids.append(id);
        m_origins.append(rect.topLeft());
    }
}

RedactionReview::~RedactionReview()
{
    for (const auto& id : m_ids)
        DialogImageProvider::removeImage(id);
}

QStringList RedactionReview::thumbnails() const
{
    QStringList urls;
    for (const auto& id : m_ids)
        urls.append("image://dialog/" + id);
    return urls;
}

void RedactionReview::select(int index, bool selected)
{
    if (index >= 0 && index < m_selected.size())
        m_selected[index] = selected;
}

QVector<QRect> RedactionReview::selected(const QVector<QRect>& regions, int offset) const
{
    QVector<QRect> result;
    for (int i = 0; i < regions.size(); ++i) {
        if (m_selected.value(offset + i))
            result.append(regions[i]);
    }
    return result;
}

QString redactionSummary(int faces, int textRegions)
{
    if (!faces && !textRegions)
        return QCoreApplication::translate("RedactionReview", "No faces or sensitive text detected");
    return QCoreApplication::translate("RedactionReview", "Blurred faces: %1 · Text regions: %2")
        .arg(faces).arg(textRegions);
}

QmlDialog* reviewRedactions(QWidget* owner, const QImage& image, const QVector<QRect>& faces,
                            const QVector<QRect>& text, ReviewCallback callback)
{
    auto* review = new RedactionReview(image, faces + text);
    auto* dialog = new QmlDialog(QUrl("qrc:/SnapTrayQml/dialogs/RedactionReview.qml"),
                                 review, "viewModel", owner);
    dialog->setModal(true);
    QObject::connect(review, &RedactionReview::apply, dialog, [dialog, review, faces, text, callback] {
        QObject::disconnect(dialog, &QmlDialog::closed, review, nullptr);
        dialog->close();
        callback(true, review->selected(faces, 0), review->selected(text, faces.size()));
    });
    QObject::connect(review, &RedactionReview::cancel, dialog, &QmlDialog::close);
    QObject::connect(dialog, &QmlDialog::closed, review, [dialog, callback] {
        dialog->deleteLater();
        callback(false, {}, {});
    }, Qt::SingleShotConnection);
    return dialog;
}
}
