/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/add_participants_box.h"

#include "boxes/peers/edit_participant_box.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "history/history.h"
#include "dialogs/dialogs_indexed_list.h"
#include "core/tl_help.h"
#include "base/overload.h"
#include "auth_session.h"
#include "mainwidget.h"
#include "apiwrap.h"
#include "observer_peer.h"

namespace {

constexpr auto kParticipantsFirstPageCount = 16;
constexpr auto kParticipantsPerPage = 200;

base::flat_set<not_null<UserData*>> GetAlreadyInFromPeer(PeerData *peer) {
	if (!peer) {
		return {};
	}
	if (const auto chat = peer->asChat()) {
		const auto participants = (
			chat->participants
		) | ranges::view::transform([](auto &&pair) -> not_null<UserData*> {
			return pair.first;
		});
		return { participants.begin(), participants.end() };
	} else if (const auto channel = peer->asChannel()) {
		if (channel->isMegagroup()) {
			const auto &participants = channel->mgInfo->lastParticipants;
			return { participants.cbegin(), participants.cend() };
		}
	}
	return {};
}

bool InviteSelectedUsers(
		not_null<PeerListBox*> box,
		not_null<PeerData*> chat) {
	const auto rows = box->peerListCollectSelectedRows();
	const auto users = ranges::view::all(
		rows
	) | ranges::view::transform([](not_null<PeerData*> peer) {
		Expects(peer->isUser());
		Expects(!peer->isSelf());

		return not_null<UserData*>(peer->asUser());
	}) | ranges::to_vector;
	if (users.empty()) {
		return false;
	}
	Auth().api().addChatParticipants(chat, users);
	return true;
}

} // namespace

AddParticipantsBoxController::AddParticipantsBoxController(PeerData *peer)
: ContactsBoxController(std::make_unique<PeerListGlobalSearchController>())
, _peer(peer)
, _alreadyIn(GetAlreadyInFromPeer(peer)) {
}

AddParticipantsBoxController::AddParticipantsBoxController(
	not_null<ChannelData*> channel,
	base::flat_set<not_null<UserData*>> &&alreadyIn)
: ContactsBoxController(std::make_unique<PeerListGlobalSearchController>())
, _peer(channel)
, _alreadyIn(std::move(alreadyIn)) {
}

void AddParticipantsBoxController::rowClicked(not_null<PeerListRow*> row) {
	auto count = fullCount();
	auto limit = (_peer && _peer->isMegagroup()) ? Global::MegagroupSizeMax() : Global::ChatSizeMax();
	if (count < limit || row->checked()) {
		delegate()->peerListSetRowChecked(row, !row->checked());
		updateTitle();
	} else if (auto channel = _peer ? _peer->asChannel() : nullptr) {
		if (!_peer->isMegagroup()) {
			Ui::show(
				Box<MaxInviteBox>(_peer->asChannel()),
				LayerOption::KeepOther);
		}
	} else if (count >= Global::ChatSizeMax() && count < Global::MegagroupSizeMax()) {
		Ui::show(
			Box<InformBox>(lng_profile_add_more_after_upgrade(lt_count, Global::MegagroupSizeMax())),
			LayerOption::KeepOther);
	}
}

void AddParticipantsBoxController::itemDeselectedHook(not_null<PeerData*> peer) {
	updateTitle();
}

void AddParticipantsBoxController::prepareViewHook() {
	updateTitle();
}

int AddParticipantsBoxController::alreadyInCount() const {
	if (!_peer) {
		return 1; // self
	}
	if (auto chat = _peer->asChat()) {
		return qMax(chat->count, 1);
	} else if (auto channel = _peer->asChannel()) {
		return qMax(channel->membersCount(), int(_alreadyIn.size()));
	}
	Unexpected("User in AddParticipantsBoxController::alreadyInCount");
}

bool AddParticipantsBoxController::isAlreadyIn(not_null<UserData*> user) const {
	if (!_peer) {
		return false;
	}
	if (auto chat = _peer->asChat()) {
		return chat->participants.contains(user);
	} else if (auto channel = _peer->asChannel()) {
		return _alreadyIn.contains(user)
			|| (channel->isMegagroup() && base::contains(channel->mgInfo->lastParticipants, user));
	}
	Unexpected("User in AddParticipantsBoxController::isAlreadyIn");
}

int AddParticipantsBoxController::fullCount() const {
	return alreadyInCount() + delegate()->peerListSelectedRowsCount();
}

