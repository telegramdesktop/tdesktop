/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_suggest_post.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "chat_helpers/message_field.h"
#include "core/click_handler_types.h"
#include "data/components/credits.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_saved_sublist.h"
#include "history/view/controls/history_view_suggest_options.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "mainwindow.h"
#include "settings/settings_credits_graphics.h"
#include "ui/boxes/choose_date_time.h"
#include "ui/layers/generic_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace Api {
namespace {

void SendApproval(
		std::shared_ptr<Main::SessionShow> show,
		not_null<HistoryItem*> item,
		TimeId scheduleDate = 0) {
	using Flag = MTPmessages_ToggleSuggestedPostApproval::Flag;
	const auto suggestion = item->Get<HistoryMessageSuggestedPost>();
	if (!suggestion
		|| suggestion->accepted
		|| suggestion->rejected
		|| suggestion->requestId) {
		return;
	}

	const auto id = item->fullId();
	const auto session = &show->session();
	const auto finish = [=] {
		if (const auto item = session->data().message(id)) {
			const auto suggestion = item->Get<HistoryMessageSuggestedPost>();
			if (suggestion) {
				suggestion->requestId = 0;
			}
		}
	};
	suggestion->requestId = session->api().request(
		MTPmessages_ToggleSuggestedPostApproval(
			MTP_flags(scheduleDate ? Flag::f_schedule_date : Flag()),
			item->history()->peer->input,
			MTP_int(item->id.bare),
			MTP_int(scheduleDate),
			MTPstring()) // reject_comment
	).done([=](const MTPUpdates &result) {
		session->api().applyUpdates(result);
		finish();
	}).fail([=](const MTP::Error &error) {
		show->showToast(error.type());
		finish();
	}).send();
}

void ConfirmApproval(
		std::shared_ptr<Main::SessionShow> show,
		not_null<HistoryItem*> item,
		TimeId scheduleDate = 0,
		Fn<void()> accepted = nullptr) {
	const auto suggestion = item->Get<HistoryMessageSuggestedPost>();
	if (!suggestion
		|| suggestion->accepted
		|| suggestion->rejected
		|| suggestion->requestId) {
		return;
	}
	const auto id = item->fullId();
	const auto price = suggestion->price;
	const auto admin = item->history()->amMonoforumAdmin();
	if (!admin && !price.empty()) {
		const auto credits = &item->history()->session().credits();
		if (price.ton()) {
			if (!credits->tonLoaded()) {
				credits->tonLoad();
				return;
			} else if (price > credits->tonBalance()) {
				const auto peer = item->history()->peer;
				show->show(
					Box(HistoryView::InsufficientTonBox, peer, price));
				return;
			}
		} else {
			if (!credits->loaded()) {
				credits->load();
				return;
			} else if (price > credits->balance()) {
				using namespace Settings;
				const auto peer = item->history()->peer;
				const auto broadcast = peer->monoforumBroadcast();
				const auto broadcastId = (broadcast ? broadcast : peer)->id;
				const auto done = [=](SmallBalanceResult result) {
					if (result == SmallBalanceResult::Success
						|| result == SmallBalanceResult::Already) {
						const auto item = peer->owner().message(id);
						if (item) {
							ConfirmApproval(
								show,
								item,
								scheduleDate,
								accepted);
						}
					}
				};
				MaybeRequestBalanceIncrease(
					show,
					int(base::SafeRound(price.value())),
					SmallBalanceForSuggest{ broadcastId },
					done);
				return;
			}
		}
	}
	const auto peer = item->history()->peer;
	const auto session = &peer->session();
	const auto broadcast = peer->monoforumBroadcast();
	const auto channelName = (broadcast ? broadcast : peer)->name();
	const auto amount = admin
		? HistoryView::PriceAfterCommission(session, price)
		: price;
	const auto commission = HistoryView::FormatAfterCommissionPercent(
		session,
		price);
	const auto date = langDateTime(base::unixtime::parse(scheduleDate));
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto callback = std::make_shared<Fn<void()>>();
		auto text = admin
			? tr::lng_suggest_accept_text(
				tr::now,
				lt_from,
				Ui::Text::Bold(item->from()->shortName()),
				Ui::Text::WithEntities)
			: tr::lng_suggest_accept_text_to(
				tr::now,
				lt_channel,
				Ui::Text::Bold(channelName),
				Ui::Text::WithEntities);
		if (price) {
			text.append("\n\n").append(admin
				? (scheduleDate
					? (amount.stars()
						? tr::lng_suggest_accept_receive_stars
						: tr::lng_suggest_accept_receive_ton)(
							tr::now,
							lt_count_decimal,
							amount.value(),
							lt_channel,
							Ui::Text::Bold(channelName),
							lt_percent,
							TextWithEntities{ commission },
							lt_date,
							Ui::Text::Bold(date),
							Ui::Text::RichLangValue)
					: (amount.stars()
						? tr::lng_suggest_accept_receive_now_stars
						: tr::lng_suggest_accept_receive_now_ton)(
							tr::now,
							lt_count_decimal,
							amount.value(),
							lt_channel,
							Ui::Text::Bold(channelName),
							lt_percent,
							TextWithEntities{ commission },
							Ui::Text::RichLangValue))
				: (scheduleDate
					? (amount.stars()
						? tr::lng_suggest_accept_pay_stars
						: tr::lng_suggest_accept_pay_ton)(
							tr::now,
							lt_count_decimal,
							amount.value(),
							lt_date,
							Ui::Text::Bold(date),
							Ui::Text::RichLangValue)
					: (amount.stars()
						? tr::lng_suggest_accept_pay_now_stars
						: tr::lng_suggest_accept_pay_now_ton)(
							tr::now,
							lt_count_decimal,
							amount.value(),
							Ui::Text::RichLangValue)));
			if (admin) {
				text.append(' ').append(
					tr::lng_suggest_accept_receive_if(
						tr::now,
						Ui::Text::RichLangValue));
				if (price.stars()) {
					text.append("\n\n").append(
						tr::lng_suggest_options_stars_warning(
							tr::now,
							Ui::Text::RichLangValue));
				}
			}
		}
		Ui::ConfirmBox(box, {
			.text = text,
			.confirmed = [=](Fn<void()> close) { (*callback)(); close(); },
			.confirmText = tr::lng_suggest_accept_send(),
			.title = tr::lng_suggest_accept_title(),
		});
		*callback = [=, weak = base::make_weak(box)] {
			if (const auto onstack = accepted) {
				onstack();
			}
			const auto item = show->session().data().message(id);
			if (!item) {
				return;
			}
			SendApproval(show, item, scheduleDate);
			if (const auto strong = weak.get()) {
				strong->closeBox();
			}
		};
	}));
}

