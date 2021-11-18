/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/player/media_player_repeat_controls.h"

#include "media/player/media_player_dropdown.h"
#include "ui/widgets/buttons.h"
#include "styles/style_media_player.h"

namespace Media::Player {

void PrepareRepeatDropdown(
		not_null<Dropdown*> dropdown,
		not_null<Window::SessionController*> controller) {
	const auto makeButton = [&] {
		const auto result = Ui::CreateChild<Ui::IconButton>(
			dropdown.get(),
			st::mediaPlayerRepeatButton);
		result->show();
		return result;
	};

	const auto repeatOne = makeButton();
	const auto repeat = makeButton();
	const auto shuffle = makeButton();
	const auto reverse = makeButton();

	repeatOne->setIconOverride(&st::mediaPlayerRepeatOneIcon);
	shuffle->setIconOverride(&st::mediaPlayerShuffleIcon);
	reverse->setIconOverride(&st::mediaPlayerReverseIcon);

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
		move(repeat);
		move(shuffle);
		move(reverse);
	}, dropdown->lifetime());
}

} // namespace Media::Player
