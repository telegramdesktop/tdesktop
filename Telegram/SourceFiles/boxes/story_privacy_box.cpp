/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/story_privacy_box.h"

#include "api/api_stories.h"
#include "apiwrap.h"
#include "boxes/peer_list_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/send_story_row.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/stories/media_stories_post.h"
#include "ui/layers/generic_box.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

namespace {

class StoryPeersController final : public ContactsBoxController {
public:
	StoryPeersController(
		not_null<Main::Session*> session,
		rpl::producer<QString> title,
		std::vector<not_null<PeerData*>> selected);

	void rowClicked(not_null<PeerListRow*> row) override;

protected:
	void prepareViewHook() override;

private:
	rpl::producer<QString> _title;
	std::vector<not_null<PeerData*>> _selected;

};

StoryPeersController::StoryPeersController(
	not_null<Main::Session*> session,
	rpl::producer<QString> title,
	std::vector<not_null<PeerData*>> selected)
: ContactsBoxController(session)
, _title(std::move(title))
, _selected(std::move(selected)) {
}

void StoryPeersController::rowClicked(not_null<PeerListRow*> row) {
	delegate()->peerListSetRowChecked(row, !row->checked());
}

void StoryPeersController::prepareViewHook() {
	delegate()->peerListSetTitle(std::move(_title));
	delegate()->peerListAddSelectedPeers(_selected);
}

} // namespace

std::vector<not_null<PeerData*>> CloseFriendsList(
		not_null<Main::Session*> session) {
	auto result = std::vector<not_null<PeerData*>>();
	for (const auto &row : session->data().contactsList()->all()) {
		if (const auto history = row->history()) {
			if (const auto user = history->peer->asUser()) {
				if (user->isCloseFriend()) {
					result.push_back(user);
				}
			}
		}
	}
	return result;
}

object_ptr<Ui::BoxContent> ChooseStoryPeersBox(
		StoryPeersChooserArgs &&args) {
	auto controller = std::make_unique<StoryPeersController>(
		args.session,
		std::move(args.title),
		std::move(args.selected));
	if (args.style) {
		controller->setStyleOverrides(
			&args.style->peerList,
			&args.style->multiSelect);
	}
	const auto done = std::move(args.done);
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->addButton(tr::lng_settings_save(), [=] {
			done(box->collectSelectedRows());
			box->closeBox();
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	};
	return Box<PeerListBox>(std::move(controller), std::move(initBox));
}

void ShowCloseFriendsBox(
		std::shared_ptr<ChatHelpers::Show> show,
		Fn<void()> done,
		const style::SendStoryBoxStyle *style) {
	const auto session = &show->session();
	const auto weak = base::make_weak(session);
	show->showBox(ChooseStoryPeersBox({
		.session = session,
		.title = tr::lng_story_privacy_close_friends_title(),
		.selected = CloseFriendsList(session),
		.style = style,
		.done = [=](std::vector<not_null<PeerData*>> peers) {
			if (const auto session = weak.get()) {
				session->api().stories().saveCloseFriends(
					std::move(peers),
					done);
			}
		},
	}));
}

void ShowStoryBlocklistBox(
		std::shared_ptr<ChatHelpers::Show> show,
		std::vector<not_null<PeerData*>> current,
		const style::SendStoryBoxStyle *style) {
	const auto session = &show->session();
	const auto weak = base::make_weak(session);
	show->showBox(ChooseStoryPeersBox({
		.session = session,
		.title = tr::lng_story_blocklist_title(),
		.selected = std::move(current),
		.style = style,
		.done = [=](std::vector<not_null<PeerData*>> peers) {
			if (const auto session = weak.get()) {
				session->api().stories().saveBlocklist(std::move(peers));
			}
		},
	}));
}

QString StoryPostAsStatus(not_null<PeerData*> peer) {
	if (peer->isUser()) {
		return tr::lng_story_post_as_personal(tr::now);
	}
	const auto channel = peer->isChannel() && !peer->isMegagroup();
	const auto count = peer->isChannel()
		? peer->asChannel()->membersCount()
		: peer->isChat()
		? peer->asChat()->count
		: 0;
	if (count > 0) {
		return channel
			? tr::lng_chat_status_subscribers(
				tr::now,
				lt_count_decimal,
				count)
			: tr::lng_chat_status_members(tr::now, lt_count_decimal, count);
	}
	return channel
		? tr::lng_channel_status(tr::now)
		: tr::lng_group_status(tr::now);
}

object_ptr<Ui::BoxContent> ChooseStoryPostAsBox(StoryPostAsArgs &&args) {
	const auto controller = args.controller;
	const auto peers = std::move(args.peers);
	const auto selected = args.selected;
	const auto done = std::move(args.done);
	return Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_story_post_as());
		box->setWidth(st::boxWideWidth);
		const auto content = box->verticalLayout();
		const auto session = &controller->session();
		for (const auto &peer : peers) {
			const auto row = content->add(object_ptr<SendStoryRow>(
				content,
				SendStoryRowContent{
					.peer = peer,
					.title = peer->name(),
					.subtitle = StoryPostAsStatus(peer),
					.radio = true,
					.divider = (peer != peers.back()),
				}));
			row->setChecked((peer == selected), anim::type::instant);
			row->setClickedCallback([=] {
				if (peer->isUser()) {
					done(peer);
					box->closeBox();
					return;
				}
				const auto weak = base::make_weak(box);
				session->api().stories().checkCanSend(peer, crl::guard(
					controller,
					[=](Api::CanSendStory result) {
						if (!result.allowed()) {
							Media::Stories::ShowCanSendStoryError(
								controller,
								peer,
								result);
						} else {
							done(peer);
						}
						if (const auto strong = weak.get()) {
							strong->closeBox();
						}
					}));
			});
		}
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	});
}
