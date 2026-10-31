/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_bridge.h"

#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/file_utilities.h"
#include "core/local_url_handlers.h"
#include "data/data_peer.h"
#include "data/data_thread.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "twidget/sdk_policy.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/toast/toast.h"
#include "ui/widgets/labels.h"
#include "window/window_controller.h"
#include "window/window_separate_id.h"
#include "window/window_session_controller.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"

namespace Tgw {
namespace {

// WHY: offline MTP keeps a request queued forever and the widget can ask
// nothing more until it completes, so an unanswered request fails after
// twice the server's wait for the bot.
constexpr auto kRequestTimeout = crl::time(30000);
constexpr auto kCopiedToastLines = 4;

using Failure = twidget::WidgetRequestFailure;

[[nodiscard]] twidget::WidgetRequestOutcome Outcome(Failure failure) {
	return { .failure = failure };
}

[[nodiscard]] twidget::WidgetRequestOutcome AnswerOutcome(
		const MTPmessages_BotWidgetQueryAnswer &answer) {
	auto result = twidget::WidgetRequestOutcome();
	if (const auto bytes = answer.data().vresult()) {
		result.answer.result = bytes->v.toStdString();
	}
	return result;
}

[[nodiscard]] twidget::WidgetRequestOutcome ErrorOutcome(
		const MTP::Error &error) {
	if (MTP::IsFloodError(error)) {
		return Outcome(Failure::RateLimited);
	} else if (error.type() == u"WIDGET_RESPONSE_TIMEOUT"_q) {
		return Outcome(Failure::Timeout);
	}
	return Outcome(Failure::Failed);
}

[[nodiscard]] Window::SessionController *ControllerOf(
		Window::Controller *window,
		not_null<Main::Session*> session) {
	const auto controller = window ? window->sessionController() : nullptr;
	return (controller && (&controller->session() == session.get()))
		? controller
		: nullptr;
}

[[nodiscard]] Window::SessionController *FindController(
		not_null<Data::Thread*> thread) {
	const auto session = &thread->session();
	const auto preferred = Core::App().windowFor(Window::SeparateId(thread));
	if (const auto controller = ControllerOf(preferred, session)) {
		return controller;
	}
	auto result = (Window::SessionController*)nullptr;
	Core::App().enumerateWindows([&](not_null<Window::Controller*> window) {
		if (!result) {
			result = ControllerOf(window, session);
		}
	});
	return result;
}

void ConfirmExternalUrl(
		not_null<Window::SessionController*> controller,
		const QString &url) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		Ui::ConfirmBox(box, {
			.text = tr::lng_open_this_link(tr::now),
			.confirmed = [=](Fn<void()> close) {
				close();
				File::OpenUrl(url);
			},
			.confirmText = tr::lng_open_link(),
		});
		box->addSkip(st::boxLabel.style.lineHeight - st::boxPadding.bottom());
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			url,
			st::boxLabel))->setSelectable(true);
	}));
}

} // namespace

BotQueries::BotQueries(not_null<Main::Session*> session)
: _api(&session->mtp())
, _deadlines([=] { checkDeadlines(); }) {
}

BotQueries::~BotQueries() {
	_deadlines.cancel();
	for (auto &[id, pending] : base::take(_pending)) {
		pending.respond(Outcome(Failure::Failed));
	}
}

void BotQueries::send(
		not_null<PeerData*> peer,
		MsgId msgId,
		const MTPWidgetQueryType &type,
		const std::string &data,
		Respond respond) {
	if (!IsServerMsgId(msgId)) {
		respond(Outcome(Failure::Failed));
		return;
	}
	const auto id = _api.request(MTPmessages_GetBotWidgetQueryAnswer(
		peer->input(),
		MTP_int(msgId),
		type,
		MTP_bytes(QByteArray::fromStdString(data))
	)).done([=](
			const MTPmessages_BotWidgetQueryAnswer &result,
			mtpRequestId requestId) {
		finish(requestId, AnswerOutcome(result));
	}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
		finish(requestId, ErrorOutcome(error));
	}).handleAllErrors().send();
	_pending.emplace(id, Pending{
		.respond = std::move(respond),
		.deadline = crl::now() + kRequestTimeout,
	});
	if (!_deadlines.isActive()) {
		_deadlines.callOnce(kRequestTimeout);
	}
}

