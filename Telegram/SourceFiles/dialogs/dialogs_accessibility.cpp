#include "dialogs/dialogs_accessibility.h"
#include "dialogs/dialogs_inner_widget.h"
#include <QWindow>

namespace Dialogs {

AccessibleRow::AccessibleRow(InnerWidget *parent, int index) : _parent(parent), _index(index) {}

QObject *AccessibleRow::object() const {
	return nullptr;
}

bool AccessibleRow::isValid() const {
    if (_parent.isNull()) return false;
    
    if (_index < 0) return false;

    return _index < _parent->getAccessibleChildCount();
}

QWindow *AccessibleRow::window() const {
	if (_parent.isNull() || !_parent->window()) return nullptr;
    return _parent->window()->windowHandle();
}

QAccessibleInterface *AccessibleRow::parent() const {
    if (_parent.isNull()) {
        return nullptr;
    }

    return QAccessible::queryAccessibleInterface(_parent.data());
}

QAccessibleInterface *AccessibleRow::child(int index) const {
	return nullptr;
}

QAccessibleInterface *AccessibleRow::childAt(int x, int y) const {
	return nullptr;
}

int AccessibleRow::childCount() const {
	return 0;
}

int AccessibleRow::indexOfChild(const QAccessibleInterface *child) const {
	return -1;
}

QString AccessibleRow::text(QAccessible::Text t) const {
	if (!_parent) return QString();
	
	if (t == QAccessible::Name) {
		return _parent->getAccessibleName(_index);
	} else if (t == QAccessible::Description || t == QAccessible::Value) {
		return _parent->getAccessibleDescription(_index);
	}
	return QString();
}

void AccessibleRow::setText(QAccessible::Text t, const QString &text) {
}

QRect AccessibleRow::rect() const {
	if (!_parent) return QRect();
	
	// Use the helper on InnerWidget to calculate rect based on index
	// This keeps the logic centralized.
	QRect local = _parent->getAccessibleRect(_index);
	
	// If the item is logically selected but physically scrolled out of view,
	// we must return a valid on-screen rect (e.g. at the edge) or the 
	// screen reader will refuse to focus it.
	if (local.isEmpty()) {
		// Fallback: Use parent's top-left so it's at least "somewhere"
		return QRect(_parent->mapToGlobal(QPoint(0,0)), QSize(1,1));
	}
	
	QPoint globalTopLeft = _parent->mapToGlobal(local.topLeft());
	return QRect(globalTopLeft, local.size());
}

QAccessible::Role AccessibleRow::role() const {
	return QAccessible::ListItem;
}

QAccessible::State AccessibleRow::state() const {
	QAccessible::State s;
	s.selectable = true;
	s.focusable = true;
	
	if (_parent && _parent->isAccessibleRowSelected(_index)) {
		s.selected = true;
		s.focused = true;
	}
	return s;
}
// AccessibleInnerWidget Implementation
AccessibleInnerWidget::AccessibleInnerWidget(InnerWidget *widget) 
    : QAccessibleWidget(widget, QAccessible::List), _inner(widget) {}

QAccessibleInterface *AccessibleInnerWidget::child(int index) const {
	if (!_inner || index < 0 || index >= childCount()) {
		return nullptr;
	}

	return new AccessibleRow(_inner, index);
}
int AccessibleInnerWidget::childCount() const {
	return _inner ? _inner->getAccessibleChildCount() : 0;
}

int AccessibleInnerWidget::indexOfChild(const QAccessibleInterface *child) const {
	if (!child || !_inner) return -1;

	if (child->role() == QAccessible::ListItem) {
		const auto row = static_cast<const AccessibleRow*>(child);
		return row->index();
	}
	return -1;
}

QAccessibleInterface *AccessibleInnerWidget::childAt(int x, int y) const {
	if (!_inner) return nullptr;
	QPoint local = _inner->mapFromGlobal(QPoint(x, y));
	int index = _inner->getAccessibleIndexAt(local.y());
	if (index >= 0) {
		return child(index);
	}
	return nullptr;
}

QAccessibleInterface *AccessibleInnerWidget::focusChild() const {
	if (!_inner) return nullptr;
	if (_inner->hasFocus()) {
		int index = _inner->currentAccessibleIndex();
		if (index >= 0) {
			return child(index);
		}
	}
	return nullptr;
}

QAccessible::Role AccessibleInnerWidget::role() const {
	return QAccessible::List;
}

QAccessible::State AccessibleInnerWidget::state() const {
	auto s = QAccessibleWidget::state();
	s.focusable = false; 
	s.multiSelectable = false;
	return s;
}

} // namespace Dialogs