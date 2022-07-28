/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_bot.h"

#include "apiwrap.h"
#include "api/api_cloud_password.h"
#include "api/api_send_progress.h"
#include "boxes/share_box.h"
#include "boxes/passcode_box.h"
#include "boxes/url_auth_box.h"
#include "lang/lang_keys.h"
#include "core/core_cloud_password.h"
#include "core/click_handler_types.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "data/data_poll.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "inline_bots/bot_attach_web_view.h"
#include "payments/payments_checkout_process.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "ui/boxes/confirm_box.h"
#include "ui/toast/toast.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"

namespace Api {
namespace {

void SendBotCallbackData(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		int row,
		int column,
		std::optional<Core::CloudPasswordResult> password,
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
	const auto weak = base::make_weak(controller.get());
	const auto show = std::make_shared<Window::Show>(controller);
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
		const auto &data = result.data();
		const auto message = data.vmessage()
			? qs(*data.vmessage())
			: QString();
		const auto link = data.vurl() ? qs(*data.vurl()) : QString();
		const auto showAlert = data.is_alert();

		if (!message.isEmpty()) {
			if (showAlert) {
				show->showBox(Ui::MakeInformBox(message));
			} else {
				if (withPassword) {
					show->hideLayer();
				}
				Ui::Toast::Show(show->toastParent(), message);
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
			BotGameUrlClickHandler(bot, scoreLink).onClick({
				Qt::LeftButton,
				QVariant::fromValue(ClickHandlerContext{
					.itemId = item->fullId(),
					.sessionWindow = weak,
				}),
			});
			session->sendProgressManager().update(
				history,
				Api::SendProgressType::PlayGame);
		} else if (withPassword) {
			show->hideLayer();
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

void HideSingleUseKeyboard(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	controller->content()->hideSingleUseKeyboard(
		item->history()->peer,
		item->id);
}

} // namespace

void SendBotCallbackData(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		int row,
		int column) {
	SendBotCallbackData(controller, item, row, column, std::nullopt);
}

void SendBotCallbackDataWithPassword(
		not_null<Window::SessionController*> controller,
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
	const auto weak = base::make_weak(controller.get());
	const auto show = std::make_shared<Window::Show>(controller);
	SendBotCallbackData(controller, item, row, column, std::nullopt, [=](const QString &error) {
		auto box = PrePasswordErrorBox(
			error,
			session,
			tr::lng_bots_password_confirm_check_about(
				tr::now,
				Ui::Text::WithEntities));
		if (box) {
			show->showBox(std::move(box), Ui::LayerOption::CloseOther);
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
						const auto strongController = weak.get();
						if (!strongController) {
							return;
						}
						SendBotCallbackData(strongController, item, row, column, result, [=](const QString &error) {
							if (*box) {
								(*box)->handleCustomCheckError(error);
							}
						});
					}
				};
				auto object = Box<PasscodeBox>(session, fields);
				*box = Ui::MakeWeak(object.data());
				show->showBox(std::move(object), Ui::LayerOption::CloseOther);
			}, *lifetime);
		}
	});
}

bool SwitchInlineBotButtonReceived(
		not_null<Window::SessionController*> controller,
		const QString &query,
		UserData *samePeerBot,
		MsgId samePeerReplyTo) {
	return controller->content()->notify_switchInlineBotButtonReceived(
		query,
		samePeerBot,
		samePeerReplyTo);
}

