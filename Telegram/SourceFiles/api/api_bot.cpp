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
#include "api/api_suggest_post.h"
#include "boxes/share_box.h"
#include "boxes/passcode_box.h"
#include "boxes/url_auth_box.h"
#include "boxes/peers/choose_peer_box.h"
#include "lang/lang_keys.h"
#include "chat_helpers/bot_command.h"
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
#include "payments/payments_non_panel_process.h"
#include "main/main_session.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "window/window_session_controller.h"
#include "window/window_peer_menu.h"
#include "ui/boxes/confirm_box.h"
#include "ui/toast/toast.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace Api {
namespace {

void SendBotCallbackData(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item,
		int row,
		int column,
		std::optional<Core::CloudPasswordResult> password,
		Fn<void()> done = nullptr,
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
		return HistoryMessageMarkupButton::Get(owner, fullId, row, column);
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
	const auto weak = base::make_weak(controller);
	const auto show = controller->uiShow();
	button->requestId = api->request(MTPmessages_GetBotCallbackAnswer(
		MTP_flags(flags),
		history->peer->input,
		MTP_int(item->id),
		MTP_bytes(sendData),
		password ? password->result : MTP_inputCheckPasswordEmpty()
	)).done([=](const MTPmessages_BotCallbackAnswer &result) {
		const auto guard = gsl::finally([&] {
			if (done) {
				done();
			}
		});
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
			if (!show->valid()) {
				return;
			} else if (showAlert) {
				show->showBox(Ui::MakeInformBox(message));
			} else {
				if (withPassword) {
					show->hideLayer();
				}
				show->showToast(message);
			}
		} else if (!link.isEmpty()) {
			if (!isGame) {
				UrlClickHandler::Open(link);
				return;
			}
			BotGameUrlClickHandler(bot, link).onClick({
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
		const auto guard = gsl::finally([&] {
			if (handleError) {
				handleError(error.type());
			}
		});
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

void HideSingleUseKeyboard(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	controller->content()->hideSingleUseKeyboard(item->fullId());
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
	const auto weak = base::make_weak(controller);
	const auto show = controller->uiShow();
	SendBotCallbackData(controller, item, row, column, {}, {}, [=](
			const QString &error) {
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
				auto fields = PasscodeBox::CloudFields::From(state);
				fields.customTitle = tr::lng_bots_password_confirm_title();
				fields.customDescription
					= tr::lng_bots_password_confirm_description(tr::now);
				fields.customSubmitButton = tr::lng_passcode_submit();
				fields.customCheckCallback = [=](
						const Core::CloudPasswordResult &result,
						base::weak_qptr<PasscodeBox> box) {
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
						SendBotCallbackData(strongController, item, row, column, result, [=] {
							if (box) {
								box->closeBox();
							}
						}, [=](const QString &error) {
							if (box) {
								box->handleCustomCheckError(error);
							}
						});
					}
				};
				auto object = Box<PasscodeBox>(session, fields);
				show->showBox(std::move(object), Ui::LayerOption::CloseOther);
			}, *lifetime);
		}
	});
}

bool SwitchInlineBotButtonReceived(
		not_null<Window::SessionController*> controller,
		const QByteArray &queryWithPeerTypes,
		UserData *samePeerBot,
		MsgId samePeerReplyTo) {
	return controller->content()->notify_switchInlineBotButtonReceived(
		QString::fromUtf8(queryWithPeerTypes),
		samePeerBot,
		samePeerReplyTo);
}

void ActivateBotCommand(ClickHandlerContext context, int row, int column) {
	const auto strong = context.sessionWindow.get();
	if (!strong) {
		return;
	}
	const auto controller = not_null{ strong };
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
		const auto replyTo = item->isRegular()
			? item->fullId()
			: FullMsgId();
		controller->content()->sendBotCommand({
			.peer = item->history()->peer,
			.command = QString(button->text),
			.context = item->fullId(),
			.replyTo = { replyTo },
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
			}),
			Payments::ProcessNonPanelPaymentFormFactory(controller, item));
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
		const auto itemId = item->fullId();
		const auto topicRootId = item->topicRootId();
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
				action.replyTo = {
					.messageId = itemId,
					.topicRootId = topicRootId,
				};
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
		const auto replyTo = FullReplyTo();
		const auto suggest = SuggestPostOptions();
		Window::PeerMenuCreatePoll(
			controller,
			item->history()->peer,
			replyTo,
			suggest,
			chosen,
			disabled);
	} break;

