#ifndef CURSORSTYLECATALOG_H
#define CURSORSTYLECATALOG_H

#include <QCursor>

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
};

#endif // CURSORSTYLECATALOG_H
