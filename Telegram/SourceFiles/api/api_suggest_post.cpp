/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_suggest_post.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "core/click_handler_types.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/choose_date_time.h"
#include "ui/layers/generic_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/fields/input_field.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"

namespace Api {
namespace {

void SendApproval(
		not_null<Window::SessionController*> controller,
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
	const auto weak = base::make_weak(controller);
	const auto session = &controller->session();
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
		if (const auto window = weak.get()) {
			window->showToast(error.type());
		}
		finish();
	}).send();
}

void SendDecline(
		not_null<Window::SessionController*> controller,
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
	const auto weak = base::make_weak(controller);
	const auto session = &controller->session();
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
		if (const auto window = weak.get()) {
			window->showToast(error.type());
		}
		finish();
	}).send();
}

void RequestApprovalDate(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	const auto weak = std::make_shared<QPointer<Ui::BoxContent>>();
	const auto done = [=](TimeId result) {
		SendApproval(controller, item, result);
		if (const auto strong = weak->data()) {
			strong->closeBox();
		}
	};
	auto dateBox = Box(Ui::ChooseDateTimeBox, Ui::ChooseDateTimeBoxArgs{
		.title = tr::lng_suggest_options_date(),
		.submit = tr::lng_settings_save(),
		.done = done,
		.min = [] { return base::unixtime::now() + 1; },
		.time = (base::unixtime::now() + 86400),
	});
	*weak = dateBox.data();
	controller->uiShow()->show(std::move(dateBox));
}

void RequestDeclineComment(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	const auto id = item->fullId();
	controller->uiShow()->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto callback = std::make_shared<Fn<void()>>();
		Ui::ConfirmBox(box, {
			.text = tr::lng_suggest_decline_text(
				lt_from,
				rpl::single(Ui::Text::Bold(item->from()->shortName())),
				Ui::Text::WithEntities),
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
		*callback = [=, weak = Ui::MakeWeak(box)] {
			const auto item = controller->session().data().message(id);
			if (!item) {
				return;
			}
			SendDecline(controller, item, reason->getLastText().trimmed());
			if (const auto strong = weak.data()) {
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
		const auto suggestion = item->Get<HistoryMessageSuggestedPost>();
		if (!suggestion) {
			return;
		} else if (!suggestion->date) {
			RequestApprovalDate(controller, item);
		} else {
			SendApproval(controller, item);
		}
	});
}

std::shared_ptr<ClickHandler> DeclineClickHandler(
		not_null<HistoryItem*> item) {
	const auto id = item->fullId();
	return std::make_shared<LambdaClickHandler>([=](ClickContext context) {
		const auto my = context.other.value<ClickHandlerContext>();
		const auto controller = my.sessionWindow.get();
		if (!controller) {
			return;
		}
		RequestDeclineComment(controller, item);
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

	});
}

} // namespace Api
