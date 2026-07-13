/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_community_requestable_list.h"

#include "boxes/peers/community_box.h"
#include "boxes/peer_list_box.h"
#include "data/data_channel.h"
#include "data/data_community.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "ui/qt_object_factory.h"
#include "window/window_session_controller.h"

#include "styles/style_dialogs.h"

namespace Dialogs {
namespace {

[[nodiscard]] std::unique_ptr<PeerListRow> MakeCommunityChatRow(
		not_null<PeerData*> peer) {
	auto row = std::make_unique<PeerListRow>(peer);
	const auto channel = peer->asChannel();
	if (channel && channel->membersCountKnown()) {
		row->setCustomStatus((channel->isBroadcast()
			? tr::lng_chat_status_subscribers
			: tr::lng_chat_status_members)(
				tr::now,
				lt_count_decimal,
				channel->membersCount()));
	}
	return row;
}

class ChatsController final : public PeerListController {
public:
	ChatsController(
		not_null<Main::Session*> session,
		rpl::producer<std::vector<not_null<PeerData*>>> chats,
		Fn<void(not_null<PeerData*>)> callback);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

	[[nodiscard]] rpl::producer<int> countValue() const {
		return _count.value();
	}

private:
	const not_null<Main::Session*> _session;
	rpl::producer<std::vector<not_null<PeerData*>>> _chats;
	Fn<void(not_null<PeerData*>)> _callback;
	rpl::variable<int> _count = 0;

};

ChatsController::ChatsController(
	not_null<Main::Session*> session,
	rpl::producer<std::vector<not_null<PeerData*>>> chats,
	Fn<void(not_null<PeerData*>)> callback)
: _session(session)
, _chats(std::move(chats))
, _callback(std::move(callback)) {
	setStyleOverrides(&st::communityRequestableList);
}

Main::Session &ChatsController::session() const {
	return *_session;
}

void ChatsController::prepare() {
	std::move(
		_chats
	) | rpl::on_next([=](const std::vector<not_null<PeerData*>> &list) {
		while (delegate()->peerListFullRowsCount() > 0) {
			delegate()->peerListRemoveRow(
				delegate()->peerListRowAt(
					delegate()->peerListFullRowsCount() - 1));
		}
		for (const auto &peer : list) {
			delegate()->peerListAppendRow(MakeCommunityChatRow(peer));
		}
		delegate()->peerListRefreshRows();
		_count = int(list.size());
	}, lifetime());
}

void ChatsController::rowClicked(not_null<PeerListRow*> row) {
	_callback(row->peer());
}

} // namespace

CommunityRequestableList::CommunityRequestableList(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<Data::CommunityInfo*> community)
: RpWidget(parent)
, _controller(controller) {
	auto requestable = community->linkedPeersValue(
	) | rpl::map([=] {
		auto result = std::vector<not_null<PeerData*>>();
		for (const auto &linked : community->linkedPeers()) {
			if (Data::CommunityChatJoined(linked.peer)) {
				continue;
			} else if (!linked.peer->isUser()
				&& Data::IsCommunityChatViewable(linked)) {
				continue;
			}
			result.push_back(linked.peer);
		}
		return result;
	}) | rpl::start_spawning(lifetime());

	const auto openChat = [=](not_null<PeerData*> peer) {
		OpenCommunityLinkedPeer(_controller, community, peer);
	};

	const auto delegate = lifetime().make_state<
		PeerListContentDelegateShow
	>(controller->uiShow());
	const auto chatsController = lifetime().make_state<ChatsController>(
		&controller->session(),
		rpl::duplicate(requestable),
		openChat);
	_content = Ui::CreateChild<PeerListContent>(this, chatsController);
	delegate->setContent(_content);
	chatsController->setDelegate(delegate);

	_count = chatsController->countValue();

	_content->heightValue(
	) | rpl::on_next([=] {
		resizeToWidth(width());
	}, lifetime());

	hide();
}

CommunityRequestableList::~CommunityRequestableList() = default;

int CommunityRequestableList::resizeGetHeight(int newWidth) {
	_content->resizeToWidth(newWidth);
	_content->moveToLeft(0, 0);
	return _content->height();
}

rpl::producer<int> CommunityRequestableList::countValue() const {
	return _count.value();
}

} // namespace Dialogs
