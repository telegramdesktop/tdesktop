/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_cover_item.h"

#include "boxes/peers/prepare_short_info_box.h"
#include "styles/style_calls.h"
#include "styles/style_info.h"

namespace Calls {

CoverItem::CoverItem(
	not_null<RpWidget*> parent,
	const style::Menu &stMenu,
	const style::ShortInfoCover &st,
	rpl::producer<QString> name,
	rpl::producer<QString> status,
	PreparedShortInfoUserpic userpic)
: Ui::Menu::ItemBase(parent, stMenu)
, _cover(
	this,
	st,
	std::move(name),
	std::move(status),
	std::move(userpic.value),
	[] { return false; })
, _dummyAction(new QAction(parent))
, _st(st) {
	setPointerCursor(false);

	initResizeHook(parent->sizeValue());
	enableMouseSelecting();
	enableMouseSelecting(_cover.widget());

	_cover.widget()->move(0, 0);
	_cover.moveRequests(
	) | rpl::start_with_next(userpic.move, lifetime());
}

not_null<QAction*> CoverItem::action() const {
	return _dummyAction;
}

bool CoverItem::isEnabled() const {
	return false;
}

int CoverItem::contentHeight() const {
	return _st.size + st::groupCallMenu.separator.padding.bottom();
}

AboutItem::AboutItem(
	not_null<RpWidget*> parent,
	const style::Menu &st,
	TextWithEntities &&about)
: Ui::Menu::ItemBase(parent, st)
, _st(st)
, _text(base::make_unique_q<Ui::FlatLabel>(
	this,
	rpl::single(std::move(about)),
	st::groupCallMenuAbout))
, _dummyAction(new QAction(parent)) {
	setPointerCursor(false);

	initResizeHook(parent->sizeValue());
	enableMouseSelecting();
	enableMouseSelecting(_text.get());

	_text->setSelectable(true);
	_text->resizeToWidth(st::groupCallMenuAbout.minWidth);
	_text->moveToLeft(st.itemPadding.left(), st.itemPadding.top());
}

not_null<QAction*> AboutItem::action() const {
	return _dummyAction;
}

bool AboutItem::isEnabled() const {
	return false;
}

int AboutItem::contentHeight() const {
	return _st.itemPadding.top()
		+ _text->height()
		+ _st.itemPadding.bottom();
}

} // namespace Calls
