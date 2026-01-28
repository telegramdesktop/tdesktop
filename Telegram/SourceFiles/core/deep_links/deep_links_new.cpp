/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/deep_links/deep_links_router.h"

#include "window/window_session_controller.h"

namespace Core::DeepLinks {
namespace {

Result ShowNewGroup(const Context &ctx) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}
	ctx.controller->showNewGroup();
	return Result::Handled;
}

Result ShowNewChannel(const Context &ctx) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}
	ctx.controller->showNewChannel();
	return Result::Handled;
}

Result ShowAddContact(const Context &ctx) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}
	ctx.controller->showAddContact();
	return Result::Handled;
}

} // namespace

void RegisterNewHandlers(Router &router) {
	router.add(u"new"_q, {
		.path = u"group"_q,
		.action = CodeBlock{ ShowNewGroup },
	});

	router.add(u"new"_q, {
		.path = u"channel"_q,
		.action = CodeBlock{ ShowNewChannel },
	});

	router.add(u"new"_q, {
		.path = u"contact"_q,
		.action = CodeBlock{ ShowAddContact },
	});
}

} // namespace Core::DeepLinks
