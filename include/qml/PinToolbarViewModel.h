#pragma once

#include "qml/ToolbarViewModelBase.h"

/**
 * @brief ViewModel exposing PinWindow toolbar state to QML.
 *
 * Builds a button list from ToolRegistry::getToolsForToolbar(PinWindow),
 * tracks active tool and undo/redo/share state, and forwards QML clicks
 * back to PinWindow via signals.
 */
class PinToolbarViewModel : public ToolbarViewModelBase
{
    Q_OBJECT

public:
    explicit PinToolbarViewModel(QObject* parent = nullptr);

    // Local-only button ID for "Done" (not a ToolId)
    static constexpr int ButtonDone = 10001;

signals:
    // Action signals (QML → C++)
    void toolSelected(int toolId);
    void undoClicked();
    void redoClicked();
    void ocrClicked();
    void qrCodeClicked();
    void shareClicked();
    void beautifyClicked();
    void copyClicked();
    void saveClicked();
    void doneClicked();

public slots:
    /**
     * @brief Called from QML when a button is clicked.
     * Dispatches to the correct signal based on button type.
     */
    void handleButtonClicked(int buttonId);

private:
    void buildButtonList();
};
