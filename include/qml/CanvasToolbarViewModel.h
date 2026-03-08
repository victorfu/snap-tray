#pragma once

#include <QObject>
#include <QVariantList>

/**
 * @brief ViewModel exposing ScreenCanvas toolbar state to QML.
 *
 * Builds a button list from ToolRegistry::getToolsForToolbar(ScreenCanvas),
 * tracks active tool and undo/redo state, and forwards QML clicks
 * back to ScreenCanvas via signals.
 */
class CanvasToolbarViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList buttons READ buttons CONSTANT)
    Q_PROPERTY(int activeTool READ activeTool WRITE setActiveTool NOTIFY activeToolChanged)
    Q_PROPERTY(bool canUndo READ canUndo WRITE setCanUndo NOTIFY canUndoChanged)
    Q_PROPERTY(bool canRedo READ canRedo WRITE setCanRedo NOTIFY canRedoChanged)
    // Not used by ScreenCanvas but required by FloatingToolbar.qml interface
    Q_PROPERTY(bool ocrAvailable READ ocrAvailable CONSTANT)
    Q_PROPERTY(bool shareInProgress READ shareInProgress CONSTANT)

public:
    explicit CanvasToolbarViewModel(QObject* parent = nullptr);

    QVariantList buttons() const;

    int activeTool() const;
    void setActiveTool(int toolId);

    bool canUndo() const;
    void setCanUndo(bool value);

    bool canRedo() const;
    void setCanRedo(bool value);

    bool ocrAvailable() const { return false; }
    bool shareInProgress() const { return false; }

signals:
    void activeToolChanged();
    void canUndoChanged();
    void canRedoChanged();

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

    QVariantList m_buttons;
    int m_activeTool = -1;
    bool m_canUndo = false;
    bool m_canRedo = false;
};
