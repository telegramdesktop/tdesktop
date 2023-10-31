/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/boosts/giveaway/giveaway_list_controllers.h"

#include "apiwrap.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "lang/lang_keys.h"

namespace Giveaway {

AwardMembersListController::AwardMembersListController(
	not_null<Window::SessionNavigation*> navigation,
	not_null<PeerData*> peer)
: ParticipantsBoxController(navigation, peer, ParticipantsRole::Members) {
}

void AwardMembersListController::rowClicked(not_null<PeerListRow*> row) {
	delegate()->peerListSetRowChecked(row, !row->checked());
}

std::unique_ptr<PeerListRow> AwardMembersListController::createRow(
		not_null<PeerData*> participant) const {
	const auto user = participant->asUser();
	if (!user || user->isInaccessible() || user->isBot() || user->isSelf()) {
		return nullptr;
	}
	return std::make_unique<PeerListRow>(participant);
}

base::unique_qptr<Ui::PopupMenu> AwardMembersListController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	return nullptr;
}

MyChannelsListController::MyChannelsListController(
	not_null<PeerData*> peer,
	std::shared_ptr<Ui::Show> show,
	std::vector<not_null<PeerData*>> selected)
: PeerListController(
	std::make_unique<PeerListGlobalSearchController>(&peer->session()))
, _peer(peer)
, _show(show)
, _selected(std::move(selected)) {
}

std::unique_ptr<PeerListRow> MyChannelsListController::createSearchRow(
		not_null<PeerData*> peer) {
	if (const auto channel = peer->asChannel()) {
		return createRow(channel);
	}
	return nullptr;
}

std::unique_ptr<PeerListRow> MyChannelsListController::createRestoredRow(
		not_null<PeerData*> peer) {
	if (const auto channel = peer->asChannel()) {
		return createRow(channel);
	}
	return nullptr;
}

void MyChannelsListController::rowClicked(not_null<PeerListRow*> row) {
	const auto channel = row->peer()->asChannel();
	const auto checked = !row->checked();
	if (checked && channel && channel->username().isEmpty()) {
		_show->showBox(Box(Ui::ConfirmBox, Ui::ConfirmBoxArgs{
			.text = tr::lng_giveaway_channels_confirm_about(),
			.confirmed = [=](Fn<void()> close) {
				delegate()->peerListSetRowChecked(row, checked);
				close();
			},
			.confirmText = tr::lng_filters_recommended_add(),
			.title = tr::lng_giveaway_channels_confirm_title(),
		}));
	} else {
		delegate()->peerListSetRowChecked(row, checked);
	}
}

Main::Session &MyChannelsListController::session() const {
	return _peer->session();
}

void MyChannelsListController::prepare() {
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	const auto api = _apiLifetime.make_state<MTP::Sender>(
		&session().api().instance());
	api->request(
		MTPstories_GetChatsToSend()
	).done([=](const MTPmessages_Chats &result) {
		_apiLifetime.destroy();
		const auto &chats = result.match([](const auto &data) {
			return data.vchats().v;
		});
		auto &owner = session().data();
		for (const auto &chat : chats) {
			if (const auto peer = owner.processChat(chat)) {
				if (!peer->isChannel() || (peer == _peer)) {
					continue;
				}
				if (!delegate()->peerListFindRow(peer->id.value)) {
					if (const auto channel = peer->asChannel()) {
						auto row = createRow(channel);
						const auto raw = row.get();
						delegate()->peerListAppendRow(std::move(row));
						if (ranges::contains(_selected, peer)) {
							delegate()->peerListSetRowChecked(raw, true);
							_selected.erase(
								ranges::remove(_selected, peer),
								end(_selected));
						}
					}
				}
			}
		}
		for (const auto &selected : _selected) {
			if (const auto channel = selected->asChannel()) {
				auto row = createRow(channel);
				const auto raw = row.get();
				delegate()->peerListAppendRow(std::move(row));
				delegate()->peerListSetRowChecked(raw, true);
			}
		}
		delegate()->peerListRefreshRows();
		_selected.clear();
	}).send();
}

std::unique_ptr<PeerListRow> MyChannelsListController::createRow(
		not_null<ChannelData*> channel) const {
	if (channel->isMegagroup()) {
		return nullptr;
	}
	auto row = std::make_unique<PeerListRow>(channel);
	row->setCustomStatus(tr::lng_chat_status_subscribers(
		tr::now,
		lt_count,
		channel->membersCount()));
	return row;
}

} // namespace Giveaway