void ActivateBotCommand(ClickHandlerContext context, int row, int column) {
	const auto controller = context.sessionWindow.get();
	if (!controller) {
		return;
	}
	const auto item = controller->session().data().message(context.itemId);
	if (!item) {
		return;
	}
	const auto button = HistoryMessageMarkupButton::Get(
		&item->history()->owner(),
		item->fullId(),
		row,
		column);
	if (!button) {
		return;
	}

	using ButtonType = HistoryMessageMarkupButton::Type;
	switch (button->type) {
	case ButtonType::Default: {
		// Copy string before passing it to the sending method
		// because the original button can be destroyed inside.
		const auto replyTo = item->isRegular() ? item->id : 0;
		controller->content()->sendBotCommand({
			.peer = item->history()->peer,
			.command = QString(button->text),
			.context = item->fullId(),
			.replyTo = replyTo,
		});
	} break;

	case ButtonType::Callback:
	case ButtonType::Game: {
		SendBotCallbackData(controller, item, row, column);
	} break;

	case ButtonType::CallbackWithPassword: {
		SendBotCallbackDataWithPassword(controller, item, row, column);
	} break;

	case ButtonType::Buy: {
		Payments::CheckoutProcess::Start(
			item,
			Payments::Mode::Payment,
			crl::guard(controller, [=](auto) {
				controller->widget()->activate();
			}));
	} break;

	case ButtonType::Url: {
		auto url = QString::fromUtf8(button->data);
		auto skipConfirmation = false;
		if (const auto bot = item->getMessageBot()) {
			if (bot->isVerified()) {
				skipConfirmation = true;
			}
		}
		const auto variant = QVariant::fromValue(context);
		if (skipConfirmation) {
			UrlClickHandler::Open(url, variant);
		} else {
			HiddenUrlClickHandler::Open(url, variant);
		}
	} break;

	case ButtonType::RequestLocation: {
		HideSingleUseKeyboard(controller, item);
		controller->show(
			Ui::MakeInformBox(tr::lng_bot_share_location_unavailable()));
	} break;

	case ButtonType::RequestPhone: {
		HideSingleUseKeyboard(controller, item);
		const auto itemId = item->id;
		const auto history = item->history();
		controller->show(Ui::MakeConfirmBox({
			.text = tr::lng_bot_share_phone(),
			.confirmed = [=] {
				controller->showPeerHistory(
					history,
					Window::SectionShow::Way::Forward,
					ShowAtTheEndMsgId);
				auto action = Api::SendAction(history);
				action.clearDraft = false;
				action.replyTo = itemId;
				history->session().api().shareContact(
					history->session().user(),
					action);
			},
			.confirmText = tr::lng_bot_share_phone_confirm(),
		}));
	} break;

	case ButtonType::RequestPoll: {
		HideSingleUseKeyboard(controller, item);
		auto chosen = PollData::Flags();
		auto disabled = PollData::Flags();
		if (!button->data.isEmpty()) {
			disabled |= PollData::Flag::Quiz;
			if (button->data[0]) {
				chosen |= PollData::Flag::Quiz;
			}
		}
		const auto replyToId = MsgId(0);
		Window::PeerMenuCreatePoll(
			controller,
			item->history()->peer,
			replyToId,
			chosen,
			disabled);
	} break;

	case ButtonType::SwitchInlineSame:
	case ButtonType::SwitchInline: {
		if (const auto bot = item->getMessageBot()) {
			const auto fastSwitchDone = [&] {
				const auto samePeer = (button->type
					== ButtonType::SwitchInlineSame);
				if (samePeer) {
					SwitchInlineBotButtonReceived(
						controller,
						QString::fromUtf8(button->data),
						bot,
						item->id);
					return true;
				} else if (bot->isBot() && bot->botInfo->inlineReturnTo.key) {
					const auto switched = SwitchInlineBotButtonReceived(
						controller,
						QString::fromUtf8(button->data));
					if (switched) {
						return true;
					}
				}
				return false;
			}();
			if (!fastSwitchDone) {
				controller->content()->inlineSwitchLayer('@'
					+ bot->username
					+ ' '
					+ QString::fromUtf8(button->data));
			}
		}
	} break;

	case ButtonType::Auth:
		UrlAuthBox::Activate(item, row, column);
		break;

	case ButtonType::UserProfile: {
		const auto session = &item->history()->session();
		const auto userId = UserId(button->data.toULongLong());
		if (const auto user = session->data().userLoaded(userId)) {
			controller->showPeerInfo(user);
		}
	} break;

	case ButtonType::WebView: {
		if (const auto bot = item->getMessageBot()) {
			bot->session().attachWebView().request(
				controller,
				bot,
				bot,
				{ .text = button->text, .url = button->data });
		}
	} break;

	case ButtonType::SimpleWebView: {
		if (const auto bot = item->getMessageBot()) {
			bot->session().attachWebView().requestSimple(
				controller,
				bot,
				{ .text = button->text, .url = button->data });
		}
	} break;
	}
}

} // namespace Api
