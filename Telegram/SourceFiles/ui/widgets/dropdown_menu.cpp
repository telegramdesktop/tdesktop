/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/dropdown_menu.h"

#include "application.h"
#include "lang/lang_keys.h"

namespace Ui {

DropdownMenu::DropdownMenu(QWidget *parent, const style::DropdownMenu &st) : InnerDropdown(parent, st.wrap)
, _st(st) {
	_menu = setOwnedWidget(object_ptr<Ui::Menu>(this, _st.menu));
	init();
}

// Not ready with submenus yet.
//DropdownMenu::DropdownMenu(QWidget *parent, QMenu *menu, const style::DropdownMenu &st) : InnerDropdown(parent, st.wrap)
//, _st(st) {
//	_menu = setOwnedWidget(object_ptr<Ui::Menu>(this, menu, _st.menu));
//	init();
//
//	for (auto action : actions()) {
//		if (auto submenu = action->menu()) {
//			auto it = _submenus.insert(action, new DropdownMenu(submenu, st));
//			it.value()->deleteOnHide(false);
//		}
//	}
//}

void DropdownMenu::init() {
	InnerDropdown::setHiddenCallback([this] { hideFinish(); });

	_menu->setResizedCallback([this] { resizeToContent(); });
	_menu->setActivatedCallback([this](QAction *action, int actionTop, TriggeredSource source) {
		handleActivated(action, actionTop, source);
	});
	_menu->setTriggeredCallback([this](QAction *action, int actionTop, TriggeredSource source) {
		handleTriggered(action, actionTop, source);
	});
	_menu->setKeyPressDelegate([this](int key) { return handleKeyPress(key); });
	_menu->setMouseMoveDelegate([this](QPoint globalPosition) { handleMouseMove(globalPosition); });
	_menu->setMousePressDelegate([this](QPoint globalPosition) { handleMousePress(globalPosition); });
	_menu->setMouseReleaseDelegate([this](QPoint globalPosition) { handleMouseRelease(globalPosition); });

	setMouseTracking(true);

	hide();
}

QAction *DropdownMenu::addAction(const QString &text, const QObject *receiver, const char* member, const style::icon *icon, const style::icon *iconOver) {
	return _menu->addAction(text, receiver, member, icon, iconOver);
}

QAction *DropdownMenu::addAction(const QString &text, base::lambda<void()> callback, const style::icon *icon, const style::icon *iconOver) {
	return _menu->addAction(text, std::move(callback), icon, iconOver);
}

QAction *DropdownMenu::addSeparator() {
	return _menu->addSeparator();
}

void DropdownMenu::clearActions() {
	//for (auto submenu : base::take(_submenus)) {
	//	delete submenu;
	//}
	return _menu->clearActions();
}

DropdownMenu::Actions &DropdownMenu::actions() {
	return _menu->actions();
}

void DropdownMenu::handleActivated(QAction *action, int actionTop, TriggeredSource source) {
	if (source == TriggeredSource::Mouse) {
		if (!popupSubmenuFromAction(action, actionTop, source)) {
			if (auto currentSubmenu = base::take(_activeSubmenu)) {
				currentSubmenu->hideMenu(true);
			}
		}
	}
}

void DropdownMenu::handleTriggered(QAction *action, int actionTop, TriggeredSource source) {
	if (!popupSubmenuFromAction(action, actionTop, source)) {
		hideMenu();
		_triggering = true;
		emit action->trigger();
		_triggering = false;
		if (_deleteLater) {
			_deleteLater = false;
			deleteLater();
		}
	}
}

// Not ready with submenus yet.
bool DropdownMenu::popupSubmenuFromAction(QAction *action, int actionTop, TriggeredSource source) {
	//if (auto submenu = _submenus.value(action)) {
	//	if (_activeSubmenu == submenu) {
	//		submenu->hideMenu(true);
	//	} else {
	//		popupSubmenu(submenu, actionTop, source);
	//	}
	//	return true;
	//}
	return false;
}

//void DropdownMenu::popupSubmenu(SubmenuPointer submenu, int actionTop, TriggeredSource source) {
//	if (auto currentSubmenu = base::take(_activeSubmenu)) {
//		currentSubmenu->hideMenu(true);
//	}
//	if (submenu) {
//		auto menuTopLeft = mapFromGlobal(_menu->mapToGlobal(QPoint(0, 0)));
//		auto menuBottomRight = mapFromGlobal(_menu->mapToGlobal(QPoint(_menu->width(), _menu->height())));
//		QPoint p(menuTopLeft.x() + (rtl() ? (width() - menuBottomRight.x()) : menuBottomRight.x()), menuTopLeft.y() + actionTop);
//		_activeSubmenu = submenu;
//		_activeSubmenu->showMenu(geometry().topLeft() + p, this, source);
//
//		_menu->setChildShown(true);
//	} else {
//		_menu->setChildShown(false);
//	}
//}

void DropdownMenu::forwardKeyPress(int key) {
	if (!handleKeyPress(key)) {
		_menu->handleKeyPress(key);
	}
}

bool DropdownMenu::handleKeyPress(int key) {
	if (_activeSubmenu) {
		_activeSubmenu->handleKeyPress(key);
		return true;
	} else if (key == Qt::Key_Escape) {
		hideMenu(_parent ? true : false);
		return true;
	} else if (key == (rtl() ? Qt::Key_Right : Qt::Key_Left)) {
		if (_parent) {
			hideMenu(true);
			return true;
		}
	}
	return false;
}

void DropdownMenu::handleMouseMove(QPoint globalPosition) {
	if (_parent) {
		_parent->forwardMouseMove(globalPosition);
	}
}

void DropdownMenu::handleMousePress(QPoint globalPosition) {
	if (_parent) {
		_parent->forwardMousePress(globalPosition);
	} else {
		hideMenu();
	}
}

void DropdownMenu::handleMouseRelease(QPoint globalPosition) {
	if (_parent) {
		_parent->forwardMouseRelease(globalPosition);
	} else {
		hideMenu();
	}
}

void DropdownMenu::focusOutEvent(QFocusEvent *e) {
	hideMenu();
}

void DropdownMenu::hideEvent(QHideEvent *e) {
	if (_deleteOnHide) {
		if (_triggering) {
			_deleteLater = true;
		} else {
			deleteLater();
		}
	}
}

void DropdownMenu::hideMenu(bool fast) {
	if (isHidden()) return;
	if (_parent && !isHiding()) {
		_parent->childHiding(this);
	}
	if (fast) {
		hideFast();
	} else {
		hideAnimated();
		if (_parent) {
			_parent->hideMenu();
		}
	}
	if (_activeSubmenu) {
		_activeSubmenu->hideMenu(fast);
	}
}

void DropdownMenu::childHiding(DropdownMenu *child) {
	if (_activeSubmenu && _activeSubmenu == child) {
		_activeSubmenu = SubmenuPointer();
	}
}

void DropdownMenu::hideFinish() {
	_menu->clearSelection();
	if (_hiddenCallback) {
		_hiddenCallback();
	}
}

// Not ready with submenus yet.
//void DropdownMenu::deleteOnHide(bool del) {
//	_deleteOnHide = del;
//}

//void DropdownMenu::popup(const QPoint &p) {
//	showMenu(p, nullptr, TriggeredSource::Mouse);
//}
//
//void DropdownMenu::showMenu(const QPoint &p, DropdownMenu *parent, TriggeredSource source) {
//	_parent = parent;
//
//	auto menuTopLeft = mapFromGlobal(_menu->mapToGlobal(QPoint(0, 0)));
//	auto w = p - QPoint(0, menuTopLeft.y());
//	auto r = Sandbox::screenGeometry(p);
//	if (rtl()) {
//		if (w.x() - width() < r.x() - _padding.left()) {
//			if (_parent && w.x() + _parent->width() - _padding.left() - _padding.right() + width() - _padding.right() <= r.x() + r.width()) {
//				w.setX(w.x() + _parent->width() - _padding.left() - _padding.right());
//			} else {
//				w.setX(r.x() - _padding.left());
//			}
//		} else {
//			w.setX(w.x() - width());
//		}
//	} else {
//		if (w.x() + width() - _padding.right() > r.x() + r.width()) {
//			if (_parent && w.x() - _parent->width() + _padding.left() + _padding.right() - width() + _padding.right() >= r.x() - _padding.left()) {
//				w.setX(w.x() + _padding.left() + _padding.right() - _parent->width() - width() + _padding.left() + _padding.right());
//			} else {
//				w.setX(r.x() + r.width() - width() + _padding.right());
//			}
//		}
//	}
//	if (w.y() + height() - _padding.bottom() > r.y() + r.height()) {
//		if (_parent) {
//			w.setY(r.y() + r.height() - height() + _padding.bottom());
//		} else {
//			w.setY(p.y() - height() + _padding.bottom());
//		}
//	}
//	if (w.y() < r.y()) {
//		w.setY(r.y());
//	}
//	move(w);
//
//	_menu->setShowSource(source);
//}

DropdownMenu::~DropdownMenu() {
	clearActions();
}

} // namespace Ui