std::unique_ptr<PeerListRow> AddParticipantsBoxController::createRow(
		not_null<UserData*> user) {
	if (user->isSelf()) {
		return nullptr;
	}
	auto result = std::make_unique<PeerListRow>(user);
	if (isAlreadyIn(user)) {
		result->setDisabledState(PeerListRow::State::DisabledChecked);
	}
	return result;
}

void AddParticipantsBoxController::updateTitle() {
	auto additional = (_peer && _peer->isChannel() && !_peer->isMegagroup())
		? QString() :
		QString("%1 / %2").arg(fullCount()).arg(Global::MegagroupSizeMax());
	delegate()->peerListSetTitle(langFactory(lng_profile_add_participant));
	delegate()->peerListSetAdditionalTitle([additional] { return additional; });
}

void AddParticipantsBoxController::Start(not_null<ChatData*> chat) {
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->addButton(langFactory(lng_participant_invite), [=] {
			if (InviteSelectedUsers(box, chat)) {
				Ui::showPeerHistory(chat, ShowAtTheEndMsgId);
			}
		});
		box->addButton(langFactory(lng_cancel), [box] { box->closeBox(); });
	};
	Ui::show(Box<PeerListBox>(std::make_unique<AddParticipantsBoxController>(chat), std::move(initBox)));
}

void AddParticipantsBoxController::Start(
		not_null<ChannelData*> channel,
		base::flat_set<not_null<UserData*>> &&alreadyIn,
		bool justCreated) {
	auto initBox = [channel, justCreated](not_null<PeerListBox*> box) {
		auto subscription = std::make_shared<rpl::lifetime>();
		box->addButton(langFactory(lng_participant_invite), [=, copy = subscription] {
			if (InviteSelectedUsers(box, channel)) {
				if (channel->isMegagroup()) {
					Ui::showPeerHistory(channel, ShowAtTheEndMsgId);
				} else {
					box->closeBox();
				}
			}
		});
		box->addButton(langFactory(justCreated ? lng_create_group_skip : lng_cancel), [box] { box->closeBox(); });
		if (justCreated) {
			box->boxClosing() | rpl::start_with_next([=] {
				Ui::showPeerHistory(channel, ShowAtTheEndMsgId);
			}, *subscription);
		}
	};
	Ui::show(Box<PeerListBox>(std::make_unique<AddParticipantsBoxController>(channel, std::move(alreadyIn)), std::move(initBox)));
}

void AddParticipantsBoxController::Start(
		not_null<ChannelData*> channel,
		base::flat_set<not_null<UserData*>> &&alreadyIn) {
	Start(channel, std::move(alreadyIn), false);
}

void AddParticipantsBoxController::Start(not_null<ChannelData*> channel) {
	Start(channel, {}, true);
}

AddSpecialBoxController::AddSpecialBoxController(
	not_null<PeerData*> peer,
	Role role,
	AdminDoneCallback adminDoneCallback,
	BannedDoneCallback bannedDoneCallback)
: PeerListController(std::make_unique<AddSpecialBoxSearchController>(
	peer,
	&_additional))
, _peer(peer)
, _role(role)
, _adminDoneCallback(std::move(adminDoneCallback))
, _bannedDoneCallback(std::move(bannedDoneCallback)) {
	_additional.fillCreator(_peer);
}

std::unique_ptr<PeerListRow> AddSpecialBoxController::createSearchRow(not_null<PeerData*> peer) {
	if (peer->isSelf()) {
		return nullptr;
	}
	if (const auto user = peer->asUser()) {
		return createRow(user);
	}
	return nullptr;
}

void AddSpecialBoxController::prepare() {
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	const auto title = [&] {
		switch (_role) {
		case Role::Admins: return langFactory(lng_channel_add_admin);
		case Role::Restricted: return langFactory(lng_channel_add_restricted);
		case Role::Kicked: return langFactory(lng_channel_add_banned);
		}
		Unexpected("Role in AddSpecialBoxController::prepare()");
	}();
	delegate()->peerListSetTitle(title);
	setDescriptionText(lang(lng_contacts_loading));
	setSearchNoResultsText(lang(lng_blocked_list_not_found));

	if (const auto chat = _peer->asChat()) {
		prepareChatRows(chat);
	} else {
		loadMoreRows();
	}
	delegate()->peerListRefreshRows();
}

