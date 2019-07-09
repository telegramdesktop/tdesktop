/*
This file is part of Bettergram.

For license and copyright information please follow this link:
https://github.com/bettergram/bettergram/blob/master/LEGAL
*/

#include "dialogs_chat_tab_button.h"
#include "dialogs_layout.h"
#include "ui/widgets/popup_menu.h"
#include "app.h"
#include "mainwidget.h"
#include "lang/lang_keys.h"
#include "styles/style_dialogs.h"

namespace Dialogs {

ChatTabButton::ChatTabButton(EntryTypes type, QWidget *parent, const style::IconButton &st) :
	Ui::IconButton(parent, st),
	_type(type) {
}

EntryTypes ChatTabButton::type() {
	return _type;
}

bool ChatTabButton::selected() const {
	return _selected;
}

void ChatTabButton::setSelected(bool selected) {
	if (_selected != selected) {
		_selected = selected;

		if (_selected) {
			setIconOverride(&style().iconOver);
		} else {
			setIconOverride(nullptr, &style().icon);
		}
	}
}

void ChatTabButton::select() {
	setSelected(true);
}

void ChatTabButton::unselect() {
	setSelected(false);
}

int ChatTabButton::unreadCount() const {
	return _unreadCount;
}

void ChatTabButton::setUnreadCount(int unreadCount) {
	if (_unreadCount != unreadCount) {
		_unreadCount = unreadCount;

		update();
	}
}

void ChatTabButton::enterEventHook(QEvent *e) {
	if (!_selected) {
		setIconOverride(nullptr, nullptr);
	}
	Ui::IconButton::enterEventHook(e);
}

void ChatTabButton::leaveEventHook(QEvent *e) {
	if (!_selected) {
		setIconOverride(nullptr, nullptr);
	}
	Ui::IconButton::enterEventHook(e);
}

void ChatTabButton::paintEvent(QPaintEvent *event) {
	Ui::IconButton::paintEvent(event);

	if (_unreadCount <= 0) {
		return;
	}

	Painter painter(this);

	QString counter = QString::number(_unreadCount);
	if (counter.size() > 4) {
		counter = qsl("..") + counter.mid(counter.size() - 3);
	}

	::Dialogs::Layout::UnreadBadgeStyle st;
	st.active = false;
	st.muted = false;

	// place the badge at horizontal center
	int unreadRight = (width() + std::max(st.size, st.font->width(counter) + 2 * st.padding)) / 2;

	// keep the bottom padding for the badge as the top padding for the button icon
	int unreadTop = height() - st.size - style().iconPosition.y();
	int unreadWidth = 0;

	::Dialogs::Layout::paintUnreadCount(painter, counter, unreadRight, unreadTop, st, &unreadWidth);
}

void ChatTabButton::contextMenuEvent(QContextMenuEvent *e) {
	_menu = nullptr;
	return; // Sean didn't know why Bettergram have implemented empty menu
	_menu = base::make_unique_q<Ui::PopupMenu>(nullptr);

	connect(_menu.get(), &QObject::destroyed, [this] {
		leaveEventHook(nullptr);
	});

	_menu->popup(e->globalPos());
	e->accept();
}

} // namespace Dialogs
