#pragma once

#include <QObject>
#include <QVariantList>

/**
 * @brief ViewModel exposing PinWindow toolbar state to QML.
 *
 * Builds a button list from ToolRegistry::getToolsForToolbar(PinWindow),
 * tracks active tool and undo/redo/share state, and forwards QML clicks
 * back to PinWindow via signals.
 */
class PinToolbarViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList buttons READ buttons CONSTANT)
    Q_PROPERTY(int activeTool READ activeTool WRITE setActiveTool NOTIFY activeToolChanged)
    Q_PROPERTY(bool canUndo READ canUndo WRITE setCanUndo NOTIFY canUndoChanged)
    Q_PROPERTY(bool canRedo READ canRedo WRITE setCanRedo NOTIFY canRedoChanged)
    Q_PROPERTY(bool ocrAvailable READ ocrAvailable WRITE setOCRAvailable NOTIFY ocrAvailableChanged)
    Q_PROPERTY(bool shareInProgress READ shareInProgress WRITE setShareInProgress NOTIFY shareInProgressChanged)

public:
    explicit PinToolbarViewModel(QObject* parent = nullptr);

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

    // Local-only button ID for "Done" (not a ToolId)
    static constexpr int ButtonDone = 10001;

signals:
    void activeToolChanged();
    void canUndoChanged();
    void canRedoChanged();
    void ocrAvailableChanged();
    void shareInProgressChanged();

    // Action signals (QML → C++)
    void toolSelected(int toolId);
    void undoClicked();
    void redoClicked();
    void ocrClicked();
    void qrCodeClicked();
    void shareClicked();
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

    QVariantList m_buttons;
    int m_activeTool = -1;
    bool m_canUndo = false;
    bool m_canRedo = false;
    bool m_ocrAvailable = true;
    bool m_shareInProgress = false;
};