void AddSpecialBoxController::prepareChatRows(not_null<ChatData*> chat) {
	_onlineSorter = std::make_unique<ParticipantsOnlineSorter>(
		chat,
		delegate());

	rebuildChatRows(chat);
	if (!delegate()->peerListFullRowsCount()) {
		chat->updateFullForced();
	}

	using UpdateFlag = Notify::PeerUpdate::Flag;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(
		UpdateFlag::MembersChanged,
		[=](const Notify::PeerUpdate &update) {
			if (update.flags & UpdateFlag::MembersChanged) {
				if (update.peer == chat) {
					rebuildChatRows(chat);
				}
			}
		}));
}

void AddSpecialBoxController::rebuildChatRows(not_null<ChatData*> chat) {
	if (chat->participants.empty()) {
		// We get such updates often
		// (when participants list was invalidated).
		//while (delegate()->peerListFullRowsCount() > 0) {
		//	delegate()->peerListRemoveRow(
		//		delegate()->peerListRowAt(0));
		//}
		return;
	}

	auto &participants = chat->participants;
	auto count = delegate()->peerListFullRowsCount();
	for (auto i = 0; i != count;) {
		auto row = delegate()->peerListRowAt(i);
		auto user = row->peer()->asUser();
		if (participants.contains(user)) {
			++i;
		} else {
			delegate()->peerListRemoveRow(row);
			--count;
		}
	}
	for (const auto [user, v] : participants) {
		if (auto row = createRow(user)) {
			delegate()->peerListAppendRow(std::move(row));
		}
	}
	_onlineSorter->sort();

	delegate()->peerListRefreshRows();
}

void AddSpecialBoxController::loadMoreRows() {
	if (searchController() && searchController()->loadMoreRows()) {
		return;
	} else if (!_peer->isChannel() || _loadRequestId || _allLoaded) {
		return;
	}

	// First query is small and fast, next loads a lot of rows.
	const auto perPage = (_offset > 0)
		? kParticipantsPerPage
		: kParticipantsFirstPageCount;
	const auto participantsHash = 0;
	const auto channel = _peer->asChannel();

	_loadRequestId = request(MTPchannels_GetParticipants(
		channel->inputChannel,
		MTP_channelParticipantsRecent(),
		MTP_int(_offset),
		MTP_int(perPage),
		MTP_int(participantsHash)
	)).done([=](const MTPchannels_ChannelParticipants &result) {
		_loadRequestId = 0;

		Auth().api().parseChannelParticipants(channel, result, [&](
				int availableCount,
				const QVector<MTPChannelParticipant> &list) {
			for (auto &participant : list) {
				HandleParticipant(
					participant,
					&_additional,
					[&](auto user) { appendRow(user); });
			}
			if (auto size = list.size()) {
				_offset += size;
			} else {
				// To be sure - wait for a whole empty result list.
				_allLoaded = true;
			}
		});

		if (delegate()->peerListFullRowsCount() > 0) {
			setDescriptionText(QString());
		} else if (_allLoaded) {
			setDescriptionText(lang(lng_blocked_list_not_found));
		}
		delegate()->peerListRefreshRows();
	}).fail([this](const RPCError &error) {
		_loadRequestId = 0;
	}).send();
}

void AddSpecialBoxController::rowClicked(not_null<PeerListRow*> row) {
	auto user = row->peer()->asUser();
	switch (_role) {
	case Role::Admins: return showAdmin(user);
	case Role::Restricted: return showRestricted(user);
	case Role::Kicked: return kickUser(user);
	}
	Unexpected("Role in AddSpecialBoxController::rowClicked()");
}

template <typename Callback>
bool AddSpecialBoxController::checkInfoLoaded(
		not_null<UserData*> user,
		Callback callback) {
	if (_peer->isChat()
		|| (_additional.infoNotLoaded.find(user)
			== _additional.infoNotLoaded.end())) {
		return true;
	}

	// We don't know what this user status is in the group.
	const auto channel = _peer->asChannel();
	request(MTPchannels_GetParticipant(
		channel->inputChannel,
		user->inputUser
	)).done([=](const MTPchannels_ChannelParticipant &result) {
		Expects(result.type() == mtpc_channels_channelParticipant);

		const auto &participant = result.c_channels_channelParticipant();
		App::feedUsers(participant.vusers);
		HandleParticipant(
			participant.vparticipant,
			&_additional,
			[](not_null<UserData*>) {});
		_additional.infoNotLoaded.erase(user);
		callback();
	}).fail([this, user, callback](const RPCError &error) {
		_additional.infoNotLoaded.erase(user);
		_additional.external.emplace(user);
		callback();
	}).send();
	return false;
}

