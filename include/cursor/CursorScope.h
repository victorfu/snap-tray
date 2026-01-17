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
 * directly. Instead, ensure the widget destructor calls clearAll() or
 * clearAllForWidget() to clean up any residual cursor state.
 *
 * Usage:
 *   void someFunction() {
 *       CursorScope scope(CursorContext::Override, Qt::WaitCursor);
 *       // ... do work ...
 *       // cursor automatically restored when scope exits
 *   }
 */
class CursorScope
{
public:
    explicit CursorScope(CursorContext context, Qt::CursorShape shape)
        : m_context(context)
        , m_active(true)
    {
        CursorManager::instance().pushCursor(context, shape);
    }

    explicit CursorScope(CursorContext context, const QCursor& cursor)
        : m_context(context)
        , m_active(true)
    {
        CursorManager::instance().pushCursor(context, cursor);
    }

    ~CursorScope()
    {
        if (m_active) {
            CursorManager::instance().popCursor(m_context);
        }
    }

    // Release the cursor early (before scope exit)
    void release()
    {
        if (m_active) {
            CursorManager::instance().popCursor(m_context);
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
        : m_context(other.m_context)
        , m_active(other.m_active)
    {
        other.m_active = false;
    }

    CursorScope& operator=(CursorScope&& other) noexcept
    {
        if (this != &other) {
            if (m_active) {
                CursorManager::instance().popCursor(m_context);
            }
            m_context = other.m_context;
            m_active = other.m_active;
            other.m_active = false;
        }
        return *this;
    }

private:
    CursorContext m_context;
    bool m_active;
};
