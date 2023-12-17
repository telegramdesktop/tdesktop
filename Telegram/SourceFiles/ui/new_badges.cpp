/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/new_badges.h"

#include "lang/lang_keys.h"
#include "ui/painter.h"
#include "ui/widgets/labels.h"
#include "styles/style_window.h"
#include "styles/style_settings.h"

namespace Ui::NewBadge {
namespace {

[[nodiscard]] not_null<Ui::RpWidget*> CreateNewBadge(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<QString> text) {
	const auto badge = Ui::CreateChild<Ui::PaddingWrap<Ui::FlatLabel>>(
		parent.get(),
		object_ptr<Ui::FlatLabel>(
			parent,
			std::move(text),
			st::settingsPremiumNewBadge),
		st::settingsPremiumNewBadgePadding);
	badge->setAttribute(Qt::WA_TransparentForMouseEvents);
	badge->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(badge);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgActive);
		const auto r = st::settingsPremiumNewBadgePadding.left();
		p.drawRoundedRect(badge->rect(), r, r);
	}, badge->lifetime());
	return badge;
}

} // namespace

void AddToRight(not_null<Ui::RpWidget*> parent) {
	const auto badge = CreateNewBadge(parent, tr::lng_bot_side_menu_new());

	parent->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		badge->moveToRight(
			st::mainMenuButton.padding.right(),
			(size.height() - badge->height()) / 2,
			size.width());
	}, badge->lifetime());
}

void AddAfterLabel(
		not_null<Ui::RpWidget*> parent,
		not_null<Ui::RpWidget*> label) {
	const auto badge = CreateNewBadge(
		parent,
		tr::lng_premium_summary_new_badge());

	label->geometryValue(
	) | rpl::start_with_next([=](QRect geometry) {
		badge->move(st::settingsPremiumNewBadgePosition
			+ QPoint(label->x() + label->width(), label->y()));
	}, badge->lifetime());
}

} // namespace Ui::NewBadge