void AddSpecialBoxController::showAdmin(
		not_null<UserData*> user,
		bool sure) {
	if (!checkInfoLoaded(user, [=] { showAdmin(user); })) {
		return;
	}

	if (sure && _editBox) {
		// Close the confirmation box.
		_editBox->closeBox();
	}

	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();

	// Check restrictions.
	const auto canAddMembers = chat
		? chat->canAddMembers()
		: channel->canAddMembers();
	const auto canBanMembers = chat
		? chat->canBanMembers()
		: channel->canBanMembers();
	const auto weak = base::make_weak(this);
	const auto alreadyIt = _additional.adminRights.find(user);
	auto currentRights = (_additional.creator == user)
		? MTP_chatAdminRights(MTP_flags(~MTPDchatAdminRights::Flag::f_add_admins | MTPDchatAdminRights::Flag::f_add_admins))
		: MTP_chatAdminRights(MTP_flags(0));
	if (alreadyIt != _additional.adminRights.end()) {
		// The user is already an admin.
		currentRights = alreadyIt->second;
	} else if (_additional.kicked.find(user) != _additional.kicked.end()) {
		// The user is banned.
		if (canAddMembers) {
			if (canBanMembers) {
				if (!sure) {
					_editBox = Ui::show(Box<ConfirmBox>(lang(lng_sure_add_admin_unban), [weak, user] {
						if (weak) {
							weak->showAdmin(user, true);
						}
					}), LayerOption::KeepOther);
					return;
				}
			} else {
				Ui::show(Box<InformBox>(
					lang(lng_error_cant_add_admin_unban)),
					LayerOption::KeepOther);
				return;
			}
		} else {
			Ui::show(Box<InformBox>(
				lang(lng_error_cant_add_admin_invite)),
				LayerOption::KeepOther);
			return;
		}
	} else if (_additional.restrictedRights.find(user) != _additional.restrictedRights.end()) {
		// The user is restricted.
		if (canBanMembers) {
			if (!sure) {
				_editBox = Ui::show(Box<ConfirmBox>(lang(lng_sure_add_admin_unban), [weak, user] {
					if (weak) {
						weak->showAdmin(user, true);
					}
				}), LayerOption::KeepOther);
				return;
			}
		} else {
			Ui::show(Box<InformBox>(
				lang(lng_error_cant_add_admin_unban)),
				LayerOption::KeepOther);
			return;
		}
	} else if (_additional.external.find(user) != _additional.external.end()) {
		// The user is not in the group yet.
		if (canAddMembers) {
			if (!sure) {
				const auto text = lang(
					((_peer->isChat() || _peer->isMegagroup())
						? lng_sure_add_admin_invite
						: lng_sure_add_admin_invite_channel));
				_editBox = Ui::show(Box<ConfirmBox>(text, [=] {
					if (weak) {
						weak->showAdmin(user, true);
					}
				}), LayerOption::KeepOther);
				return;
			}
		} else {
			Ui::show(
				Box<InformBox>(lang(lng_error_cant_add_admin_invite)),
				LayerOption::KeepOther);
			return;
		}
	}

	// Finally show the admin.
	auto canNotEdit = (_additional.creator == user)
		|| ((alreadyIt != _additional.adminRights.end())
			&& (_additional.adminCanEdit.find(user) == _additional.adminCanEdit.end()));
	auto box = Box<EditAdminBox>(_peer, user, currentRights);
	if (!canNotEdit) {
		if (chat) {
			// #TODO groups autoconv
		} else {
			box->setSaveCallback(SaveAdminCallback(channel, user, [=](
					const MTPChatAdminRights &newRights) {
				if (weak) {
					weak->editAdminDone(user, newRights);
				}
			}, [=] {
				if (weak && weak->_editBox) {
					weak->_editBox->closeBox();
				}
			}));
		}
	}
	_editBox = Ui::show(std::move(box), LayerOption::KeepOther);
}

void AddSpecialBoxController::editAdminDone(
		not_null<UserData*> user,
		const MTPChatAdminRights &rights) {
	if (_editBox) _editBox->closeBox();
	_additional.restrictedRights.erase(user);
	_additional.restrictedBy.erase(user);
	_additional.kicked.erase(user);
	_additional.external.erase(user);
	if (rights.c_chatAdminRights().vflags.v == 0) {
		_additional.adminRights.erase(user);
		_additional.adminPromotedBy.erase(user);
		_additional.adminCanEdit.erase(user);
	} else {
		_additional.adminRights[user] = rights;
		_additional.adminCanEdit.emplace(user);
		auto it = _additional.adminPromotedBy.find(user);
		if (it == _additional.adminPromotedBy.end()) {
			_additional.adminPromotedBy.emplace(user, Auth().user());
		}
	}
	if (_adminDoneCallback) {
		_adminDoneCallback(user, rights);
	}
}