void SendDecline(
		std::shared_ptr<Main::SessionShow> show,
		not_null<HistoryItem*> item,
		const QString &comment) {
	using Flag = MTPmessages_ToggleSuggestedPostApproval::Flag;
	const auto suggestion = item->Get<HistoryMessageSuggestedPost>();
	if (!suggestion
		|| suggestion->accepted
		|| suggestion->rejected
		|| suggestion->requestId) {
		return;
	}

	const auto id = item->fullId();
	const auto session = &show->session();
	const auto finish = [=] {
		if (const auto item = session->data().message(id)) {
			const auto suggestion = item->Get<HistoryMessageSuggestedPost>();
			if (suggestion) {
				suggestion->requestId = 0;
			}
		}
	};
	suggestion->requestId = session->api().request(
		MTPmessages_ToggleSuggestedPostApproval(
			MTP_flags(Flag::f_reject
				| (comment.isEmpty() ? Flag() : Flag::f_reject_comment)),
			item->history()->peer->input,
			MTP_int(item->id.bare),
			MTPint(), // schedule_date
			MTP_string(comment))
	).done([=](const MTPUpdates &result) {
		session->api().applyUpdates(result);
		finish();
	}).fail([=](const MTP::Error &error) {
		show->showToast(error.type());
		finish();
	}).send();
}

