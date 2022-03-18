/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_bot.h"

#include "apiwrap.h"
#include "api/api_cloud_password.h"
#include "core/core_cloud_password.h"
#include "api/api_send_progress.h"
#include "ui/boxes/confirm_box.h"
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
		std::optional<Core::CloudPasswordResult> password = std::nullopt,
		Fn<void(const QString &)> handleError = nullptr) {
	if (!item->isRegular()) {
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
		password ? password->result : MTP_inputCheckPasswordEmpty()
	)).done([=](const MTPmessages_BotCallbackAnswer &result) {
		const auto item = owner->message(fullId);
		if (!item) {
			return;
		}
		if (const auto button = getButton()) {
			button->requestId = 0;
			owner->requestItemRepaint(item);
		}
		const auto &data = result.match([](
				const auto &data) -> const MTPDmessages_botCallbackAnswer& {
			return data;
		});
		const auto message = data.vmessage()
			? qs(*data.vmessage())
			: QString();
		const auto link = data.vurl() ? qs(*data.vurl()) : QString();
		const auto showAlert = data.is_alert();

		if (!message.isEmpty()) {
			if (showAlert) {
				Ui::show(Ui::MakeInformBox(message));
			} else {
				if (withPassword) {
					Ui::hideLayer();
				}
				Ui::Toast::Show(message);
			}
		} else if (!link.isEmpty()) {
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
			handleError(error.type());
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
	SendBotCallbackData(item, row, column, std::nullopt);
}

void SendBotCallbackDataWithPassword(
		not_null<HistoryItem*> item,
		int row,
		int column) {
	if (!item->isRegular()) {
		return;
	}
	const auto history = item->history();
	const auto session = &history->session();
	const auto owner = &history->owner();
	const auto api = &session->api();
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
	api->cloudPassword().reload();
	SendBotCallbackData(item, row, column, std::nullopt, [=](const QString &error) {
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
			api->cloudPassword().state(
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
						SendBotCallbackData(item, row, column, result, [=](const QString &error) {
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
