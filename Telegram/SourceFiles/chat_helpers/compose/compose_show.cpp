/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/compose/compose_show.h"

#include "core/application.h"
#include "main/main_session.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace ChatHelpers {

rpl::producer<bool> Show::adjustShadowLeft() const {
	return rpl::single(false);
}

ResolveWindow ResolveWindowDefault() {
	return [](not_null<Main::Session*> session, WindowUsage usage)
	-> Window::SessionController* {
		const auto check = [&](Window::Controller *window) {
			if (const auto controller = window->sessionController()) {
				if (&controller->session() == session) {
					return controller;
				}
			}
			return (Window::SessionController*)nullptr;
		};
		auto &app = Core::App();
		if (const auto a = check(app.activeWindow())) {
			return a;
		} else if (const auto b = check(app.activePrimaryWindow())) {
			return b;
		} else if (const auto c = check(app.windowFor(&session->account()))) {
			return c;
		} else if (const auto d = check(
			app.ensureSeparateWindowForAccount(
				&session->account()))) {
			return d;
		}
		return nullptr;
	};
}

Window::SessionController *Show::resolveWindow(WindowUsage usage) const {
	return ResolveWindowDefault()(&session(), usage);
}

} // namespace ChatHelpers
