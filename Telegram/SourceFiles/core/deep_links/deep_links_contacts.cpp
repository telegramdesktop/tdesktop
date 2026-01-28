/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/deep_links/deep_links_router.h"

#include "boxes/peer_list_controllers.h"
#include "window/window_session_controller.h"

namespace Core::DeepLinks {
namespace {

Result ShowContacts(const Context &ctx) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}
	ctx.controller->show(PrepareContactsBox(ctx.controller));
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

void RegisterContactsHandlers(Router &router) {
	router.add(u"contacts"_q, {
		.path = QString(),
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowContacts(ctx);
		}},
	});

	router.add(u"contacts"_q, {
		.path = u"search"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			return ShowContacts(ctx);
		}},
	});

	router.add(u"contacts"_q, {
		.path = u"sort"_q,
		.action = CodeBlock{ [](const Context &ctx) {
			if (!ctx.controller) {
				return Result::NeedsAuth;
			}
			ctx.controller->setHighlightControlId(u"contacts/sort"_q);
			ctx.controller->show(PrepareContactsBox(ctx.controller));
			return Result::Handled;
		}},
	});

	router.add(u"contacts"_q, {
		.path = u"new"_q,
		.action = CodeBlock{ ShowAddContact },
	});
}

} // namespace Core::DeepLinks