void AddSpecialBoxController::showRestricted(
		not_null<UserData*> user,
		bool sure) {
	if (!checkInfoLoaded(user, [=] { showRestricted(user); })) {
		return;
	}

	if (sure && _editBox) {
		// Close the confirmation box.
		_editBox->closeBox();
	}

	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();

	// Check restrictions.
	const auto weak = base::make_weak(this);
	const auto alreadyIt = _additional.restrictedRights.find(user);
	auto currentRights = MTP_chatBannedRights(MTP_flags(0), MTP_int(0));
	auto hasAdminRights = false;
	if (alreadyIt != _additional.restrictedRights.end()) {
		// The user is already banned or restricted.
		currentRights = alreadyIt->second;
	} else if (_additional.adminRights.find(user) != _additional.adminRights.end() || _additional.creator == user) {
		// The user is an admin or creator.
		if (_additional.adminCanEdit.find(user) != _additional.adminCanEdit.end()) {
			hasAdminRights = true;
			if (!sure) {
				_editBox = Ui::show(Box<ConfirmBox>(lang(lng_sure_ban_admin), [weak, user] {
					if (weak) {
						weak->showRestricted(user, true);
					}
				}), LayerOption::KeepOther);
				return;
			}
		} else {
			Ui::show(
				Box<InformBox>(lang(lng_error_cant_ban_admin)),
				LayerOption::KeepOther);
			return;
		}
	}

	// Finally edit the restricted.
	auto box = Box<EditRestrictedBox>(_peer, user, hasAdminRights, currentRights);
	if (chat) {
		// #TODO groups autoconv
	} else {
		box->setSaveCallback(SaveRestrictedCallback(channel, user, [=](
				const MTPChatBannedRights &newRights) {
			if (const auto strong = weak.get()) {
				strong->editRestrictedDone(user, newRights);
			}
		}, [=] {
			if (weak && weak->_editBox) {
				weak->_editBox->closeBox();
			}
		}));
	}
	_editBox = Ui::show(std::move(box), LayerOption::KeepOther);
}

void AddSpecialBoxController::editRestrictedDone(
		not_null<UserData*> user,
		const MTPChatBannedRights &rights) {
	if (_editBox) _editBox->closeBox();
	_additional.adminRights.erase(user);
	_additional.adminCanEdit.erase(user);
	_additional.adminPromotedBy.erase(user);
	if (rights.c_chatBannedRights().vflags.v == 0) {
		_additional.restrictedRights.erase(user);
		_additional.restrictedBy.erase(user);
		_additional.kicked.erase(user);
	} else {
		_additional.restrictedRights[user] = rights;
		if (rights.c_chatBannedRights().vflags.v & MTPDchatBannedRights::Flag::f_view_messages) {
			_additional.kicked.emplace(user);
		} else {
			_additional.kicked.erase(user);
		}
		_additional.restrictedBy.emplace(user, Auth().user());
	}
	if (_bannedDoneCallback) {
		_bannedDoneCallback(user, rights);
	}
}

void AddSpecialBoxController::kickUser(
		not_null<UserData*> user,
		bool sure) {
	if (!checkInfoLoaded(user, [=] { kickUser(user); })) {
		return;
	}

	// Check restrictions.
	auto weak = base::make_weak(this);
	if (_additional.adminRights.find(user) != _additional.adminRights.end() || _additional.creator == user) {
		// The user is an admin or creator.
		if (_additional.adminCanEdit.find(user) != _additional.adminCanEdit.end()) {
			if (!sure) {
				_editBox = Ui::show(Box<ConfirmBox>(lang(lng_sure_ban_admin), [weak, user] {
					if (weak) {
						weak->kickUser(user, true);
					}
				}), LayerOption::KeepOther);
				return;
			}
		} else {
			Ui::show(
				Box<InformBox>(lang(lng_error_cant_ban_admin)),
				LayerOption::KeepOther);
			return;
		}
	}

	// Finally kick him.
	if (!sure) {
		const auto text = ((_peer->isChat() || _peer->isMegagroup())
			? lng_sure_ban_user_group
			: lng_sure_ban_user_channel)(lt_user, App::peerName(user));
		_editBox = Ui::show(Box<ConfirmBox>(text, [=] {
			if (weak) {
				weak->kickUser(user, true);
			}
		}), LayerOption::KeepOther);
		return;
	}
	auto currentRights = MTP_chatBannedRights(MTP_flags(0), MTP_int(0));
	auto alreadyIt = _additional.restrictedRights.find(user);
	if (alreadyIt != _additional.restrictedRights.end()) {
		// The user is already banned or restricted.
		currentRights = alreadyIt->second;
	}
	if (const auto chat = _peer->asChat()) {
		// #TODO groups
	} else if (const auto channel = _peer->asChannel()) {
		const auto callback = SaveRestrictedCallback(channel, user, [](
			const MTPChatBannedRights &newRights) {}, [] {});
		callback(currentRights, ChannelData::KickedRestrictedRights());
	}
}

