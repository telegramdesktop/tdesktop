/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_collectibles_viewport.h"

#include "ui/wrap/vertical_layout.h"

namespace Wallet {

CollectiblesViewport::CollectiblesViewport(
		QWidget *parent,
		not_null<Ui::RpWidget*> above,
		not_null<Ui::SlideWrap<>*> skip)
: RpWidget(parent)
, _above(above)
, _skip(skip)
, _page(Ui::CreateChild<Ui::VerticalLayout>(this)) {
	_page->show();
	rpl::combine(
		_above->heightValue(),
		_skip->heightValue()
	) | rpl::on_next([=] {
		placePage();
	}, lifetime());
	_page->heightValue(
	) | rpl::on_next([=](int height) {
		resize(width(), height);
	}, lifetime());
}

not_null<Ui::VerticalLayout*> CollectiblesViewport::page() const {
	return _page;
}

int CollectiblesViewport::resizeGetHeight(int newWidth) {
	_page->resizeToWidth(newWidth);
	placePage();
	return _page->height();
}

void CollectiblesViewport::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	placePage();
	const auto top = std::max(visibleTop, _page->y());
	const auto bottom = std::min(
		visibleBottom,
		_page->y() + _page->height());
	if (top < bottom) {
		setChildVisibleTopBottom(_page, top, bottom);
	} else {
		_page->setVisibleTopBottom(0, 0); // Keep hidden preloading at the top.
	}
}

void CollectiblesViewport::placePage() {
	const auto top = _skip->wrapped()->height()
		- _skip->height()
		- _above->height();
	if (_page->y() != top) {
		_page->move(0, top);
	}
}

} // namespace Wallet
