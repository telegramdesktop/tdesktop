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
#include "styles/style_info.h"
#include "styles/style_window.h"
#include "styles/style_settings.h"

namespace Ui {

Text::PaletteDependentEmoji AttentionMarkEmoji() {
	return {
		.factory = [] {
			const auto s = st::infoSecurityRiskIconSize;
			const auto ratio = style::DevicePixelRatio();
			const auto rect = QRect(0, 0, s, s);
			auto result = QImage(
				rect.size() * ratio,
				QImage::Format_ARGB32_Premultiplied);
			result.setDevicePixelRatio(ratio);
			result.fill(Qt::transparent);

			auto p = QPainter(&result);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::attentionButtonFg);
			p.drawEllipse(rect);

			p.setPen(st::windowFgActive);
			p.setFont(st::semiboldFont);
			p.drawText(rect, u"!"_q, style::al_center);

			p.end();
			return result;
		},
		.margin = st::infoSecurityRiskIconMargin,
	};
}

} // namespace Ui

namespace Ui::NewBadge {

not_null<Ui::RpWidget*> CreateNewBadge(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<QString> text) {
	const auto badge = Ui::CreateChild<Ui::PaddingWrap<Ui::FlatLabel>>(
		parent.get(),
		object_ptr<Ui::FlatLabel>(
			parent,
			std::move(text),
			st::settingsPremiumNewBadge),
		st::settingsPremiumNewBadgePadding);
	badge->show();
	badge->setAttribute(Qt::WA_TransparentForMouseEvents);
	badge->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(badge);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgActive);
		const auto r = st::settingsPremiumNewBadgePadding.left();
		p.drawRoundedRect(badge->rect(), r, r);
	}, badge->lifetime());
	return badge;
}

void AddToRight(not_null<Ui::RpWidget*> parent) {
	const auto badge = CreateNewBadge(parent, tr::lng_bot_side_menu_new());

	parent->sizeValue(
	) | rpl::on_next([=](QSize size) {
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
	) | rpl::on_next([=](QRect geometry) {
		badge->move(st::settingsPremiumNewBadgePosition
			+ QPoint(label->x() + label->width(), label->y()));
	}, badge->lifetime());
}

void AddAfterButtonText(
		not_null<Ui::RpWidget*> button,
		rpl::producer<QString> text,
		const style::SettingsButton &st) {
	const auto badge = CreateNewBadge(
		button,
		tr::lng_premium_summary_new_badge());
	rpl::combine(
		std::move(text),
		button->widthValue()
	) | rpl::on_next([=, &st](const QString &text, int width) {
		const auto space = st.style.font->spacew;
		const auto left = st.padding.left()
			+ st.style.font->width(text)
			+ space;
		const auto available = width - left - st.padding.right();
		badge->setVisible(available >= badge->width());
		if (!badge->isHidden()) {
			const auto top = st.padding.top()
				+ st.style.font->ascent
				- st::settingsPremiumNewBadge.style.font->ascent
				- st::settingsPremiumNewBadgePadding.top();
			badge->moveToLeft(left, top, width);
		}
	}, badge->lifetime());
}

} // namespace Ui::NewBadge
