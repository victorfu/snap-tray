#ifndef LINESTYLE_H
#define LINESTYLE_H

/**
 * @brief Line style for stroke annotations.
 *
 * Defines different dash patterns for drawing lines.
 */
enum class LineStyle {
    Solid = 0,   // ─────────
    Dashed,      // ─ ─ ─ ─ ─
    Dotted       // ···········
};

#endif // LINESTYLE_H
