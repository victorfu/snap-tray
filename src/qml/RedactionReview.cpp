#include "qml/RedactionReview.h"
#include "qml/DialogImageProvider.h"
#include "qml/QmlDialog.h"

#include <QCoreApplication>
#include <QUuid>
#include <QWidget>

namespace SnapTray {
RedactionReview::RedactionReview(const QImage& image, const QVector<QRect>& faces,
                                 const QVector<QRect>& text, const QString& warning)
    : m_warning(warning)
{
    constexpr QSize kThumbnailBounds(320, 64);
    const QString prefix = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto append = [&](const QVector<QRect>& regions, bool face) {
        for (const auto& rect : regions) {
            const QRect clipped = rect.intersected(image.rect());
            if (clipped.isEmpty())
                continue;
            const QString id = prefix + '-' + QString::number(m_entries.size());
            const QImage crop = image.copy(clipped);
            DialogImageProvider::setImage(id, crop.scaled(crop.size().boundedTo(kThumbnailBounds),
                                                         Qt::KeepAspectRatio, Qt::SmoothTransformation));
            m_entries.append({id, clipped, face});
        }
    };
    append(faces, true);
    append(text, false);
}

RedactionReview::~RedactionReview()
{
    for (const auto& entry : m_entries)
        DialogImageProvider::removeImage(entry.imageId);
}

QStringList RedactionReview::thumbnails() const
{
    QStringList urls;
    for (const auto& entry : m_entries)
        urls.append("image://dialog/" + entry.imageId);
    return urls;
}

void RedactionReview::select(int index, bool selected)
{
    if (index >= 0 && index < m_entries.size())
        m_entries[index].selected = selected;
}

QVector<QRect> RedactionReview::selected(bool faces) const
{
    QVector<QRect> result;
    for (const auto& entry : m_entries) {
        if (entry.selected && entry.face == faces)
            result.append(entry.rect);
    }
    return result;
}

QVector<QRect> RedactionReview::selectedFaces() const { return selected(true); }
QVector<QRect> RedactionReview::selectedText() const { return selected(false); }

QString redactionSummary(int faces, int textRegions)
{
    if (!faces && !textRegions)
        return QCoreApplication::translate("RedactionReview", "No faces or sensitive text detected");
    return QCoreApplication::translate("RedactionReview", "Blurred faces: %1 · Text regions: %2")
        .arg(faces).arg(textRegions);
}

QmlDialog* reviewRedactions(QWidget* owner, const QImage& image, const QVector<QRect>& faces,
                            const QVector<QRect>& text, ReviewCallback callback, const QString& warning)
{
    auto* review = new RedactionReview(image, faces, text, warning);
    auto* dialog = new QmlDialog(QUrl("qrc:/SnapTrayQml/dialogs/RedactionReview.qml"),
                                 review, "viewModel", owner);
    dialog->setModal(true);
    if (!dialog->prepare()) {
        delete dialog;
        return nullptr;
    }
    QObject::connect(review, &RedactionReview::apply, dialog, [dialog, review, callback] {
        QObject::disconnect(dialog, &QmlDialog::closed, review, nullptr);
        auto selectedFaces = review->selectedFaces();
        auto selectedText = review->selectedText();
        dialog->close();
        callback(true, std::move(selectedFaces), std::move(selectedText));
    });
    QObject::connect(review, &RedactionReview::cancel, dialog, &QmlDialog::close);
    QObject::connect(dialog, &QmlDialog::closed, review, [dialog, callback] {
        dialog->deleteLater();
        callback(false, {}, {});
    }, Qt::SingleShotConnection);
    return dialog;
}

void abandonRedactionReview(QPointer<QmlDialog>& dialog)
{
    if (dialog) {
        // The owner is closing. Release the modal window without invoking a
        // completion callback that still refers to its widgets.
        dialog->blockSignals(true);
        delete dialog.data();
    }
}
}
