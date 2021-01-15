/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_invite_links.h"

#include "data/data_changes.h"
#include "data/data_user.h"
#include "data/data_drafts.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "api/api_invite_links.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/abstract_button.h"
#include "ui/widgets/popup_menu.h"
#include "ui/controls/invite_link_label.h"
#include "ui/controls/invite_link_buttons.h"
#include "ui/toast/toast.h"
#include "history/view/history_view_group_call_tracker.h" // GenerateUs...
#include "history/history.h"
#include "lang/lang_keys.h"
#include "boxes/confirm_box.h"
#include "boxes/peer_list_box.h"
#include "boxes/peer_list_controllers.h"
#include "apiwrap.h"
#include "api/api_common.h"
#include "styles/style_info.h"

#include <QtGui/QGuiApplication>

namespace {

void ShareLinkBox(not_null<PeerData*> peer, const QString &link) {
	const auto weak = std::make_shared<QPointer<PeerListBox>>();
	auto callback = [
		weak,
		link
	](not_null<PeerData*> peer) mutable {
		const auto history = peer->owner().history(peer);
		if (peer->isSelf()) {
			const auto api = &peer->session().api();
			auto message = Api::MessageToSend(history);
			message.action.clearDraft = false;
			message.action.generateLocal = false;
			message.textWithTags = { link };
			api->sendMessage(std::move(message));
			Ui::Toast::Show(tr::lng_share_done(tr::now));
		} else {
			auto textWithTags = TextWithTags{
				link + '\n',
				TextWithTags::Tags()
			};
			auto cursor = MessageCursor{
				link.size() + 1,
				link.size() + 1,
				QFIXED_MAX
			};
			history->setLocalDraft(
				std::make_unique<Data::Draft>(textWithTags, 0, cursor, false));
			history->clearLocalEditDraft();
			history->session().changes().historyUpdated(
				history,
				Data::HistoryUpdate::Flag::LocalDraftSet);
		}
		if (const auto strong = *weak) {
			strong->closeBox();
		}
	};
	auto initBox = [](not_null<PeerListBox*> box) {
		box->addButton(tr::lng_cancel(), [box] {
			box->closeBox();
		});
	};
	*weak = Ui::show(Box<PeerListBox>(
		std::make_unique<ChooseRecipientBoxController>(
			&peer->session(),
			std::move(callback)),
		std::move(initBox)), Ui::LayerOption::KeepOther);
}

} // namespace

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
	}) | rpl::distinct_until_changed(
	) | rpl::start_spawning(container->lifetime());

	const auto weak = Ui::MakeWeak(container);
	const auto copyLink = crl::guard(weak, [=] {
		if (const auto link = computePermanentLink()) {
			QGuiApplication::clipboard()->setText(link->link);
			Ui::Toast::Show(tr::lng_group_invite_copied(tr::now));
		}
	});
	const auto shareLink = crl::guard(weak, [=] {
		if (const auto link = computePermanentLink()) {
			ShareLinkBox(peer, link->link);
		}
	});
	const auto revokeLink = crl::guard(weak, [=] {
		const auto box = std::make_shared<QPointer<ConfirmBox>>();
		const auto done = crl::guard(weak, [=] {
			if (const auto link = computePermanentLink()) {
				const auto close = [=](auto&&) {
					if (*box) {
						(*box)->closeBox();
					}
				};
				peer->session().api().inviteLinks().revoke(
					peer,
					link->link,
					close);
			}
		});
		*box = Ui::show(
			Box<ConfirmBox>(tr::lng_group_invite_about_new(tr::now), done),
			Ui::LayerOption::KeepOther);
	});

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

	AddCopyShareLinkButtons(
		container,
		copyLink,
		shareLink);

	struct JoinedState {
		QImage cachedUserpics;
		std::vector<HistoryView::UserpicInRow> list;
		int count = 0;
		bool allUserpicsLoaded = false;
		rpl::variable<Ui::JoinedCountContent> content;
		rpl::lifetime lifetime;
	};
	const auto state = container->lifetime().make_state<JoinedState>();
	const auto push = [=] {
		HistoryView::GenerateUserpicsInRow(
			state->cachedUserpics,
			state->list,
			st::inviteLinkUserpics,
			0);
		state->allUserpicsLoaded = ranges::all_of(
			state->list,
			[](const HistoryView::UserpicInRow &element) {
				return !element.peer->hasUserpic() || element.view->image();
			});
		state->content = Ui::JoinedCountContent{
			.count = state->count,
			.userpics = state->cachedUserpics
		};
	};
	std::move(
		value
	) | rpl::map([=](QString link, int usage) {
		return peer->session().api().inviteLinks().joinedFirstSliceValue(
			peer,
			link,
			usage);
	}) | rpl::flatten_latest(
	) | rpl::start_with_next([=](const Api::JoinedByLinkSlice &slice) {
		auto list = std::vector<HistoryView::UserpicInRow>();
		list.reserve(slice.users.size());
		for (const auto &item : slice.users) {
			const auto i = ranges::find(
				state->list,
				item.user,
				&HistoryView::UserpicInRow::peer);
			if (i != end(state->list)) {
				list.push_back(std::move(*i));
			} else {
				list.push_back({ item.user });
			}
		}
		state->count = slice.count;
		state->list = std::move(list);
		push();
	}, state->lifetime);

	peer->session().downloaderTaskFinished(
	) | rpl::filter([=] {
		return !state->allUserpicsLoaded;
	}) | rpl::start_with_next([=] {
		auto pushing = false;
		state->allUserpicsLoaded = true;
		for (const auto &element : state->list) {
			if (!element.peer->hasUserpic()) {
				continue;
			} else if (element.peer->userpicUniqueKey(element.view)
				!= element.uniqueKey) {
				pushing = true;
			} else if (!element.view->image()) {
				state->allUserpicsLoaded = false;
			}
		}
		if (pushing) {
			push();
		}
	}, state->lifetime);

	Ui::AddJoinedCountButton(
		container,
		state->content.value(),
		st::inviteLinkJoinedRowPadding
	)->setClickedCallback([=] {
	});
}
