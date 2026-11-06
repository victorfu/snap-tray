#include "qml/AccessibilityScope.h"

#include <QAccessible>
#include <QtQuick/private/qaccessiblequickitem_p.h>

namespace SnapTray {
namespace {

// Keep Qt's geometry, parent, focus and ordinary child semantics. Only the
// modal boundary changes; Accessible.ignored alone promotes its children.
class ScopeInterface final : public QAccessibleQuickItem
{
public:
    explicit ScopeInterface(AccessibilityScope* scope) : QAccessibleQuickItem(scope), m_scope(scope) {}
    int childCount() const override { return blocked() ? 0 : QAccessibleQuickItem::childCount(); }
    QAccessibleInterface* child(int index) const override
    {
        return blocked() ? nullptr : QAccessibleQuickItem::child(index);
    }
    int indexOfChild(const QAccessibleInterface* child) const override
    {
        return blocked() ? -1 : QAccessibleQuickItem::indexOfChild(child);
    }
    QAccessibleInterface* childAt(int x, int y) const override
    {
        return blocked() ? nullptr : QAccessibleQuickItem::childAt(x, y);
    }
    QAccessibleInterface* focusChild() const override
    {
        return blocked() ? nullptr : QAccessibleQuickItem::focusChild();
    }
    QAccessible::State state() const override
    {
        auto result = QAccessibleQuickItem::state();
        if (blocked()) result.invisible = true;
        return result;
    }
private:
    bool blocked() const { return m_scope->accessibilityBlocked(); }
    AccessibilityScope* m_scope;
};

QAccessibleInterface* scopeFactory(const QString&, QObject* object)
{
    if (auto* scope = qobject_cast<AccessibilityScope*>(object))
        return new ScopeInterface(scope);
    return nullptr;
}

} // namespace

AccessibilityScope::AccessibilityScope(QQuickItem* parent) : QQuickItem(parent)
{
    static const bool installed = [] { QAccessible::installFactory(scopeFactory); return true; }();
    Q_UNUSED(installed);
}

void AccessibilityScope::setAccessibilityBlocked(bool blocked)
{
    if (m_blocked == blocked) return;
    m_blocked = blocked;
    emit accessibilityBlockedChanged();
    QAccessibleEvent visibility(this, blocked ? QAccessible::ObjectHide : QAccessible::ObjectShow);
    QAccessible::updateAccessibility(&visibility);
    if (auto* iface = QAccessible::queryAccessibleInterface(this)) {
        if (auto* parent = iface->parent()) {
            QAccessibleEvent reordered(parent, QAccessible::ObjectReorder);
            QAccessible::updateAccessibility(&reordered);
        }
    }
}

} // namespace SnapTray