void RequestApprovalDate(
		std::shared_ptr<Main::SessionShow> show,
		not_null<HistoryItem*> item) {
	const auto id = item->fullId();
	const auto weak = std::make_shared<base::weak_qptr<Ui::BoxContent>>();
	const auto close = [=] {
		if (const auto strong = weak->get()) {
			strong->closeBox();
		}
	};
	const auto done = [=](TimeId result) {
		if (const auto item = show->session().data().message(id)) {
			ConfirmApproval(show, item, result, close);
		} else {
			close();
		}
	};
	using namespace HistoryView;
	auto dateBox = Box(ChooseSuggestTimeBox, SuggestTimeBoxArgs{
		.session = &show->session(),
		.done = done,
		.mode = SuggestMode::Publish,
	});
	*weak = dateBox.data();
	show->show(std::move(dateBox));
}

void RequestDeclineComment(
		std::shared_ptr<Main::SessionShow> show,
		not_null<HistoryItem*> item) {
	const auto id = item->fullId();
	const auto admin = item->history()->amMonoforumAdmin();
	const auto peer = item->history()->peer;
	const auto broadcast = peer->monoforumBroadcast();
	const auto channelName = (broadcast ? broadcast : peer)->name();
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto callback = std::make_shared<Fn<void()>>();
		Ui::ConfirmBox(box, {
			.text = (admin
				? tr::lng_suggest_decline_text(
					lt_from,
					rpl::single(Ui::Text::Bold(item->from()->shortName())),
					Ui::Text::WithEntities)
				: tr::lng_suggest_decline_text_to(
					lt_channel,
					rpl::single(Ui::Text::Bold(channelName)),
					Ui::Text::WithEntities)),
			.confirmed = [=](Fn<void()> close) { (*callback)(); close(); },
			.confirmText = tr::lng_suggest_action_decline(),
			.confirmStyle = &st::attentionBoxButton,
			.title = tr::lng_suggest_decline_title(),
		});
		const auto reason = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::factcheckField,
			Ui::InputField::Mode::NoNewlines,
			tr::lng_suggest_decline_reason()));
		box->setFocusCallback([=] {
			reason->setFocusFast();
		});
		*callback = [=, weak = base::make_weak(box)] {
			const auto item = show->session().data().message(id);
			if (!item) {
				return;
			}
			SendDecline(show, item, reason->getLastText().trimmed());
			if (const auto strong = weak.get()) {
				strong->closeBox();
			}
		};
		reason->submits(
		) | rpl::start_with_next([=](Qt::KeyboardModifiers modifiers) {
			if (!(modifiers & Qt::ShiftModifier)) {
				(*callback)();
			}
		}, box->lifetime());
	}));
}

struct SendSuggestState {
	SendPaymentHelper sendPayment;
};
void SendSuggest(
		std::shared_ptr<Main::SessionShow> show,
		not_null<HistoryItem*> item,
		std::shared_ptr<SendSuggestState> state,
		Fn<void(SuggestPostOptions&)> modify,
		Fn<void()> done = nullptr,
		int starsApproved = 0) {
	const auto suggestion = item->Get<HistoryMessageSuggestedPost>();
	const auto id = item->fullId();
	const auto withPaymentApproved = [=](int stars) {
		if (const auto item = show->session().data().message(id)) {
			SendSuggest(show, item, state, modify, done, stars);
		}
	};
	const auto isForward = item->Get<HistoryMessageForwarded>();
	auto action = SendAction(item->history());
	action.options.suggest.exists = 1;
	if (suggestion) {
		action.options.suggest.date = suggestion->date;
		action.options.suggest.priceWhole = suggestion->price.whole();
		action.options.suggest.priceNano = suggestion->price.nano();
		action.options.suggest.ton = suggestion->price.ton() ? 1 : 0;
	}
	modify(action.options.suggest);
	action.options.starsApproved = starsApproved;
	action.replyTo.monoforumPeerId = item->history()->amMonoforumAdmin()
		? item->sublistPeerId()
		: PeerId();
	action.replyTo.messageId = item->fullId();

	const auto checked = state->sendPayment.check(
		show,
		item->history()->peer,
		action.options,
		1,
		withPaymentApproved);
	if (!checked) {
		return;
	}

	show->session().api().sendAction(action);
	show->session().api().forwardMessages({
		.items = { item },
		.options = (isForward
			? Data::ForwardOptions::PreserveInfo
			: Data::ForwardOptions::NoSenderNames),
		}, action);
	if (const auto onstack = done) {
		onstack();
	}
}

