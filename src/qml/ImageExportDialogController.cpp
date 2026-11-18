#include "qml/ImageExportDialogController.h"

#include "qml/ImageExportViewModel.h"
#include "qml/QmlDialog.h"
#include "settings/FileSettingsManager.h"
#include "utils/ImageSaveUtils.h"
#include "utils/NativeFileDialogUtils.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QScopeGuard>
#include <QScreen>
#include <QUrl>
#include <QWidget>
#include <QWindow>

namespace SnapTray {

namespace {

QString nameFilterFor(const ImageExportOptions& options)
{
    return options.format == ImageExportOptions::Format::Jpeg
        ? ImageExportDialogController::tr("JPEG Image (*.jpg *.jpeg)")
        : ImageExportDialogController::tr("PNG Image (*.png)");
}

QString saveErrorDetail(const ImageSaveUtils::Error& error)
{
    if (error.stage.isEmpty()) {
        return error.message.isEmpty() ? QStringLiteral("Unknown error") : error.message;
    }
    if (error.message.isEmpty()) {
        return error.stage;
    }
    return QStringLiteral("%1: %2").arg(error.stage, error.message);
}

} // namespace

ImageExportDialogController::ImageExportDialogController(QObject* parent)
    : QObject(parent)
{
}

ImageExportDialogController::~ImageExportDialogController()
{
    if (m_dialog) {
        m_dialog->disconnect(this);
        m_dialog->close();
    }
}

bool ImageExportDialogController::isOpen() const
{
    return !m_viewModel.isNull();
}

ImageExportViewModel* ImageExportDialogController::viewModel() const
{
    return m_viewModel.data();
}

bool ImageExportDialogController::open(const Request& request)
{
    if (request.image.isNull()) {
        return false;
    }
    close();

    m_request = request;
    auto* viewModel = new ImageExportViewModel(
        request.image, FileSettingsManager::instance().loadImageExportOptions());
    m_viewModel = viewModel;
    connect(viewModel, &ImageExportViewModel::saveRequested,
            this, &ImageExportDialogController::handleSaveRequested);
    connect(viewModel, &ImageExportViewModel::cancelRequested,
            this, &ImageExportDialogController::close);

    if (!m_showDialog) {
        viewModel->setParent(this);
        return true;
    }

    // A widget parent also makes the dialog a transient child of its window.
    QObject* dialogParent = request.parentWidget ? static_cast<QObject*>(request.parentWidget)
                                                 : static_cast<QObject*>(this);
    auto* dialog = new QmlDialog(QUrl(QStringLiteral("qrc:/SnapTrayQml/dialogs/ImageExportDialog.qml")),
                                 viewModel, QStringLiteral("viewModel"), dialogParent);
    m_dialog = dialog;
    connect(dialog, &QmlDialog::closed, this, [this, dialog]() {
        // Closed by the window manager, the dialog would otherwise live on
        // with the image it holds.
        dialog->deleteLater();
        if (m_dialog == dialog) {
            m_dialog = nullptr;
            m_viewModel = nullptr;
            m_request = Request();
            emit closed();
        }
    });
    QScreen* screen = request.screen ? request.screen : QGuiApplication::primaryScreen();
    dialog->showCenteredOnScreen(screen);
    return true;
}

void ImageExportDialogController::close()
{
    if (m_dialog) {
        m_dialog->close();   // emits closed(), handled above
        return;
    }
    if (m_viewModel) {
        m_viewModel->deleteLater();
        m_viewModel = nullptr;
        m_request = Request();
        emit closed();
    }
}

QString ImageExportDialogController::pathWithFormatSuffix(const QString& chosenPath,
                                                          const ImageExportOptions& options)
{
    if (chosenPath.isEmpty()) {
        return chosenPath;
    }
    const QString suffix = QFileInfo(chosenPath).suffix().toLower();
    if (options.acceptedSuffixes().contains(suffix)) {
        return chosenPath;
    }
    return chosenPath + QLatin1Char('.') + options.suffix();
}

QString ImageExportDialogController::suggestedPath(const ImageExportOptions& options,
                                                   const QSize& pixelSize) const
{
    auto& fileSettings = FileSettingsManager::instance();
    const QString directory = fileSettings.resolveManualScreenshotSaveDirectory(
        fileSettings.loadUseLastScreenshotSaveLocation());
    FilenameTemplateEngine::Context context = m_request.nameContext;
    context.width = pixelSize.width();
    context.height = pixelSize.height();
    context.ext = options.suffix();
    context.outputDir = directory;
    if (context.dateFormat.isEmpty()) {
        context.dateFormat = fileSettings.loadDateFormat();
    }
    return FilenameTemplateEngine::buildUniqueFilePath(
        directory, fileSettings.loadFilenameTemplate(), context, 1);
}

void ImageExportDialogController::handleSaveRequested()
{
    if (!m_viewModel || m_saving) {
        return;
    }
    // Hold on to the candidate now: the options may change while the file
    // dialog is open, and what is written must be what was shown.
    const std::optional<ImageExport::Encoded> candidate = m_viewModel->candidate();
    if (!candidate || !candidate->isValid()) {
        return;
    }
    m_saving = true;
    // The file dialog spins an event loop: the owner (a pin, say) may be
    // closed and this object deleted before it returns.
    const QPointer<ImageExportDialogController> self(this);
    const auto savingGuard = qScopeGuard([self]() {
        if (self) self->m_saving = false;
    });
    const QPointer<ImageExportViewModel> viewModel = m_viewModel;
    viewModel->setErrorText(QString());

    const QString title = tr("Export Image");
    const QString defaultPath = suggestedPath(candidate->options, candidate->pixelSize);
    const QString filter = nameFilterFor(candidate->options);
    const QString chosen = m_saveFileDialog
        ? m_saveFileDialog(title, defaultPath, filter)
        : NativeFileDialogUtils::getSaveFileName(
              m_request.parentWidget, title, defaultPath, filter, m_request.parentWindow);
    if (!self || chosen.isEmpty() || !viewModel || viewModel != m_viewModel) {
        return;   // cancelled, or the dialog went away meanwhile
    }

    const QString filePath = pathWithFormatSuffix(chosen, candidate->options);
    if (filePath != chosen && QFileInfo::exists(filePath)) {
        // The file dialog only confirmed replacing the name the user typed.
        viewModel->setErrorText(
            tr("%1 already exists. Choose a name ending in .%2.")
                .arg(QFileInfo(filePath).fileName(), candidate->options.suffix()));
        return;
    }

    ImageSaveUtils::Error saveError;
    if (!ImageSaveUtils::saveEncodedAtomically(candidate->data, filePath, &saveError)) {
        // The dialog stays open so the export can be tried again.
        viewModel->setErrorText(tr("Failed to export image: %1").arg(saveErrorDetail(saveError)));
        return;
    }

    auto& fileSettings = FileSettingsManager::instance();
    fileSettings.saveImageExportOptions(candidate->options);
    if (fileSettings.loadUseLastScreenshotSaveLocation()) {
        fileSettings.rememberManualScreenshotSaveDirectory(filePath);
    }
    emit exported(filePath, candidate->pixelSize, candidate->data.size());
    close();
}

} // namespace SnapTray
