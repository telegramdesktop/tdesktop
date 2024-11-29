/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/bot/starref/info_bot_starref_common.h"

#include "settings/settings_common.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/text/text_utilities.h"
//#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
//#include "styles/style_premium.h"

namespace Info::BotStarRef {

not_null<Ui::AbstractButton*> AddViewListButton(
		not_null<Ui::VerticalLayout*> parent,
		rpl::producer<QString> title,
		rpl::producer<QString> subtitle) {
	const auto &stLabel = st::defaultFlatLabel;
	const auto iconSize = st::settingsPremiumIconDouble.size();
	const auto &titlePadding = st::settingsPremiumRowTitlePadding;
	const auto &descriptionPadding = st::settingsPremiumRowAboutPadding;

	const auto button = Ui::CreateChild<Ui::SettingsButton>(
		parent,
		rpl::single(QString()));
	button->show();

	const auto label = parent->add(
		object_ptr<Ui::FlatLabel>(
			parent,
			std::move(title) | Ui::Text::ToBold(),
			stLabel),
		titlePadding);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto description = parent->add(
		object_ptr<Ui::FlatLabel>(
			parent,
			std::move(subtitle),
			st::boxDividerLabel),
		descriptionPadding);
	description->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto dummy = Ui::CreateChild<Ui::AbstractButton>(parent);
	dummy->setAttribute(Qt::WA_TransparentForMouseEvents);
	dummy->show();

	parent->sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		dummy->resize(s.width(), iconSize.height());
	}, dummy->lifetime());

	button->geometryValue(
	) | rpl::start_with_next([=](const QRect &r) {
		dummy->moveToLeft(0, r.y() + (r.height() - iconSize.height()) / 2);
	}, dummy->lifetime());

	::Settings::AddButtonIcon(dummy, st::settingsButton, {
		.icon = &st::settingsStarRefEarnStars,
		.backgroundBrush = st::premiumIconBg3,
	});

	rpl::combine(
		parent->widthValue(),
		label->heightValue(),
		description->heightValue()
	) | rpl::start_with_next([=,
		topPadding = titlePadding,
		bottomPadding = descriptionPadding](
			int width,
			int topHeight,
			int bottomHeight) {
		button->resize(
			width,
			topPadding.top()
			+ topHeight
			+ topPadding.bottom()
			+ bottomPadding.top()
			+ bottomHeight
			+ bottomPadding.bottom());
	}, button->lifetime());
	label->topValue(
	) | rpl::start_with_next([=, padding = titlePadding.top()](int top) {
		button->moveToLeft(0, top - padding);
	}, button->lifetime());
	const auto arrow = Ui::CreateChild<Ui::IconButton>(
		button,
		st::backButton);
	arrow->setIconOverride(
		&st::settingsPremiumArrow,
		&st::settingsPremiumArrowOver);
	arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
	button->sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		const auto &point = st::settingsPremiumArrowShift;
		arrow->moveToRight(
			-point.x(),
			point.y() + (s.height() - arrow->height()) / 2);
	}, arrow->lifetime());

	return button;
}

QString FormatStarRefCommission(ushort commission) {
	return QString::number(commission / 10.) + '%';
}

} // namespace Info::BotStarRef