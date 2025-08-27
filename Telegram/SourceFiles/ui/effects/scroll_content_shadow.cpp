/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/effects/scroll_content_shadow.h"

#include "ui/rp_widget.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/ui_utility.h"

namespace Ui {

void SetupShadowsToScrollContent(
		not_null<Ui::RpWidget*> parent,
		not_null<Ui::ScrollArea*> scroll,
		rpl::producer<int> &&innerHeightValue) {
	using namespace rpl::mappers;

	const auto topShadow = Ui::CreateChild<Ui::FadeShadow>(parent.get());
	const auto bottomShadow = Ui::CreateChild<Ui::FadeShadow>(parent.get());
	scroll->geometryValue(
	) | rpl::start_with_next_done([=](const QRect &geometry) {
		topShadow->resizeToWidth(geometry.width());
		topShadow->move(
			geometry.x(),
			geometry.y());
		bottomShadow->resizeToWidth(geometry.width());
		bottomShadow->move(
			geometry.x(),
			geometry.y() + geometry.height() - st::lineWidth);
	}, [t = base::make_weak(topShadow), b = base::make_weak(bottomShadow)] {
		Ui::DestroyChild(t.get());
		Ui::DestroyChild(b.get());
	}, topShadow->lifetime());

	topShadow->toggleOn(scroll->scrollTopValue() | rpl::map(_1 > 0));
	bottomShadow->toggleOn(rpl::combine(
		scroll->scrollTopValue(),
		scroll->heightValue(),
		std::move(innerHeightValue),
		_1 + _2 < _3));
}

} // namespace Ui
