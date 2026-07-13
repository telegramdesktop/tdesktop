/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/suggestions/suggestion.h"

#include "boxes/star_gift_auction_box.h"
#include "core/ui_integration.h"
#include "data/components/gift_auctions.h"
#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h"
#include "window/window_session_controller.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"

namespace Dialogs::TopBarSuggestions {
namespace {

bool Available(const Context &context) {
	return context.session->giftAuctions().hasActive();
}

void Activate(ActivateArgs args) {
	const auto session = args.context.session.get();
	const auto auctions = &session->giftAuctions();
	const auto content = args.context.ensureContent();
	const auto window = args.context.findController();

	struct Button {
		rpl::variable<TextWithEntities> text;
		Fn<void()> callback;
		base::has_weak_ptr guard;
	};
	const auto button = args.lifetime->make_state<Button>();
	const auto recompute = args.recompute;
	auctions->active(
	) | rpl::on_next([=](Data::ActiveAuctions &&active) {
		if (active.list.empty()) {
			recompute();
			return;
		}
		auto text = Ui::ActiveAuctionsState(active);
		const auto textColorOverride = text.someOutbid
			? st::attentionButtonFg->c
			: std::optional<QColor>();
		content->setContent(
			Ui::ActiveAuctionsTitle(active),
			std::move(text.text),
			Core::TextContext({ .session = session }),
			textColorOverride);
		button->text = Ui::ActiveAuctionsButton(active);
		button->callback = Ui::ActiveAuctionsCallback(window, active);
	}, *args.lifetime);
	const auto callback = crl::guard(&button->guard, [=] {
		button->callback();
	});
	content->setRightButton(button->text.value(), callback);
	content->setClickedCallback(callback);
	content->setLeadingWidget(nullptr);
	args.done(content, [content] { content->prepareCollapseSnapshot(); });
}

} // namespace

Spec MakeGiftAuctionsSpec() {
	return {
		.priority = Priority::GiftAuctions,
		.available = Available,
		.activate = Activate,
	};
}

} // namespace Dialogs::TopBarSuggestions
