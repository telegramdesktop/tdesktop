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
#include "data/data_session.h"
#include "data/data_histories.h"
#include "main/main_session.h"
#include "api/api_invite_links.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/abstract_button.h"
#include "ui/widgets/popup_menu.h"
#include "ui/controls/invite_link_label.h"
#include "ui/controls/invite_link_buttons.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "history/view/history_view_group_call_tracker.h" // GenerateUs...
#include "history/history_message.h" // GetErrorTextForSending.
#include "history/history.h"
#include "lang/lang_keys.h"
#include "boxes/confirm_box.h"
#include "boxes/peer_list_box.h"
#include "boxes/peer_list_controllers.h"
#include "apiwrap.h"
#include "mainwindow.h"
#include "boxes/share_box.h"
#include "window/window_session_controller.h"
#include "api/api_common.h"
#include "styles/style_info.h"

#include <QtGui/QGuiApplication>

namespace {

void ShareLinkBox(not_null<PeerData*> peer, const QString &link) {
	const auto session = &peer->session();
	const auto sending = std::make_shared<bool>();
	const auto box = std::make_shared<QPointer<ShareBox>>();

	auto copyCallback = [=] {
		QGuiApplication::clipboard()->setText(link);
		Ui::Toast::Show(tr::lng_group_invite_copied(tr::now));
	};
	auto submitCallback = [=](
			std::vector<not_null<PeerData*>> &&result,
			TextWithTags &&comment,
			Api::SendOptions options) {
		if (*sending || result.empty()) {
			return;
		}

		const auto error = [&] {
			for (const auto peer : result) {
				const auto error = GetErrorTextForSending(
					peer,
					{},
					comment);
				if (!error.isEmpty()) {
					return std::make_pair(error, peer);
				}
			}
			return std::make_pair(QString(), result.front());
		}();
		if (!error.first.isEmpty()) {
			auto text = TextWithEntities();
			if (result.size() > 1) {
				text.append(
					Ui::Text::Bold(error.second->name)
				).append("\n\n");
			}
			text.append(error.first);
			Ui::show(
				Box<InformBox>(text),
				Ui::LayerOption::KeepOther);
			return;
		}

		*sending = true;
		if (!comment.text.isEmpty()) {
			comment.text = link + "\n" + comment.text;
			const auto add = link.size() + 1;
			for (auto &tag : comment.tags) {
				tag.offset += add;
			}
		}
		const auto owner = &peer->owner();
		auto &api = peer->session().api();
		auto &histories = owner->histories();
		const auto requestType = Data::Histories::RequestType::Send;
		for (const auto peer : result) {
			const auto history = owner->history(peer);
			auto message = ApiWrap::MessageToSend(history);
			message.textWithTags = comment;
			message.action.options = options;
			message.action.clearDraft = false;
			api.sendMessage(std::move(message));
		}
		Ui::Toast::Show(tr::lng_share_done(tr::now));
		if (*box) {
			(*box)->closeBox();
		}
	};
	auto filterCallback = [](PeerData *peer) {
		return peer->canWrite();
	};
	*box = Ui::show(Box<ShareBox>(
		App::wnd()->sessionController(),
		std::move(copyCallback),
		std::move(submitCallback),
		std::move(filterCallback)));
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
