#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include "tools/ToolId.h"

/**
 * Shared base for QML toolbar view models.
 *
 * Keeps the QML-facing state contract consistent across PinWindow,
 * RegionSelector, and ScreenCanvas toolbars.
 */
class ToolbarViewModelBase : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList buttons READ buttons NOTIFY buttonsChanged)
    Q_PROPERTY(int activeTool READ activeTool WRITE setActiveTool NOTIFY activeToolChanged)
    Q_PROPERTY(bool canUndo READ canUndo WRITE setCanUndo NOTIFY canUndoChanged)
    Q_PROPERTY(bool canRedo READ canRedo WRITE setCanRedo NOTIFY canRedoChanged)
    Q_PROPERTY(bool ocrAvailable READ ocrAvailable WRITE setOCRAvailable NOTIFY ocrAvailableChanged)
    Q_PROPERTY(bool shareInProgress READ shareInProgress WRITE setShareInProgress NOTIFY shareInProgressChanged)

public:
    struct ToolButtonOptions {
        bool separatorBefore = false;
        bool isDrawing = false;
        bool isOCR = false;
        bool isUndo = false;
        bool isRedo = false;
        bool isShare = false;
        bool isAction = false;
        bool isCancel = false;
        bool isRecord = false;
    };

    explicit ToolbarViewModelBase(QObject* parent = nullptr);

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

signals:
    void buttonsChanged();
    void activeToolChanged();
    void canUndoChanged();
    void canRedoChanged();
    void ocrAvailableChanged();
    void shareInProgressChanged();

protected:
    ToolButtonOptions defaultToolButtonOptions(ToolId toolId) const;
    QVariantMap buildToolButtonEntry(ToolId toolId, const ToolButtonOptions& options) const;
    QVariantMap buildCustomButtonEntry(int id,
                                       const QString& iconKey,
                                       const QString& iconSource,
                                       const QString& tooltip,
                                       const ToolButtonOptions& options) const;
    void setButtons(QVariantList buttons);

private:
    QVariantList m_buttons;
    int m_activeTool = -1;
    bool m_canUndo = false;
    bool m_canRedo = false;
    bool m_ocrAvailable = true;
    bool m_shareInProgress = false;
};

