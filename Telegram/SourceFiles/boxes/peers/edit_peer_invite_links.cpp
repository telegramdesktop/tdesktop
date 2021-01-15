/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_invite_links.h"

#include "data/data_changes.h"
#include "data/data_peer.h"
#include "main/main_session.h"
#include "api/api_invite_links.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/widgets/popup_menu.h"
#include "ui/controls/invite_link_label.h"
#include "ui/toast/toast.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "styles/style_info.h"

#include <QtGui/QGuiApplication>

void AddPermanentLinkBlock(
		not_null<Ui::VerticalLayout*> container,
		not_null<PeerData*> peer) {
	const auto computePermanentLink = [=] {
		const auto &links = peer->session().api().inviteLinks().links(
			peer).links;
		const auto link = links.empty() ? nullptr : &links.front();
		return (link && link->permanent && !link->revoked) ? link : nullptr;
	};
	auto value = peer->session().changes().peerFlagsValue(
		peer,
		Data::PeerUpdate::Flag::InviteLinks
	) | rpl::map([=] {
		const auto link = computePermanentLink();
		return link
			? std::make_tuple(link->link, link->usage)
			: std::make_tuple(QString(), 0);
	}) | rpl::start_spawning(container->lifetime());

	const auto copyLink = [=] {
		if (const auto link = computePermanentLink()) {
			QGuiApplication::clipboard()->setText(link->link);
			Ui::Toast::Show(tr::lng_group_invite_copied(tr::now));
		}
	};
	const auto shareLink = [=] {
		if (const auto link = computePermanentLink()) {
			QGuiApplication::clipboard()->setText(link->link);
			Ui::Toast::Show(tr::lng_group_invite_copied(tr::now));
		}
	};
	const auto revokeLink = [=] {
		if (const auto link = computePermanentLink()) {
			QGuiApplication::clipboard()->setText(link->link);
			Ui::Toast::Show(tr::lng_group_invite_copied(tr::now));
		}
	};

	auto link = rpl::duplicate(
		value
	) | rpl::map([=](QString link, int usage) {
		const auto prefix = qstr("https://");
		return link.startsWith(prefix) ? link.mid(prefix.size()) : link;
	});
	const auto createMenu = [=] {
		auto result = base::make_unique_q<Ui::PopupMenu>(container);
		result->addAction(
			tr::lng_group_invite_context_copy(tr::now),
			copyLink);
		result->addAction(
			tr::lng_group_invite_context_share(tr::now),
			shareLink);
		result->addAction(
			tr::lng_group_invite_context_revoke(tr::now),
			revokeLink);
		return result;
	};
	const auto label = container->lifetime().make_state<Ui::InviteLinkLabel>(
		container,
		std::move(link),
		createMenu);
	container->add(
		label->take(),
		st::inviteLinkFieldPadding);

	label->clicks(
	) | rpl::start_with_next(copyLink, label->lifetime());
}
