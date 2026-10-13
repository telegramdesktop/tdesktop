/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "tgw/tgw_bridge.h"

#include "api/api_common.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/local_url_handlers.h"
#include "data/data_chat_participant_status.h"
#include "data/data_peer.h"
#include "data/data_premium_limits.h"
#include "data/data_saved_sublist.h"
#include "data/data_thread.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "twidget/json.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"
#include "window/window_controller.h"
#include "window/window_separate_id.h"
#include "window/window_session_controller.h"
#include "apiwrap.h"

#include "styles/style_layers.h"

#include <QtCore/QUrl>

namespace Tgw {
namespace {

constexpr auto kCallInterval = crl::time(1000);
constexpr auto kMaxAlertLength = 4096;

[[nodiscard]] QString LogName(const std::string &method) {
	auto result = QString::fromStdString(method);
	for (auto &ch : result) {
		if (!ch.isPrint()) {
			ch = QChar('?');
		}
	}
	return result;
}

[[nodiscard]] bool IsWebUrl(const QUrl &url) {
	const auto scheme = url.scheme().toLower();
	return url.isValid()
		&& !url.isRelative()
		&& (scheme == u"http"_q || scheme == u"https"_q)
		&& !url.host().isEmpty();
}

[[nodiscard]] bool IsLocalUrl(const QString &url) {
	return url.startsWith(u"tg://"_q, Qt::CaseInsensitive)
		|| url.startsWith(u"internal:"_q, Qt::CaseInsensitive);
}

[[nodiscard]] QString BoundedAlertText(const QString &text) {
	if (text.size() <= kMaxAlertLength) {
		return text;
	}
	const auto cut = text[kMaxAlertLength - 1].isHighSurrogate()
		? (kMaxAlertLength - 1)
		: kMaxAlertLength;
	return text.left(cut) + QChar(0x2026);
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

void ConfirmLocalUrl(
		not_null<Window::SessionController*> controller,
		const QString &url,
		const ClickHandlerContext &context) {
	const auto open = crl::guard(controller, [=] {
		HiddenUrlClickHandler::Open(url, QVariant::fromValue(context));
	});
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		Ui::ConfirmBox(box, {
			.text = tr::lng_open_this_link(tr::now),
			.confirmed = [=](Fn<void()> close) {
				close();
				open();
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

Bridge::Bridge(not_null<HistoryItem*> item)
: _item(item) {
}

QString Bridge::call(
		const std::string &method,
		const std::string &argsJson) {
	const auto parsed = Parse(method, argsJson);
	if (!parsed) {
		return u"%1: refused, unknown method or malformed arguments"_q.arg(
			LogName(method));
	}
	switch (parsed->method) {
	case Method::Ready: return u"ready: nothing to do"_q;
	case Method::OpenLink: return openLink(parsed->argument);
	case Method::SendMessage: return sendMessage(parsed->argument);
	case Method::Clipboard: return copyToClipboard(parsed->argument);
	case Method::Alert: return showAlert(parsed->argument);
	}
	Unexpected("Method in Tgw::Bridge::call.");
}

std::optional<Bridge::Call> Bridge::Parse(
		const std::string &method,
		const std::string &argsJson) {
	constexpr auto kMethods = std::array{
		std::pair{ std::string_view("ready"), Method::Ready },
		std::pair{ std::string_view("openLink"), Method::OpenLink },
		std::pair{ std::string_view("sendMessage"), Method::SendMessage },
		std::pair{ std::string_view("clipboard"), Method::Clipboard },
		std::pair{ std::string_view("alert"), Method::Alert },
	};
	const auto known = ranges::find(
		kMethods,
		std::string_view(method),
		&std::pair<std::string_view, Method>::first);
	if (known == end(kMethods)) {
		return std::nullopt;
	}
	const auto args = twidget::Json::parse(argsJson);
	if (!args || !args->isArray()) {
		return std::nullopt;
	}
	const auto &list = args->arr();
	if (known->second == Method::Ready) {
		return list.empty()
			? std::make_optional(Call{ .method = Method::Ready })
			: std::nullopt;
	} else if (list.size() != 1 || !list.front().isString()) {
		return std::nullopt;
	}
	return Call{
		.method = known->second,
		.argument = QString::fromStdString(list.front().asString()),
	};
}

bool Bridge::passCooldown(Method method) {
	const auto now = crl::now();
	auto &last = _lastCalls[method];
	if (last > 0 && (now - last) < kCallInterval) {
		return false;
	}
	last = now;
	return true;
}

Window::SessionController *Bridge::findController() const {
	const auto thread = _item->maybeNotificationThread();
	return FindController(thread ? thread : _item->history().get());
}

QString Bridge::openLink(const QString &url) {
	const auto parsed = QUrl(url, QUrl::StrictMode);
	if (!IsWebUrl(parsed)) {
		return u"openLink: refused, not an absolute http(s) URL"_q;
	} else if (!passCooldown(Method::OpenLink)) {
		return u"openLink: refused, called again within a second"_q;
	}
	const auto controller = findController();
	if (!controller) {
		return u"openLink: refused, no window of this account is open"_q;
	}
	const auto encoded = QString::fromUtf8(parsed.toEncoded());
	const auto local = Core::TryConvertUrlToLocal(encoded);
	if (Core::InternalPassportOrOAuthLink(local)) {
		return u"openLink: refused, a passport or login link"_q;
	}
	const auto context = ClickHandlerContext{
		.itemId = _item->fullId(),
		.sessionWindow = base::make_weak(controller),
		.show = controller->uiShow(),
		.ignoreIv = true,
		.forceExternalUrlConfirmation = true,
		.peer = _item->history()->peer,
	};
	if (IsLocalUrl(local)) {
		ConfirmLocalUrl(controller, encoded, context);
		return u"openLink: asked to confirm a Telegram link"_q;
	}
	HiddenUrlClickHandler::Open(encoded, QVariant::fromValue(context));
	return u"openLink: asked to confirm the link"_q;
}

QString Bridge::sendMessage(const QString &text) {
	const auto session = &_item->history()->session();
	const auto limit = Data::PremiumLimits(session).messageLengthCurrent();
	const auto prepared = TextUtilities::PrepareForSending(text);
	if (prepared.isEmpty()) {
		return u"sendMessage: refused, the text is empty"_q;
	} else if (prepared.size() > limit) {
		return u"sendMessage: refused, the text is over %1 characters"_q.arg(
			limit);
	} else if (!passCooldown(Method::SendMessage)) {
		return u"sendMessage: refused, called again within a second"_q;
	}
	const auto thread = _item->maybeNotificationThread();
	if (!thread) {
		return u"sendMessage: refused, the topic is not loaded"_q;
	} else if (!Data::CanSendTexts(thread)) {
		return u"sendMessage: refused, texts can't be sent here"_q;
	}
	auto message = Api::MessageToSend(Api::SendAction(thread));
	message.textWithTags.text = text;
	if (const auto sublist = _item->savedSublist()) {
		if (sublist->parentChat()) {
			message.action.replyTo.monoforumPeerId
				= sublist->sublistPeer()->id;
		}
	}
	message.action.clearDraft = false;
	session->api().sendMessage(std::move(message));
	return u"sendMessage: passed for sending, delivery not confirmed"_q;
}

QString Bridge::copyToClipboard(const QString &text) {
	if (text.isEmpty()) {
		return u"clipboard: refused, the text is empty"_q;
	} else if (!passCooldown(Method::Clipboard)) {
		return u"clipboard: refused, called again within a second"_q;
	}
	TextUtilities::SetClipboardText(TextForMimeData::Simple(text));
	if (const auto controller = findController()) {
		controller->showToast(tr::lng_text_copied(tr::now));
		return u"clipboard: copied"_q;
	}
	return u"clipboard: copied, no window of this account for a toast"_q;
}

QString Bridge::showAlert(const QString &text) {
	if (text.trimmed().isEmpty()) {
		return u"alert: refused, the text is empty"_q;
	} else if (!passCooldown(Method::Alert)) {
		return u"alert: refused, called again within a second"_q;
	}
	const auto controller = findController();
	if (!controller) {
		return u"alert: refused, no window of this account is open"_q;
	} else if (_alert) {
		return u"alert: refused, the previous alert is still open"_q;
	}
	_alert = controller->show(Ui::MakeInformBox(BoundedAlertText(text)));
	return u"alert: shown"_q;
}

} // namespace Tgw
