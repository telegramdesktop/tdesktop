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
#include "info/userpic/info_userpic_emoji_builder_common.h"
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
		rpl::producer<std::vector<DocumentId>> documents,
		Fn<void(QImage &&image)> &&done) {
	struct State final {
		void next() {
			documentIndex = documentIndex.current() + 1;
			if (documentIndex.current() >= shuffledDocuments.size()) {
				documentIndex = 0;
			}
			colorIndex = base::RandomIndex(std::numeric_limits<int>::max());
		}
		rpl::variable<int> documentIndex;
		rpl::variable<int> colorIndex;
		std::vector<DocumentId> shuffledDocuments;

		base::Timer timer;
	};
	const auto state = menu->lifetime().make_state<State>();
	auto item = base::make_unique_q<Ui::Menu::Action>(
		menu.get(),
		menu->st().menu,
		Ui::Menu::CreateAction(
			menu.get(),
			tr::lng_attach_profile_emoji(tr::now),
			[=, done = std::move(done), docs = rpl::duplicate(documents)] {
				const auto index = state->documentIndex.current();
				const auto id = index < state->shuffledDocuments.size()
					? state->shuffledDocuments[index]
					: 0;
				UserpicBuilder::ShowLayer(
					controller,
					{ id, state->colorIndex.current(), docs },
					base::duplicate(done));
			}),
		nullptr,
		nullptr);
	rpl::duplicate(
		documents
	) | rpl::start_with_next([=](std::vector<DocumentId> documents) {
		state->shuffledDocuments = std::move(documents);
		auto rd = std::random_device();
		ranges::shuffle(state->shuffledDocuments, std::mt19937(rd()));
		state->documentIndex = 0;
	}, item->lifetime());
	state->next();
	state->timer.setCallback([=] { state->next(); });
	constexpr auto kTimeout = crl::time(1500);
	state->timer.callEach(kTimeout);
	const auto icon = UserpicBuilder::CreateEmojiUserpic(
		item.get(),
		st::restoreUserpicIcon.size,
		state->documentIndex.value(
		) | rpl::filter([=](int index) {
			return index < state->shuffledDocuments.size();
		}) | rpl::map([=](int index) {
			return controller->session().data().document(
				state->shuffledDocuments[index]);
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
