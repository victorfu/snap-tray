#ifndef LASERPOINTERTOOLHANDLER_H
#define LASERPOINTERTOOLHANDLER_H

#include "../IToolHandler.h"

#include <QPoint>

/**
 * @brief Tool handler for laser pointer mode.
 *
 * This is a toggle tool that shows a laser pointer at the cursor
 * position. The actual rendering is typically handled by a
 * LaserPointerRenderer in the parent widget.
 */
class LaserPointerToolHandler : public IToolHandler {
public:
    LaserPointerToolHandler() = default;
    ~LaserPointerToolHandler() override = default;

    ToolId toolId() const override { return ToolId::LaserPointer; }

    void onActivate(ToolContext* ctx) override;
    void onDeactivate(ToolContext* ctx) override;
    void onMouseMove(ToolContext* ctx, const QPoint& pos) override;

    bool isDrawing() const override { return false; }

    bool supportsColor() const override { return false; }
    bool supportsWidth() const override { return false; }

    QCursor cursor() const override;

    // Laser pointer specific
    bool isActive() const { return m_isActive; }
    QPoint currentPosition() const { return m_currentPos; }

private:
    bool m_isActive = false;
    QPoint m_currentPos;
};

#endif // LASERPOINTERTOOLHANDLER_H
