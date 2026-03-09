#pragma once

#include "qml/ToolbarViewModelBase.h"

/**
 * @brief ViewModel exposing ScreenCanvas toolbar state to QML.
 *
 * Builds a button list from ToolRegistry::getToolsForToolbar(ScreenCanvas),
 * tracks active tool and undo/redo state, and forwards QML clicks
 * back to ScreenCanvas via signals.
 */
class CanvasToolbarViewModel : public ToolbarViewModelBase
{
    Q_OBJECT

public:
    explicit CanvasToolbarViewModel(QObject* parent = nullptr);

signals:
    // Action signals (QML → C++)
    void toolSelected(int toolId);
    void undoClicked();
    void redoClicked();
    void clearClicked();
    void exitClicked();
    void canvasWhiteboardClicked();
    void canvasBlackboardClicked();

public slots:
    void handleButtonClicked(int buttonId);

private:
    void buildButtonList();
};
