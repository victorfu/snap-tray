#pragma once

#include <QObject>
#include <QVariantList>

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
class RegionToolbarViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList buttons READ buttons NOTIFY buttonsChanged)
    Q_PROPERTY(int activeTool READ activeTool WRITE setActiveTool NOTIFY activeToolChanged)
    Q_PROPERTY(bool canUndo READ canUndo WRITE setCanUndo NOTIFY canUndoChanged)
    Q_PROPERTY(bool canRedo READ canRedo WRITE setCanRedo NOTIFY canRedoChanged)
    Q_PROPERTY(bool ocrAvailable READ ocrAvailable WRITE setOCRAvailable NOTIFY ocrAvailableChanged)
    Q_PROPERTY(bool shareInProgress READ shareInProgress WRITE setShareInProgress NOTIFY shareInProgressChanged)

public:
    explicit RegionToolbarViewModel(QObject* parent = nullptr);

    QVariantList buttons() const;

    int activeTool() const;
    void setActiveTool(int toolId);

    bool canUndo() const;
    void setCanUndo(bool value);

    bool canRedo() const;
    void setCanRedo(bool value);

    bool ocrAvailable() const;
    void setOCRAvailable(bool value);

    bool shareInProgress() const;
    void setShareInProgress(bool value);

    /**
     * @brief Switch between normal and multi-region toolbar layouts.
     */
    void setMultiRegionMode(bool enabled);
    bool multiRegionMode() const { return m_multiRegionMode; }

signals:
    void buttonsChanged();
    void activeToolChanged();
    void canUndoChanged();
    void canRedoChanged();
    void ocrAvailableChanged();
    void shareInProgressChanged();

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

    QVariantList m_buttons;
    int m_activeTool = -1;
    bool m_canUndo = false;
    bool m_canRedo = false;
    bool m_ocrAvailable = true;
    bool m_shareInProgress = false;
    bool m_multiRegionMode = false;
};
