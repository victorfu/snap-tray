#pragma once

#include "qml/ToolbarViewModelBase.h"

/**
 * @brief ViewModel exposing RegionSelector toolbar state to QML.
 *
 * Builds a button list from ToolRegistry::getToolsForToolbar(RegionSelector),
 * tracks active tool and undo/redo/share state, and forwards QML clicks
 * back to RegionSelector via signals.
 *
 * Supports multi-region mode which rebuilds the button list to show
 * only Done and Cancel buttons.
 */
class RegionToolbarViewModel : public ToolbarViewModelBase
{
    Q_OBJECT

public:
    explicit RegionToolbarViewModel(QObject* parent = nullptr);

    /**
     * @brief Switch between normal and multi-region toolbar layouts.
     */
    void setMultiRegionMode(bool enabled);
    bool multiRegionMode() const { return m_multiRegionMode; }

signals:
    // Action signals (QML → C++)
    void toolSelected(int toolId);
    void undoClicked();
    void redoClicked();
    void cancelClicked();
    void ocrClicked();
    void qrCodeClicked();
    void pinClicked();
    void recordClicked();
    void shareClicked();
    void saveClicked();
    void copyClicked();
    void multiRegionToggled();
    void multiRegionDoneClicked();

public slots:
    void handleButtonClicked(int buttonId);

private:
    void buildButtonList();
    void buildMultiRegionButtonList();
    bool m_multiRegionMode = false;
};
