/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/userpic/info_userpic_emoji_builder_menu_item.h"

#include "base/random.h"
#include "base/timer.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "info/userpic/info_userpic_emoji_builder.h"
#include "info/userpic/info_userpic_emoji_builder_widget.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/menu/menu_common.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_menu_icons.h"

#include <random>

namespace UserpicBuilder {

void AddEmojiBuilderAction(
		not_null<Window::SessionController*> controller,
		not_null<Ui::PopupMenu*> menu,
		std::vector<DocumentId> documents,
		Fn<void(QImage &&image)> &&done) {
	{
		auto rd = std::random_device();
		ranges::shuffle(documents, std::mt19937(rd()));
	}
	struct State final {
		rpl::variable<int> documentIndex;
		rpl::variable<int> colorIndex;

		base::Timer timer;
	};
	const auto state = menu->lifetime().make_state<State>();
	auto item = base::make_unique_q<Ui::Menu::Action>(
		menu.get(),
		menu->st().menu,
		Ui::Menu::CreateAction(
			menu.get(),
			tr::lng_attach_profile_emoji(tr::now),
			[=, done = std::move(done)] {
				const auto index = state->documentIndex.current();
				const auto id = index < documents.size()
					? documents[index]
					: 0;
				UserpicBuilder::ShowLayer(
					controller,
					{ id, state->colorIndex.current() },
					base::duplicate(done));
			}),
		nullptr,
		nullptr);
	const auto timerCallback = [=] {
		state->documentIndex = state->documentIndex.current() + 1;
		if (state->documentIndex.current() >= documents.size()) {
			state->documentIndex = 0;
		}
		state->colorIndex = base::RandomIndex(
			std::numeric_limits<int>::max());
	};
	timerCallback();
	state->timer.setCallback(timerCallback);
	constexpr auto kTimeout = crl::time(1500);
	state->timer.callEach(kTimeout);
	const auto icon = UserpicBuilder::CreateEmojiUserpic(
		item.get(),
		st::restoreUserpicIcon.size,
		state->documentIndex.value(
		) | rpl::filter([=](int index) {
			return index < documents.size();
		}) | rpl::map([=](int index) {
			return controller->session().data().document(documents[index]);
		}),
		state->colorIndex.value());
	icon->setAttribute(Qt::WA_TransparentForMouseEvents);
	icon->move(menu->st().menu.itemIconPosition
		+ QPoint(
			(st::menuIconRemove.width() - icon->width()) / 2,
			(st::menuIconRemove.height() - icon->height()) / 2));

	menu->addAction(std::move(item));
}

} // namespace UserpicBuilder