bool AddSpecialBoxController::appendRow(not_null<UserData*> user) {
	if (delegate()->peerListFindRow(user->id) || user->isSelf()) {
		return false;
	}
	delegate()->peerListAppendRow(createRow(user));
	return true;
}

bool AddSpecialBoxController::prependRow(not_null<UserData*> user) {
	if (delegate()->peerListFindRow(user->id)) {
		return false;
	}
	delegate()->peerListPrependRow(createRow(user));
	return true;
}

std::unique_ptr<PeerListRow> AddSpecialBoxController::createRow(
		not_null<UserData*> user) const {
	return std::make_unique<PeerListRow>(user);
}

template <typename Callback>
void AddSpecialBoxController::HandleParticipant(
		const MTPChannelParticipant &participant,
		not_null<Additional*> additional,
		Callback callback) {
	switch (participant.type()) {
	case mtpc_channelParticipantAdmin: {
		auto &admin = participant.c_channelParticipantAdmin();
		if (auto user = App::userLoaded(admin.vuser_id.v)) {
			additional->infoNotLoaded.erase(user);
			additional->restrictedRights.erase(user);
			additional->kicked.erase(user);
			additional->restrictedBy.erase(user);
			additional->adminRights[user] = admin.vadmin_rights;
			if (admin.is_can_edit()) {
				additional->adminCanEdit.emplace(user);
			} else {
				additional->adminCanEdit.erase(user);
			}
			if (auto promoted = App::userLoaded(admin.vpromoted_by.v)) {
				auto it = additional->adminPromotedBy.find(user);
				if (it == additional->adminPromotedBy.end()) {
					additional->adminPromotedBy.emplace(user, promoted);
				} else {
					it->second = promoted;
				}
			} else {
				LOG(("API Error: No user %1 for admin promoted by.").arg(admin.vpromoted_by.v));
			}
			callback(user);
		}
	} break;
	case mtpc_channelParticipantCreator: {
		auto &creator = participant.c_channelParticipantCreator();
		if (auto user = App::userLoaded(creator.vuser_id.v)) {
			additional->infoNotLoaded.erase(user);
			additional->creator = user;
			callback(user);
		}
	} break;
	case mtpc_channelParticipantBanned: {
		auto &banned = participant.c_channelParticipantBanned();
		if (auto user = App::userLoaded(banned.vuser_id.v)) {
			additional->infoNotLoaded.erase(user);
			additional->adminRights.erase(user);
			additional->adminCanEdit.erase(user);
			additional->adminPromotedBy.erase(user);
			if (banned.is_left()) {
				additional->kicked.emplace(user);
			} else {
				additional->kicked.erase(user);
			}
			additional->restrictedRights[user] = banned.vbanned_rights;
			if (auto kickedby = App::userLoaded(banned.vkicked_by.v)) {
				auto it = additional->restrictedBy.find(user);
				if (it == additional->restrictedBy.end()) {
					additional->restrictedBy.emplace(user, kickedby);
				} else {
					it->second = kickedby;
				}
			}
			callback(user);
		}
	} break;
	case mtpc_channelParticipant: {
		auto &data = participant.c_channelParticipant();
		if (auto user = App::userLoaded(data.vuser_id.v)) {
			additional->infoNotLoaded.erase(user);
			additional->adminRights.erase(user);
			additional->adminCanEdit.erase(user);
			additional->adminPromotedBy.erase(user);
			additional->restrictedRights.erase(user);
			additional->kicked.erase(user);
			additional->restrictedBy.erase(user);
			callback(user);
		}
	} break;
	default: Unexpected("Participant type in AddSpecialBoxController::HandleParticipant()");
	}
}

AddSpecialBoxSearchController::AddSpecialBoxSearchController(
	not_null<PeerData*> peer,
	not_null<Additional*> additional)
