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
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"
#include "window/window_controller.h"
#include "window/window_separate_id.h"
#include "window/window_session_controller.h"

#include "styles/style_layers.h"

namespace Tgw {
namespace {

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

Bridge::Bridge(not_null<HistoryItem*> item)
: _item(item) {
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
	const auto context = ClickHandlerContext{
		.itemId = _item->fullId(),
		.sessionWindow = base::make_weak(controller),
		.show = controller->uiShow(),
		.ignoreIv = true,
		.forceExternalUrlConfirmation = true,
		.peer = _item->history()->peer,
	};
	HiddenUrlClickHandler::Open(url, QVariant::fromValue(context));
	return u"openTelegramLink: passed to the app's t.me handling"_q;
}

} // namespace Tgw
