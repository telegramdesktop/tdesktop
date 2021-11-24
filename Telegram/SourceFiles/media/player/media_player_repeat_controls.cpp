/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/player/media_player_repeat_controls.h"

#include "media/player/media_player_dropdown.h"
#include "media/player/media_player_instance.h"
#include "ui/widgets/buttons.h"
#include "core/core_settings.h"
#include "core/application.h"
#include "styles/style_media_player.h"

namespace Media::Player {

void PrepareRepeatDropdown(not_null<Dropdown*> dropdown) {
	const auto makeButton = [&] {
		const auto result = Ui::CreateChild<Ui::IconButton>(
			dropdown.get(),
			st::mediaPlayerRepeatButton);
		result->show();
		return result;
	};

	const auto repeatOne = makeButton();
	const auto repeatAll = makeButton();
	const auto shuffle = makeButton();
	const auto reverse = makeButton();

	Core::App().settings().playerRepeatModeValue(
	) | rpl::start_with_next([=](RepeatMode mode) {
		const auto one = (mode == RepeatMode::One);
		repeatOne->setIconOverride(one
			? &st::mediaPlayerRepeatOneIcon
			: &st::mediaPlayerRepeatOneDisabledIcon,
			one ? nullptr : &st::mediaPlayerRepeatOneDisabledIconOver);
		repeatOne->setRippleColorOverride(
			one ? nullptr : &st::mediaPlayerRepeatDisabledRippleBg);
		const auto all = (mode == RepeatMode::All);
		repeatAll->setIconOverride(all
			? nullptr
			: &st::mediaPlayerRepeatDisabledIcon,
			all ? nullptr : &st::mediaPlayerRepeatDisabledIconOver);
		repeatAll->setRippleColorOverride(
			all ? nullptr : &st::mediaPlayerRepeatDisabledRippleBg);
	}, dropdown->lifetime());

	Core::App().settings().playerOrderModeValue(
	) | rpl::start_with_next([=](OrderMode mode) {
		const auto shuffled = (mode == OrderMode::Shuffle);
		shuffle->setIconOverride(shuffled
			? &st::mediaPlayerShuffleIcon
			: &st::mediaPlayerShuffleIcon,
			shuffled ? nullptr : &st::mediaPlayerShuffleIcon);
		shuffle->setRippleColorOverride(
			shuffled ? nullptr : &st::mediaPlayerRepeatDisabledRippleBg);
		const auto reversed = (mode == OrderMode::Reverse);
		reverse->setIconOverride(reversed
			? &st::mediaPlayerReverseIcon
			: &st::mediaPlayerReverseDisabledIcon,
			reversed ? nullptr : &st::mediaPlayerReverseDisabledIconOver);
		reverse->setRippleColorOverride(
			reversed ? nullptr : &st::mediaPlayerRepeatDisabledRippleBg);
	}, dropdown->lifetime());

	const auto toggleRepeat = [](RepeatMode mode) {
		auto &settings = Core::App().settings();
		const auto active = (settings.playerRepeatMode() == mode);
		settings.setPlayerRepeatMode(active ? RepeatMode::None : mode);
		Core::App().saveSettingsDelayed();
	};
	const auto toggleOrder = [](OrderMode mode) {
		auto &settings = Core::App().settings();
		const auto active = (settings.playerOrderMode() == mode);
		settings.setPlayerOrderMode(active ? OrderMode::Default : mode);
		Core::App().saveSettingsDelayed();
	};
	repeatOne->setClickedCallback([=] { toggleRepeat(RepeatMode::One); });
	repeatAll->setClickedCallback([=] { toggleRepeat(RepeatMode::All); });
	shuffle->setClickedCallback([=] { toggleOrder(OrderMode::Shuffle); });
	reverse->setClickedCallback([=] { toggleOrder(OrderMode::Reverse); });

	dropdown->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		const auto rect = QRect(QPoint(), size);
		const auto inner = rect.marginsRemoved(dropdown->getMargin());
		const auto skip = (inner.height() - repeatOne->height() * 4) / 3;
		auto top = 0;
		const auto move = [&](auto &widget) {
			widget->move((size.width() - widget->width()) / 2, top);
			top += widget->height() + skip;
		};
		move(repeatOne);
		move(repeatAll);
		move(shuffle);
		move(reverse);
	}, dropdown->lifetime());
}

} // namespace Media::Player