: _peer(peer)
, _additional(additional)
, _timer([=] { searchOnServer(); }) {
}

void AddSpecialBoxSearchController::searchQuery(const QString &query) {
	if (_query != query) {
		_query = query;
		_offset = 0;
		_requestId = 0;
		_participantsLoaded = false;
		_chatsContactsAdded = false;
		_globalLoaded = false;
		if (!_query.isEmpty() && !searchParticipantsInCache()) {
			_timer.callOnce(AutoSearchTimeout);
		} else {
			_timer.cancel();
		}
	}
}

void AddSpecialBoxSearchController::searchOnServer() {
	Expects(!_query.isEmpty());

	loadMoreRows();
}

bool AddSpecialBoxSearchController::isLoading() {
	return _timer.isActive() || _requestId;
}

bool AddSpecialBoxSearchController::searchParticipantsInCache() {
	auto it = _participantsCache.find(_query);
	if (it != _participantsCache.cend()) {
		_requestId = 0;
		searchParticipantsDone(_requestId, it->second.result, it->second.requestedCount);
		return true;
	}
	return false;
}

bool AddSpecialBoxSearchController::searchGlobalInCache() {
	auto it = _globalCache.find(_query);
	if (it != _globalCache.cend()) {
		_requestId = 0;
		searchGlobalDone(_requestId, it->second);
		return true;
	}
	return false;
}

bool AddSpecialBoxSearchController::loadMoreRows() {
	if (_query.isEmpty()) {
		return false;
	}
	if (_globalLoaded) {
		return true;
	}
	if (_participantsLoaded) {
		if (!_chatsContactsAdded) {
			addChatsContacts();
		}
		if (!isLoading() && !searchGlobalInCache()) {
			requestGlobal();
		}
	} else if (const auto chat = _peer->asChat()) {
		if (chat->participants.empty()) {
			return true;
		} else {
			addChatMembers();
			_participantsLoaded = true;
		}
	} else if (!isLoading()) {
		requestParticipants();
	}
	return true;
}

void AddSpecialBoxSearchController::requestParticipants() {
	Expects(_peer->isChannel());

	// For search we request a lot of rows from the first query.
	// (because we've waited for search request by timer already,
	// so we don't expect it to be fast, but we want to fill cache).
	const auto perPage = kParticipantsPerPage;
	const auto participantsHash = 0;
	const auto channel = _peer->asChannel();

	_requestId = request(MTPchannels_GetParticipants(
		channel->inputChannel,
		MTP_channelParticipantsSearch(MTP_string(_query)),
		MTP_int(_offset),
		MTP_int(perPage),
		MTP_int(participantsHash)
	)).done([=](
			const MTPchannels_ChannelParticipants &result,
			mtpRequestId requestId) {
		searchParticipantsDone(requestId, result, perPage);
	}).fail([=](const RPCError &error, mtpRequestId requestId) {
		if (_requestId == requestId) {
			_requestId = 0;
			_participantsLoaded = true;
			loadMoreRows();
			delegate()->peerListSearchRefreshRows();
		}
	}).send();

	auto entry = Query();
	entry.text = _query;
	entry.offset = _offset;
	_participantsQueries.emplace(_requestId, entry);
}

void AddSpecialBoxSearchController::searchParticipantsDone(
		mtpRequestId requestId,
		const MTPchannels_ChannelParticipants &result,
		int requestedCount) {
	Expects(_peer->isChannel());

	const auto channel = _peer->asChannel();
	auto query = _query;
	if (requestId) {
		Auth().api().parseChannelParticipants(channel, result, [&](auto&&...) {
			auto it = _participantsQueries.find(requestId);
			if (it != _participantsQueries.cend()) {
				query = it->second.text;
				if (it->second.offset == 0) {
					auto &entry = _participantsCache[query];
					entry.result = result;
					entry.requestedCount = requestedCount;
				}
				_participantsQueries.erase(it);
			}
		});
	}

	if (_requestId != requestId) {
		return;
	}
	_requestId = 0;
	TLHelp::VisitChannelParticipants(result, base::overload([&](
			const MTPDchannels_channelParticipants &data) {
		auto &list = data.vparticipants.v;
		if (list.size() < requestedCount) {
			// We want cache to have full information about a query with small
			// results count (if we don't need the second request). So we don't
			// wait for an empty results list unlike the non-search peer list.
			_participantsLoaded = true;
			if (list.empty() && _offset == 0) {
				// No results, so we want to request global search immediately.
				loadMoreRows();
			}
		}
		auto addUser = [&](auto user) {
			delegate()->peerListSearchAddRow(user);
		};
		for (auto &participant : list) {
			AddSpecialBoxController::HandleParticipant(
				participant,
				_additional,
				addUser);
		}
		_offset += list.size();
	}, [&](mtpTypeId type) {
		_participantsLoaded = true;
	}));

	delegate()->peerListSearchRefreshRows();
}

