#pragma once

#include "cursor/CursorManager.h"

/**
 * RAII scope guard for cursor management.
 *
 * Automatically pops the cursor when the scope exits, ensuring cursor
 * state is properly cleaned up even if an exception occurs or control
 * flow exits early.
 *
 * Note: For operations that span multiple events (like drag operations
 * from mousePressEvent to mouseReleaseEvent), this class cannot be used
 * directly. Instead, ensure the widget destructor calls clearAllForWidget()
 * to clean up any residual cursor state.
 *
 * Usage:
 *   void someFunction() {
 *       CursorScope scope(myWidget, CursorContext::Override, Qt::WaitCursor);
 *       // ... do work ...
 *       // cursor automatically restored when scope exits
 *   }
 */
class CursorScope
{
public:
    explicit CursorScope(QWidget* widget, CursorContext context, Qt::CursorShape shape)
        : m_widget(widget)
        , m_context(context)
        , m_active(true)
    {
        CursorManager::instance().pushCursorForWidget(widget, context, shape);
    }

    explicit CursorScope(QWidget* widget, CursorContext context, const QCursor& cursor)
        : m_widget(widget)
        , m_context(context)
        , m_active(true)
    {
        CursorManager::instance().pushCursorForWidget(widget, context, cursor);
    }

    ~CursorScope()
    {
        if (m_active && m_widget) {
            CursorManager::instance().popCursorForWidget(m_widget, m_context);
        }
    }

    // Release the cursor early (before scope exit)
    void release()
    {
        if (m_active && m_widget) {
            CursorManager::instance().popCursorForWidget(m_widget, m_context);
            m_active = false;
        }
    }

    // Check if this scope is still active
    bool isActive() const { return m_active; }

    // Disable copy
    CursorScope(const CursorScope&) = delete;
    CursorScope& operator=(const CursorScope&) = delete;

    // Allow move (transfers ownership)
    CursorScope(CursorScope&& other) noexcept
        : m_widget(other.m_widget)
        , m_context(other.m_context)
        , m_active(other.m_active)
    {
        other.m_active = false;
    }

    CursorScope& operator=(CursorScope&& other) noexcept
    {
        if (this != &other) {
            if (m_active && m_widget) {
                CursorManager::instance().popCursorForWidget(m_widget, m_context);
            }
            m_widget = other.m_widget;
            m_context = other.m_context;
            m_active = other.m_active;
            other.m_active = false;
        }
        return *this;
    }

private:
    QWidget* m_widget;
    CursorContext m_context;
    bool m_active;
};