	case ButtonType::RequestPeer: {
		HideSingleUseKeyboard(controller, item);

		auto query = RequestPeerQuery();
		Assert(button->data.size() == sizeof(query));
		memcpy(&query, button->data.data(), sizeof(query));
		const auto peer = item->history()->peer;
		const auto itemId = item->id;
		const auto id = int32(button->buttonId);
		const auto chosen = [=](std::vector<not_null<PeerData*>> result) {
			peer->session().api().request(MTPmessages_SendBotRequestedPeer(
				peer->input,
				MTP_int(itemId),
				MTP_int(id),
				MTP_vector_from_range(
					result | ranges::views::transform([](
							not_null<PeerData*> peer) {
						return MTPInputPeer(peer->input);
					}))
			)).done([=](const MTPUpdates &result) {
				peer->session().api().applyUpdates(result);
			}).send();
		};
		if (const auto bot = item->getMessageBot()) {
			ShowChoosePeerBox(controller, bot, query, chosen);
		} else {
			LOG(("API Error: Bot not found for RequestPeer button."));
		}
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
						button->data,
						bot,
						item->id);
					return true;
				} else if (bot->isBot() && bot->botInfo->inlineReturnTo.key) {
					const auto switched = SwitchInlineBotButtonReceived(
						controller,
						button->data);
					if (switched) {
						return true;
					}
				}
				return false;
			}();
			if (!fastSwitchDone) {
				const auto query = QString::fromUtf8(button->data);
				const auto chosen = [=](not_null<Data::Thread*> thread) {
					return controller->switchInlineQuery(
						thread,
						bot,
						query);
				};
				Window::ShowChooseRecipientBox(
					controller,
					chosen,
					tr::lng_inline_switch_choose(),
					nullptr,
					button->peerTypes);
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
			bot->session().attachWebView().open({
				.bot = bot,
				.context = { .controller = controller },
				.button = { .text = button->text, .url = button->data },
				.source = InlineBots::WebViewSourceButton{ .simple = false },
			});
		}
	} break;

	case ButtonType::SimpleWebView: {
		if (const auto bot = item->getMessageBot()) {
			bot->session().attachWebView().open({
				.bot = bot,
				.context = { .controller = controller },
				.button = { .text = button->text, .url = button->data },
				.source = InlineBots::WebViewSourceButton{ .simple = true },
			});
		}
	} break;

	case ButtonType::CopyText: {
		const auto text = QString::fromUtf8(button->data);
		if (!text.isEmpty()) {
			QGuiApplication::clipboard()->setText(text);
			controller->showToast(tr::lng_text_copied(tr::now));
		}
	} break;

	case ButtonType::SuggestAccept: {
		Api::AcceptClickHandler(item)->onClick(ClickContext{
			Qt::LeftButton,
			QVariant::fromValue(context),
		});
	} break;

	case ButtonType::SuggestDecline: {
		Api::DeclineClickHandler(item)->onClick(ClickContext{
			Qt::LeftButton,
			QVariant::fromValue(context),
		});
	} break;

	case ButtonType::SuggestChange: {
		Api::SuggestChangesClickHandler(item)->onClick(ClickContext{
			Qt::LeftButton,
			QVariant::fromValue(context),
		});
	} break;
	}
}

} // namespace Api