void SuggestApprovalDate(
		std::shared_ptr<Main::SessionShow> show,
		not_null<HistoryItem*> item) {
	const auto suggestion = item->Get<HistoryMessageSuggestedPost>();
	if (!suggestion) {
		return;
	}
	const auto id = item->fullId();
	const auto state = std::make_shared<SendSuggestState>();
	const auto weak = std::make_shared<base::weak_qptr<Ui::BoxContent>>();
	const auto done = [=](TimeId result) {
		const auto item = show->session().data().message(id);
		if (!item) {
			return;
		}
		const auto close = [=] {
			if (const auto strong = weak->get()) {
				strong->closeBox();
			}
		};
		SendSuggest(
			show,
			item,
			state,
			[=](SuggestPostOptions &options) { options.date = result; },
			close);
	};
	using namespace HistoryView;
	auto dateBox = Box(ChooseSuggestTimeBox, SuggestTimeBoxArgs{
		.session = &show->session(),
		.done = done,
		.value = suggestion->date,
		.mode = SuggestMode::Change,
	});
	*weak = dateBox.data();
	show->show(std::move(dateBox));
}

void SuggestOfferForMessage(
		std::shared_ptr<Main::SessionShow> show,
		not_null<HistoryItem*> item,
		SuggestPostOptions values,
		HistoryView::SuggestMode mode) {
	const auto id = item->fullId();
	const auto state = std::make_shared<SendSuggestState>();
	const auto weak = std::make_shared<base::weak_qptr<Ui::BoxContent>>();
	const auto done = [=](SuggestPostOptions result) {
		const auto item = show->session().data().message(id);
		if (!item) {
			return;
		}
		const auto close = [=] {
			if (const auto strong = weak->get()) {
				strong->closeBox();
			}
		};
		SendSuggest(
			show,
			item,
			state,
			[=](SuggestPostOptions &options) { options = result; },
			close);
	};
	using namespace HistoryView;
	auto priceBox = Box(ChooseSuggestPriceBox, SuggestPriceBoxArgs{
		.peer = item->history()->peer,
		.done = done,
		.value = values,
		.mode = mode,
	});
	*weak = priceBox.data();
	show->show(std::move(priceBox));
}

void SuggestApprovalPrice(
		std::shared_ptr<Main::SessionShow> show,
		not_null<HistoryItem*> item) {
	const auto suggestion = item->Get<HistoryMessageSuggestedPost>();
	if (!suggestion) {
		return;
	}
	using namespace HistoryView;
	SuggestOfferForMessage(show, item, {
		.exists = uint32(1),
		.priceWhole = uint32(suggestion->price.whole()),
		.priceNano = uint32(suggestion->price.nano()),
		.ton = uint32(suggestion->price.ton() ? 1 : 0),
		.date = suggestion->date,
	}, SuggestMode::Change);
}

} // namespace

