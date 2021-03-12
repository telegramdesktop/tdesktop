/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_bot.h"

#include "apiwrap.h"
#include "core/core_cloud_password.h"
#include "api/api_send_progress.h"
#include "boxes/confirm_box.h"
#include "boxes/share_box.h"
#include "boxes/passcode_box.h"
#include "lang/lang_keys.h"
#include "core/click_handler_types.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "main/main_session.h"
#include "ui/toast/toast.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"

namespace Api {
namespace {

void SendBotCallbackData(
		not_null<HistoryItem*> item,
		int row,
		int column,
		std::optional<MTPInputCheckPasswordSRP> password = std::nullopt,
		Fn<void(const MTP::Error &)> handleError = nullptr) {
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
	if (!button || button->requestId) {
		return;
	}

	using ButtonType = HistoryMessageMarkupButton::Type;
	const auto isGame = (button->type == ButtonType::Game);

	auto flags = MTPmessages_GetBotCallbackAnswer::Flags(0);
	QByteArray sendData;
	if (isGame) {
		flags |= MTPmessages_GetBotCallbackAnswer::Flag::f_game;
	} else if (button->type == ButtonType::Callback
		|| button->type == ButtonType::CallbackWithPassword) {
		flags |= MTPmessages_GetBotCallbackAnswer::Flag::f_data;
		sendData = button->data;
	}
	const auto withPassword = password.has_value();
	if (withPassword) {
		flags |= MTPmessages_GetBotCallbackAnswer::Flag::f_password;
	}
	button->requestId = api->request(MTPmessages_GetBotCallbackAnswer(
		MTP_flags(flags),
		history->peer->input,
		MTP_int(item->id),
		MTP_bytes(sendData),
		password.value_or(MTP_inputCheckPasswordEmpty())
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
					if (withPassword) {
						Ui::hideLayer();
					}
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
			} else if (withPassword) {
				Ui::hideLayer();
			}
		});
	}).fail([=](const MTP::Error &error) {
		const auto item = owner->message(fullId);
		if (!item) {
			return;
		}
		// Show error?
		if (const auto button = getButton()) {
			button->requestId = 0;
			owner->requestItemRepaint(item);
		}
		if (handleError) {
			handleError(error);
		}
	}).send();

	session->changes().messageUpdated(
		item,
		Data::MessageUpdate::Flag::BotCallbackSent
	);
}

} // namespace

void SendBotCallbackData(
		not_null<HistoryItem*> item,
		int row,
		int column) {
	SendBotCallbackData(item, row, column, MTP_inputCheckPasswordEmpty());
}

void SendBotCallbackDataWithPassword(
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
	if (!button || button->requestId) {
		return;
	}
	api->reloadPasswordState();
	SendBotCallbackData(item, row, column, MTP_inputCheckPasswordEmpty(), [=](const MTP::Error &error) {
		auto box = PrePasswordErrorBox(
			error,
			session,
			tr::lng_bots_password_confirm_check_about(
				tr::now,
				Ui::Text::WithEntities));
		if (box) {
			Ui::show(std::move(box));
		} else {
			auto lifetime = std::make_shared<rpl::lifetime>();
			button->requestId = -1;
			api->passwordState(
			) | rpl::take(
				1
			) | rpl::start_with_next([=](const Core::CloudPasswordState &state) mutable {
				if (lifetime) {
					base::take(lifetime)->destroy();
				}
				if (const auto button = getButton()) {
					if (button->requestId == -1) {
						button->requestId = 0;
					}
				} else {
					return;
				}
				const auto box = std::make_shared<QPointer<PasscodeBox>>();
				auto fields = PasscodeBox::CloudFields::From(state);
				fields.customTitle = tr::lng_bots_password_confirm_title();
				fields.customDescription
					= tr::lng_bots_password_confirm_description(tr::now);
				fields.customSubmitButton = tr::lng_passcode_submit();
				fields.customCheckCallback = [=](
						const Core::CloudPasswordResult &result) {
					if (const auto button = getButton()) {
						if (button->requestId) {
							return;
						}
					} else {
						return;
					}
					if (const auto item = owner->message(fullId)) {
						SendBotCallbackData(item, row, column, result.result, [=](const MTP::Error &error) {
							if (*box) {
								(*box)->handleCustomCheckError(error);
							}
						});
					}
				};
				*box = Ui::show(Box<PasscodeBox>(session, fields));
			}, *lifetime);
		}
	});
}

} // namespace Api
