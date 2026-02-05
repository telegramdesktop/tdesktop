/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/deep_links/deep_links_router.h"

#include "core/deep_links/deep_links_chats.h"
#include "core/deep_links/deep_links_contacts.h"
#include "core/deep_links/deep_links_new.h"
#include "core/deep_links/deep_links_settings.h"
#include "core/application.h"
#include "main/main_session.h"
#include "ui/toast/toast.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace Core::DeepLinks {
namespace {

Context ParseCommand(
		Window::SessionController *controller,
		const QString &command) {
	auto result = Context{ .controller = controller };

	auto path = command;
	auto queryStart = path.indexOf('?');
	if (queryStart >= 0) {
		const auto query = path.mid(queryStart + 1);
		path = path.left(queryStart);
		for (const auto &pair : query.split('&')) {
			const auto eq = pair.indexOf('=');
			if (eq > 0) {
				result.params[pair.left(eq).toLower()] = pair.mid(eq + 1);
			} else if (!pair.isEmpty()) {
				result.params[pair.toLower()] = QString();
			}
		}
	}

	path = path.trimmed();
	while (path.startsWith('/')) {
		path = path.mid(1);
	}
	while (path.endsWith('/')) {
		path.chop(1);
	}

	const auto slash = path.indexOf('/');
	if (slash > 0) {
		result.section = path.left(slash).toLower();
		result.path = path.mid(slash + 1);
	} else {
		result.section = path.toLower();
	}

	return result;
}

} // namespace

Router &Router::Instance() {
	static auto instance = Router();
	return instance;
}

Router::Router() {
	RegisterSettingsHandlers(*this);
	RegisterContactsHandlers(*this);
	RegisterChatsHandlers(*this);
	RegisterNewHandlers(*this);
}

void Router::add(const QString &section, Entry entry) {
	_handlers[section].push_back(std::move(entry));
}

bool Router::tryHandle(
		Window::SessionController *controller,
		const QString &command) {
	const auto ctx = ParseCommand(controller, command);
	const auto [result, skipActivation] = dispatch(ctx);

	switch (result) {
	case Result::Handled:
		if (controller && !skipActivation) {
			controller->window().activate();
		}
		return true;
	case Result::NeedsAuth:
		return false;
	case Result::Unsupported:
		showUnsupportedMessage(controller, command);
		return true;
	case Result::NotFound:
		return false;
	}
	return false;
}

Router::DispatchResult Router::dispatch(const Context &ctx) {
	if (ctx.section.isEmpty()) {
		return { Result::NotFound };
	}
	return handleSection(ctx.section, ctx);
}

Router::DispatchResult Router::handleSection(
		const QString &section,
		const Context &ctx) {
	const auto it = _handlers.find(section);
	if (it == _handlers.end()) {
		return { Result::NotFound };
	}

	const auto &entries = it->second;
	const auto path = ctx.path.toLower();

	for (const auto &entry : entries) {
		if (entry.path == path || (entry.path.isEmpty() && path.isEmpty())) {
			if (entry.requiresAuth && !ctx.controller) {
				return { Result::NeedsAuth };
			}
			return {
				executeAction(entry.action, ctx),
				entry.skipActivation,
			};
		}
	}

	for (const auto &entry : entries) {
		if (!entry.path.isEmpty() && path.startsWith(entry.path + '/')) {
			if (entry.requiresAuth && !ctx.controller) {
				return { Result::NeedsAuth };
			}
			return {
				executeAction(entry.action, ctx),
				entry.skipActivation,
			};
		}
	}

	return { Result::Unsupported };
}

Result Router::executeAction(const Action &action, const Context &ctx) {
	return v::match(action, [&](const SettingsSection &s) {
		if (!ctx.controller) {
			return Result::NeedsAuth;
		}
		ctx.controller->showSettings(s.sectionId);
		return Result::Handled;
	}, [&](const SettingsControl &s) {
		if (!ctx.controller) {
			return Result::NeedsAuth;
		}
		if (!s.controlId.isEmpty()) {
			ctx.controller->setHighlightControlId(s.controlId);
		}
		ctx.controller->showSettings(s.sectionId);
		return Result::Handled;
	}, [&](const CodeBlock &c) {
		return c.handler(ctx);
	}, [&](const AliasTo &a) {
		auto aliasCtx = ctx;
		aliasCtx.section = a.section;
		aliasCtx.path = a.path;
		return handleSection(a.section, aliasCtx).result;
	});
}

void Router::showUnsupportedMessage(
		Window::SessionController *controller,
		const QString &url) {
	const auto text = u"This link is not supported on Desktop."_q;
	if (controller) {
		controller->showToast(text);
	} else if (const auto window = Core::App().activeWindow()) {
		window->showToast(text);
	}
}

} // namespace Core::DeepLinks
