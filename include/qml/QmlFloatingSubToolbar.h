#pragma once

#include <QObject>
#include <QRect>
#include <QString>

class QQuickView;
class QQuickItem;
class PinToolOptionsViewModel;
class QWidget;
class QWindow;

namespace SnapTray {

/**
 * @brief QML-based floating sub-toolbar overlay for tool options.
 *
 * Renders ToolOptionsStrip.qml with glass-effect styling.
 * Used by RegionSelector, ScreenCanvas (with external ViewModel),
 * and PinWindow (with internally-owned ViewModel).
 */
class QmlFloatingSubToolbar : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Construct with an external ViewModel. Ownership is NOT transferred.
     */
    explicit QmlFloatingSubToolbar(PinToolOptionsViewModel* viewModel,
                                   QObject* parent = nullptr);

    /**
     * @brief Construct with an internally-owned ViewModel (created automatically).
     */
    explicit QmlFloatingSubToolbar(QObject* parent = nullptr);

    ~QmlFloatingSubToolbar() override;

    void show();
    void hide();
    void close();

    bool isVisible() const;

    void positionBelow(const QRect& toolbarRect);

    QRect geometry() const;
    QWindow* window() const;

    PinToolOptionsViewModel* viewModel() const;

    /**
     * @brief Show tool options for the given tool.
     * Delegates to ViewModel::showForTool(). Hides if tool has no options.
     */
    void showForTool(int toolId);

    /**
     * @brief Set the parent widget for coordinate mapping.
     * When set, the NSWindow level is adjusted to parent+1.
     * When not set, NSFloatingWindowLevel is used.
     */
    void setParentWidget(QWidget* parent);

signals:
    void emojiPickerRequested();

private:
    void ensureView();
    void applyPlatformWindowFlags();
    void syncTransientParent();
    void syncCursorSurface();
    bool eventFilter(QObject* obj, QEvent* event) override;

    PinToolOptionsViewModel* m_viewModel = nullptr;
    QQuickView* m_view = nullptr;
    QQuickItem* m_rootItem = nullptr;
    QWidget* m_parentWidget = nullptr;
    QString m_cursorSurfaceId;
    QString m_cursorOwnerId;
};

} // namespace SnapTray
