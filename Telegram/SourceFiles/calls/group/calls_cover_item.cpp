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
namespace {

} // namespace

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
	return _st.size + st::groupCallMenu.separatorPadding.bottom();
}

} // namespace Calls
