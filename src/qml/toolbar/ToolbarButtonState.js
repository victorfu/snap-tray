.pragma library

function isDisabled(button, viewModel) {
    if (!viewModel) return true
    if (button.isUndo) return !viewModel.canUndo
    if (button.isRedo) return !viewModel.canRedo
    if (button.isShare) return viewModel.shareInProgress || viewModel.autoBlurProcessing
    if (button.isExportAction) return viewModel.autoBlurProcessing
    return false
}
