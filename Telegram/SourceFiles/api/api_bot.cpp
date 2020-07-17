/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_bot.h"

#include "apiwrap.h"
#include "api/api_send_progress.h"
#include "boxes/confirm_box.h"
#include "boxes/share_box.h"
#include "core/click_handler_types.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "main/main_session.h"
#include "ui/toast/toast.h"

namespace Api {

void SendBotCallbackData(
		not_null<HistoryItem*> item,
		int row,
		int column) {
	if (!IsServerMsgId(item->id)) {
		return;
	}
	const auto history = item->history();
	const auto session = &history->session();
	const auto owner = &history->owner();
	const auto api = &session->api();
	const auto bot = item->getMessageBot();
	const auto fullId = item->fullId();
	const auto getButton = [=] {
		return HistoryMessageMarkupButton::Get(
			owner,
			fullId,
			row,
			column);
	};
	const auto button = getButton();
	if (!button) {
		return;
	}

	using ButtonType = HistoryMessageMarkupButton::Type;
	const auto isGame = (button->type == ButtonType::Game);

	auto flags = MTPmessages_GetBotCallbackAnswer::Flags(0);
	QByteArray sendData;
	if (isGame) {
		flags |= MTPmessages_GetBotCallbackAnswer::Flag::f_game;
	} else if (button->type == ButtonType::Callback) {
		flags |= MTPmessages_GetBotCallbackAnswer::Flag::f_data;
		sendData = button->data;
	}
	button->requestId = api->request(MTPmessages_GetBotCallbackAnswer(
		MTP_flags(flags),
		history->peer->input,
		MTP_int(item->id),
		MTP_bytes(sendData)
	)).done([=](const MTPmessages_BotCallbackAnswer &result) {
		const auto item = owner->message(fullId);
		if (!item) {
			return;
		}
		if (const auto button = getButton()) {
			button->requestId = 0;
			owner->requestItemRepaint(item);
		}
		result.match([&](const MTPDmessages_botCallbackAnswer &data) {
			if (const auto message = data.vmessage()) {
				if (data.is_alert()) {
					Ui::show(Box<InformBox>(qs(*message)));
				} else {
					Ui::Toast::Show(qs(*message));
				}
			} else if (const auto url = data.vurl()) {
				const auto link = qs(*url);
				if (!isGame) {
					UrlClickHandler::Open(link);
					return;
				}
				const auto scoreLink = AppendShareGameScoreUrl(
					session,
					link,
					item->fullId());
				BotGameUrlClickHandler(bot, scoreLink).onClick({});
				session->sendProgressManager().update(
					history,
					Api::SendProgressType::PlayGame);
			}
		});
	}).fail([=](const RPCError &error) {
		const auto item = owner->message(fullId);
		if (!item) {
			return;
		}
		// Show error?
		if (const auto button = getButton()) {
			button->requestId = 0;
			owner->requestItemRepaint(item);
		}
	}).send();

	session->changes().messageUpdated(
		item,
		Data::MessageUpdate::Flag::BotCallbackSent
	);
}

} // namespace Api
