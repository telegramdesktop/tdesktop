/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/share_message_phrase_factory.h"

#include "chat_helpers/compose/compose_show.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/text/text_utilities.h"
#include "window/window_session_controller.h"

namespace ChatHelpers {

rpl::producer<TextWithEntities> ForwardedMessagePhrase(
		const ForwardedMessagePhraseArgs &args) {
	if (args.toCount <= 1) {
		Assert(args.to1);

		if (args.to1->isSelf()) {
			if (args.toSelfWithPremiumIsEmpty && args.to1->isPremium()) {
				return {};
			}
			return (args.singleMessage
				? tr::lng_share_message_to_saved
				: tr::lng_share_messages_to_saved)(
					lt_chat,
					rpl::single(Ui::Text::Link(
						tr::lng_saved_messages(tr::now))),
					tr::rich);
		} else {
			return (args.singleMessage
				? tr::lng_share_message_to_chat
				: tr::lng_share_messages_to_chat)(
					lt_chat,
					rpl::single(TextWithEntities{ args.to1->name() }),
					tr::rich);
		}
	} else if ((args.toCount == 2) && (args.to1 && args.to2)) {
		return (args.singleMessage
			? tr::lng_share_message_to_two_chats
			: tr::lng_share_messages_to_two_chats)(
				lt_user,
				rpl::single(TextWithEntities{ args.to1->name() }),
				lt_chat,
				rpl::single(TextWithEntities{ args.to2->name() }),
				tr::rich);
	} else {
		return (args.singleMessage
			? tr::lng_share_message_to_many_chats
			: tr::lng_share_messages_to_many_chats)(
				lt_count,
				rpl::single(args.toCount) | tr::to_count(),
				tr::rich);
	}
}

Ui::Toast::ClickHandlerFilter ForwardedToSavedMessagesFilter(
		not_null<Main::Session*> session) {
	return [=](const ClickHandlerPtr &, Qt::MouseButton) {
		if (const auto window = ResolveWindowDefault()(session)) {
			window->showPeerHistory(window->session().user());
		}
		return false;
	};
}

} // namespace ChatHelpers