void AddSpecialBoxSearchController::requestGlobal() {
	if (_query.isEmpty()) {
		_globalLoaded = true;
		return;
	}

	auto perPage = SearchPeopleLimit;
	_requestId = request(MTPcontacts_Search(
		MTP_string(_query),
		MTP_int(perPage)
	)).done([=](const MTPcontacts_Found &result, mtpRequestId requestId) {
		searchGlobalDone(requestId, result);
	}).fail([=](const RPCError &error, mtpRequestId requestId) {
		if (_requestId == requestId) {
			_requestId = 0;
			_globalLoaded = true;
			delegate()->peerListSearchRefreshRows();
		}
	}).send();
	_globalQueries.emplace(_requestId, _query);
}

void AddSpecialBoxSearchController::searchGlobalDone(mtpRequestId requestId, const MTPcontacts_Found &result) {
	Expects(result.type() == mtpc_contacts_found);

	auto &found = result.c_contacts_found();
	auto query = _query;
	if (requestId) {
		App::feedUsers(found.vusers);
		App::feedChats(found.vchats);
		auto it = _globalQueries.find(requestId);
		if (it != _globalQueries.cend()) {
			query = it->second;
			_globalCache[query] = result;
			_globalQueries.erase(it);
		}
	}

	const auto feedList = [&](const MTPVector<MTPPeer> &list) {
		const auto contains = [](const auto &map, const auto &value) {
			return map.find(value) != map.end();
		};
		for (const auto &mtpPeer : list.v) {
			const auto peerId = peerFromMTP(mtpPeer);
			if (const auto peer = App::peerLoaded(peerId)) {
				if (const auto user = peer->asUser()) {
					if (_additional->creator != user
						&& !contains(_additional->adminRights, user)
						&& !contains(_additional->restrictedRights, user)
						&& !contains(_additional->external, user)
						&& !contains(_additional->kicked, user)) {
						_additional->infoNotLoaded.emplace(user);
					}
					delegate()->peerListSearchAddRow(user);
				}
			}
		}
	};
	if (_requestId == requestId) {
		_requestId = 0;
		_globalLoaded = true;
		feedList(found.vmy_results);
		feedList(found.vresults);
		delegate()->peerListSearchRefreshRows();
	}
}

void AddSpecialBoxSearchController::addChatMembers() {
	// #TODO groups
}

void AddSpecialBoxSearchController::addChatsContacts() {
	_chatsContactsAdded = true;

	auto wordList = TextUtilities::PrepareSearchWords(_query);
	if (wordList.empty()) {
		return;
	}

	auto getSmallestIndex = [&](Dialogs::IndexedList *list) -> const Dialogs::List* {
		if (list->isEmpty()) {
			return nullptr;
		}

		auto result = (const Dialogs::List*)nullptr;
		for_const (auto &word, wordList) {
			auto found = list->filtered(word[0]);
			if (found->isEmpty()) {
				return nullptr;
			}
			if (!result || result->size() > found->size()) {
				result = found;
			}
		}
		return result;
	};
	auto dialogsIndex = getSmallestIndex(App::main()->dialogsList());
	auto contactsIndex = getSmallestIndex(App::main()->contactsNoDialogsList());

	auto allWordsAreFound = [&](const base::flat_set<QString> &nameWords) {
		auto hasNamePartStartingWith = [&](const QString &word) {
			for (auto &nameWord : nameWords) {
				if (nameWord.startsWith(word)) {
					return true;
				}
			}
			return false;
		};

		for_const (auto &word, wordList) {
			if (!hasNamePartStartingWith(word)) {
				return false;
			}
		}
		return true;
	};
	auto filterAndAppend = [&](const Dialogs::List *list) {
		if (!list) {
			return;
		}

		for (const auto row : *list) {
			if (const auto history = row->history()) {
				if (const auto user = history->peer->asUser()) {
					if (allWordsAreFound(user->nameWords())) {
						delegate()->peerListSearchAddRow(user);
					}
				}
			}
		}
	};
	filterAndAppend(dialogsIndex);
	filterAndAppend(contactsIndex);
	delegate()->peerListSearchRefreshRows();
}
