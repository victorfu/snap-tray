#pragma once

#include "utils/FilenameTemplateEngine.h"
#include "utils/ImageExportOptions.h"

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

class QScreen;
class QWidget;
class QWindow;

namespace SnapTray {

class ImageExportViewModel;
class QmlDialog;

/**
 * @brief Shows the Export dialog for one composed image and writes the result.
 *
 * The one entry point shared by pins, Beautify and the Library (#357). The
 * caller hands over the finished image (annotations, beautify and color space
 * already applied); this class never touches the caller's original. The file
 * written is byte for byte the candidate whose size the dialog showed.
 */
class ImageExportDialogController : public QObject
{
    Q_OBJECT

public:
    struct Request {
        QImage image;
        // Naming context for the suggested file name. Width, height and
        // extension are filled in from the chosen output.
        FilenameTemplateEngine::Context nameContext;
        QWidget* parentWidget = nullptr;   // owner of the dialogs, may be null
        QWindow* parentWindow = nullptr;   // transient parent when there is no widget
        QScreen* screen = nullptr;         // where to center the dialog
    };

    // title, suggested path, name filter -> chosen path (empty when cancelled)
    using SaveFileDialog =
        std::function<QString(const QString&, const QString&, const QString&)>;

    explicit ImageExportDialogController(QObject* parent = nullptr);
    ~ImageExportDialogController() override;

    // Opens the dialog. A dialog that is already open is closed first, so a
    // stale image can never be exported by mistake. Returns false for a null image.
    bool open(const Request& request);
    void close();
    bool isOpen() const;

    ImageExportViewModel* viewModel() const;
    // Tests: no real windows, and a scripted file dialog.
    void setShowDialogForTesting(bool show) { m_showDialog = show; }
    void setSaveFileDialogForTesting(SaveFileDialog dialog) { m_saveFileDialog = std::move(dialog); }

    // Makes `chosenPath` end in a suffix that matches `options`' format.
    static QString pathWithFormatSuffix(const QString& chosenPath, const ImageExportOptions& options);

signals:
    void exported(const QString& filePath, const QSize& pixelSize, qint64 bytes);
    void closed();

private:
    void handleSaveRequested();
    QString suggestedPath(const ImageExportOptions& options, const QSize& pixelSize) const;

    Request m_request;
    QPointer<ImageExportViewModel> m_viewModel;
    QPointer<QmlDialog> m_dialog;
    SaveFileDialog m_saveFileDialog;
    bool m_showDialog = true;
    bool m_saving = false;
};

} // namespace SnapTray