std::shared_ptr<ClickHandler> AcceptClickHandler(
		not_null<HistoryItem*> item) {
	const auto session = &item->history()->session();
	const auto id = item->fullId();
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		const auto controller = my.sessionWindow.get();
		if (!controller || &controller->session() != session) {
			return;
		}
		const auto item = session->data().message(id);
		if (!item) {
			return;
		}
		const auto show = controller->uiShow();
		const auto suggestion = item->Get<HistoryMessageSuggestedPost>();
		if (!suggestion) {
			return;
		} else if (!suggestion->date) {
			RequestApprovalDate(show, item);
		} else {
			ConfirmApproval(show, item);
		}
	});
}

std::shared_ptr<ClickHandler> DeclineClickHandler(
		not_null<HistoryItem*> item) {
	const auto session = &item->history()->session();
	const auto id = item->fullId();
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		const auto controller = my.sessionWindow.get();
		if (!controller || &controller->session() != session) {
			return;
		}
		const auto item = session->data().message(id);
		if (!item) {
			return;
		}
		RequestDeclineComment(controller->uiShow(), item);
	});
}

std::shared_ptr<ClickHandler> SuggestChangesClickHandler(
		not_null<HistoryItem*> item) {
	const auto session = &item->history()->session();
	const auto id = item->fullId();
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		const auto window = my.sessionWindow.get();
		if (!window || &window->session() != session) {
			return;
		}
		const auto item = session->data().message(id);
		if (!item) {
			return;
		}
		const auto menu = Ui::CreateChild<Ui::PopupMenu>(
			window->widget(),
			st::popupMenuWithIcons);
		if (HistoryView::CanEditSuggestedMessage(item)) {
			menu->addAction(tr::lng_suggest_menu_edit_message(tr::now), [=] {
				const auto item = session->data().message(id);
				if (!item) {
					return;
				}
				const auto suggestion = item->Get<HistoryMessageSuggestedPost>();
				if (!suggestion) {
					return;
				}
				const auto history = item->history();
				const auto editData = PrepareEditText(item);
				const auto cursor = MessageCursor{
					int(editData.text.size()),
					int(editData.text.size()),
					Ui::kQFixedMax
				};
				const auto monoforumPeerId = history->amMonoforumAdmin()
					? item->sublistPeerId()
					: PeerId();
				const auto previewDraft = Data::WebPageDraft::FromItem(item);
				history->setLocalEditDraft(std::make_unique<Data::Draft>(
					editData,
					FullReplyTo{
						.messageId = FullMsgId(history->peer->id, item->id),
						.monoforumPeerId = monoforumPeerId,
					},
					SuggestPostOptions{
						.exists = uint32(1),
						.priceWhole = uint32(suggestion->price.whole()),
						.priceNano = uint32(suggestion->price.nano()),
						.ton = uint32(suggestion->price.ton() ? 1 : 0),
						.date = suggestion->date,
					},
					cursor,
					previewDraft));
				history->session().changes().entryUpdated(
					(monoforumPeerId
						? item->savedSublist()
						: (Data::Thread*)history.get()),
					Data::EntryUpdate::Flag::LocalDraftSet);
			}, &st::menuIconEdit);
		}
		menu->addAction(tr::lng_suggest_menu_edit_price(tr::now), [=] {
			if (const auto item = session->data().message(id)) {
				SuggestApprovalPrice(window->uiShow(), item);
			}
		}, &st::menuIconTagSell);
		menu->addAction(tr::lng_suggest_menu_edit_time(tr::now), [=] {
			if (const auto item = session->data().message(id)) {
				SuggestApprovalDate(window->uiShow(), item);
			}
		}, &st::menuIconSchedule);
		menu->popup(QCursor::pos());
	});
}

void AddOfferToMessage(
		std::shared_ptr<Main::SessionShow> show,
		FullMsgId itemId) {
	const auto session = &show->session();
	const auto item = session->data().message(itemId);
	if (!item || !HistoryView::CanAddOfferToMessage(item)) {
		return;
	}
	SuggestOfferForMessage(show, item, {}, HistoryView::SuggestMode::New);
}

} // namespace Api
