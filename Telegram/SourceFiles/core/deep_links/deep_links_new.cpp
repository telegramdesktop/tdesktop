/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/deep_links/deep_links_router.h"

#include "apiwrap.h"
#include "boxes/peers/create_managed_bot_box.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/toast/toast.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"

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

Result ShowNewBot(const Context &ctx) {
	if (!ctx.controller) {
		return Result::NeedsAuth;
	}
	const auto manager = ctx.params.value(u"manager"_q);
	const auto username = ctx.params.value(u"username"_q);
	const auto title = ctx.params.value(u"name"_q);
	if (manager.isEmpty()) {
		return Result::Handled;
	}
	const auto session = &ctx.controller->session();
	const auto weak = base::make_weak(ctx.controller);
	session->api().request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(manager),
		MTP_string()
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		result.match([&](const MTPDcontacts_resolvedPeer &data) {
			strong->session().data().processUsers(data.vusers());
			strong->session().data().processChats(data.vchats());
			const auto peerId = peerFromMTP(data.vpeer());
			if (const auto managerBot = strong->session().data().userLoaded(
					peerToUser(peerId))) {
				if (!managerBot->isBot()) {
					return;
				}
				if (!managerBot->botInfo->canManageBots) {
					strong->showToast(
						tr::lng_create_bot_no_manage(
							tr::now,
							lt_bot,
							u"@"_q + managerBot->username()));
					return;
				}
				ShowCreateManagedBotBox({
					.show = strong->uiShow(),
					.manager = managerBot,
					.suggestedName = title,
					.suggestedUsername = username,
					.viaDeeplink = true,
					.done = [weak, managerBot](not_null<UserData*> createdBot) {
						if (const auto strong = weak.get()) {
							strong->showPeerHistory(createdBot);
							strong->showToast({
								.title = tr::lng_managed_bot_created_title(
									tr::now,
									lt_name,
									createdBot->name()),
								.text = { tr::lng_managed_bot_created_text(
									tr::now,
									lt_parent_name,
									managerBot->name()) },
								.icon = &st::toastCheckIcon,
							});
						}
					},
				});
			}
		});
	}).send();
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

	router.add(u"newbot"_q, {
		.path = QString(),
		.action = CodeBlock{ ShowNewBot },
	});
}

} // namespace Core::DeepLinks
