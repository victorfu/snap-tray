#ifndef CURSORSTYLECATALOG_H
#define CURSORSTYLECATALOG_H

#include <QCursor>
#include <QHash>

#include "cursor/CursorTypes.h"

class CursorStyleCatalog
{
public:
    static CursorStyleCatalog& instance();

    CursorStyleCatalog(const CursorStyleCatalog&) = delete;
    CursorStyleCatalog& operator=(const CursorStyleCatalog&) = delete;

    QCursor cursorForStyle(const CursorStyleSpec& spec) const;
    QCursor mosaicCursor(int size) const;
    QCursor eraserCursor(int diameter) const;

private:
    CursorStyleCatalog() = default;

    QCursor cachedBrushCursor(const QString& family, int logicalSize) const;

    mutable QHash<QString, QCursor> m_brushCursorCache;
};

#endif // CURSORSTYLECATALOG_H