void BotQueries::finish(
		mtpRequestId id,
		const twidget::WidgetRequestOutcome &outcome) {
	const auto i = _pending.find(id);
	if (i == end(_pending)) {
		return;
	}
	const auto respond = std::move(i->second.respond);
	_pending.erase(i);
	respond(outcome);
}

void BotQueries::checkDeadlines() {
	const auto now = crl::now();
	auto expired = std::vector<mtpRequestId>();
	for (const auto &[id, pending] : _pending) {
		if (pending.deadline <= now) {
			expired.push_back(id);
		}
	}
	for (const auto id : expired) {
		_api.request(id).cancel();
		finish(id, Outcome(Failure::Failed));
	}
	if (_pending.empty()) {
		return;
	}
	const auto nearest = ranges::min_element(_pending, {}, [](auto &&pair) {
		return pair.second.deadline;
	})->second.deadline;
	_deadlines.callOnce(std::max(nearest - crl::now(), crl::time()));
}

Bridge::Bridge(not_null<HistoryItem*> item)
: _item(item)
, _queries(&item->history()->session()) {
}

Window::SessionController *Bridge::findController() const {
	const auto thread = _item->maybeNotificationThread();
	return FindController(thread ? thread : _item->history().get());
}

QString Bridge::openLink(const QString &url, bool telegram) {
	const auto method = telegram ? u"openTelegramLink"_q : u"openLink"_q;
	const auto controller = findController();
	const auto local = Core::TryConvertUrlToLocal(url);
	if (!controller) {
		return method + u": refused, no window of this account is open"_q;
	} else if (controller->isLayerShown()) {
		return method + u": dropped, a dialog is already shown"_q;
	} else if (Core::InternalPassportOrOAuthLink(local)) {
		return method + u": refused, a passport or login link"_q;
	} else if (!telegram) {
		// WHY: the app's handler may open a converted or authorized link instead.
		ConfirmExternalUrl(controller, url);
		return u"openLink: asked to confirm the link"_q;
	}
	// As a visible t.me link tapped in this message: only its own dialogs show.
	const auto context = ClickHandlerContext{
		.itemId = _item->fullId(),
		.sessionWindow = base::make_weak(controller),
	};
	UrlClickHandler::Open(
		UrlClickHandler::EncodeForOpening(url),
		QVariant::fromValue(context));
	return u"openTelegramLink: passed to the app's t.me handling"_q;
}

QString Bridge::copyText(const QString &text, not_null<QWidget*> window) {
	const auto controller = WindowController(
		window,
		&_item->history()->session());
	if (!controller) {
		return u"copyText: refused, no window of this account shows it"_q;
	}
	// Protected chats copy too: the text is the bot's, not message content.
	QGuiApplication::clipboard()->setText(text);
	controller->showToast({
		.text = tr::lng_tgw_copied(
			tr::now,
			lt_text,
			tr::marked(text),
			tr::marked),
		.maxlines = kCopiedToastLines,
		.iconLottie = u"toast/copy"_q,
		.iconLottieSize = st::toastLottieIconSize,
	});
	return u"copyText: copied %1 UTF-16 units"_q.arg(text.size());
}

void Bridge::request(
		const std::string &data,
		bool wallet,
		Respond respond) {
	if (!_item->isRegular()) {
		respond(Outcome(Failure::Failed));
		return;
	}
	using Flag = MTPDwidgetQueryTypeData::Flag;
	_queries.send(
		_item->history()->peer,
		_item->id,
		MTP_widgetQueryTypeData(
			MTP_flags(wallet ? Flag::f_with_wallet : Flag())),
		data,
		std::move(respond));
}

Window::SessionController *WindowController(
		not_null<QWidget*> window,
		not_null<Main::Session*> session) {
	return ControllerOf(Core::App().findWindow(window), session);
}

} // namespace Tgw
