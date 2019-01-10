/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_participants_box.h"

#include "boxes/peer_list_controllers.h"
#include "boxes/peers/edit_participant_box.h"
#include "boxes/peers/add_participants_box.h"
#include "boxes/confirm_box.h"
#include "boxes/add_contact_box.h"
#include "core/tl_help.h"
#include "base/overload.h"
#include "auth_session.h"
#include "apiwrap.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "observer_peer.h"
#include "dialogs/dialogs_indexed_list.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_controller.h"
#include "history/history.h"

namespace {

constexpr auto kParticipantsFirstPageCount = 16;
constexpr auto kParticipantsPerPage = 200;
constexpr auto kSortByOnlineDelay = TimeMs(1000);

void RemoveAdmin(
		not_null<ChannelData*> channel,
		not_null<UserData*> user,
		const MTPChatAdminRights &oldRights,
		Fn<void()> onDone) {
	const auto newRights = MTP_chatAdminRights(MTP_flags(0));
	channel->session().api().request(MTPchannels_EditAdmin(
		channel->inputChannel,
		user->inputUser,
		newRights
	)).done([=](const MTPUpdates &result) {
		channel->session().api().applyUpdates(result);
		channel->applyEditAdmin(user, oldRights, newRights);
		onDone();
	}).send();
}

void EditChatAdmin(
		not_null<ChatData*> chat,
		not_null<UserData*> user,
		bool isAdmin,
		Fn<void()> onDone) {
	chat->session().api().request(MTPmessages_EditChatAdmin(
		chat->inputChat,
		user->inputUser,
		MTP_bool(isAdmin)
	)).done([=](const MTPBool &result) {
		chat->applyEditAdmin(user, isAdmin);
		onDone();
	}).send();
}

} // namespace

Fn<void(
	const MTPChatAdminRights &oldRights,
	const MTPChatAdminRights &newRights)> SaveAdminCallback(
		not_null<ChannelData*> channel,
		not_null<UserData*> user,
		Fn<void(const MTPChatAdminRights &newRights)> onDone,
		Fn<void()> onFail) {
	return [=](
			const MTPChatAdminRights &oldRights,
			const MTPChatAdminRights &newRights) {
		auto done = [=](const MTPUpdates &result) {
			Auth().api().applyUpdates(result);
			channel->applyEditAdmin(user, oldRights, newRights);
			onDone(newRights);
		};
		auto fail = [=](const RPCError &error) {
			if (MTP::isDefaultHandledError(error)) {
				return false;
			}
			if (error.type() == qstr("USER_NOT_MUTUAL_CONTACT")) {
				Ui::show(
					Box<InformBox>(PeerFloodErrorText(
						channel->isMegagroup()
						? PeerFloodType::InviteGroup
						: PeerFloodType::InviteChannel)),
					LayerOption::KeepOther);
			} else if (error.type() == qstr("BOT_GROUPS_BLOCKED")) {
				Ui::show(
					Box<InformBox>(lang(lng_error_cant_add_bot)),
					LayerOption::KeepOther);
			} else if (error.type() == qstr("ADMINS_TOO_MUCH")) {
				Ui::show(
					Box<InformBox>(lang(channel->isMegagroup()
						? lng_error_admin_limit
						: lng_error_admin_limit_channel)),
					LayerOption::KeepOther);
			}
			onFail();
			return true;
		};
		MTP::send(
			MTPchannels_EditAdmin(
				channel->inputChannel,
				user->inputUser,
				newRights),
			rpcDone(std::move(done)),
			rpcFail(std::move(fail)));
	};
}

Fn<void(
	const MTPChatBannedRights &oldRights,
	const MTPChatBannedRights &newRights)> SaveRestrictedCallback(
		not_null<ChannelData*> channel,
		not_null<UserData*> user,
		Fn<void(const MTPChatBannedRights &newRights)> onDone,
		Fn<void()> onFail) {
	return [=](
			const MTPChatBannedRights &oldRights,
			const MTPChatBannedRights &newRights) {
		auto done = [=](const MTPUpdates &result) {
			Auth().api().applyUpdates(result);
			channel->applyEditBanned(user, oldRights, newRights);
			onDone(newRights);
		};
		auto fail = [=](const RPCError &error) {
			if (MTP::isDefaultHandledError(error)) {
				return false;
			}
			onFail();
			return true;
		};
		MTP::send(
			MTPchannels_EditBanned(
				channel->inputChannel,
				user->inputUser,
				newRights),
			rpcDone(std::move(done)),
			rpcFail(std::move(fail)));
	};
}

void ParticipantsBoxController::Additional::fillCreator(
		not_null<PeerData*> peer) {
	if (const auto chat = peer->asChat()) {
		creator = chat->owner().userLoaded(chat->creator);
	} else if (const auto channel = peer->asChannel()) {
		if (channel->mgInfo) {
			creator = channel->mgInfo->creator;
		}
	}
}

ParticipantsOnlineSorter::ParticipantsOnlineSorter(
	not_null<PeerData*> peer,
	not_null<PeerListDelegate*> delegate)
: _peer(peer)
, _delegate(delegate)
, _sortByOnlineTimer([=] { sort(); }) {
	const auto handleUpdate = [=](const Notify::PeerUpdate &update) {
		const auto peerId = update.peer->id;
		if (const auto row = _delegate->peerListFindRow(peerId)) {
			row->refreshStatus();
			sortDelayed();
		}
	};

	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(
		Notify::PeerUpdate::Flag::UserOnlineChanged,
		handleUpdate));
	sort();
}

void ParticipantsOnlineSorter::sortDelayed() {
	if (!_sortByOnlineTimer.isActive()) {
		_sortByOnlineTimer.callOnce(kSortByOnlineDelay);
	}
}

void ParticipantsOnlineSorter::sort() {
	const auto channel = _peer->asChannel();
	if (channel
		&& (!channel->isMegagroup()
			|| channel->membersCount() > Global::ChatSizeMax())) {
		_onlineCount = 0;
		return;
	}
	const auto now = unixtime();
	_delegate->peerListSortRows([&](
			const PeerListRow &a,
			const PeerListRow &b) {
		return Data::SortByOnlineValue(a.peer()->asUser(), now) >
			Data::SortByOnlineValue(b.peer()->asUser(), now);
	});
	refreshOnlineCount();
}

rpl::producer<int> ParticipantsOnlineSorter::onlineCountValue() const {
	return _onlineCount.value();
}

void ParticipantsOnlineSorter::refreshOnlineCount() {
	const auto now = unixtime();
	auto left = 0, right = _delegate->peerListFullRowsCount();
	while (right > left) {
		const auto middle = (left + right) / 2;
		const auto row = _delegate->peerListRowAt(middle);
		if (Data::OnlineTextActive(row->peer()->asUser(), now)) {
			left = middle + 1;
		} else {
			right = middle;
		}
	}
	_onlineCount = left;
}

ParticipantsBoxController::ParticipantsBoxController(
	not_null<Window::Navigation*> navigation,
	not_null<PeerData*> peer,
	Role role)
: PeerListController(CreateSearchController(peer, role, &_additional))
, _navigation(navigation)
, _peer(peer)
, _role(role) {
	_additional.fillCreator(_peer);
	if (_role == Role::Profile) {
		setupListChangeViewers();
	}
}

void ParticipantsBoxController::setupListChangeViewers() {
	const auto channel = _peer->asChannel();
	if (!channel || !channel->isMegagroup()) {
		return;
	}

	Auth().data().megagroupParticipantAdded(
		channel
	) | rpl::start_with_next([=](not_null<UserData*> user) {
		if (delegate()->peerListFullRowsCount() > 0) {
			if (delegate()->peerListRowAt(0)->peer() == user) {
				return;
			}
		}
		if (const auto row = delegate()->peerListFindRow(user->id)) {
			delegate()->peerListPartitionRows([&](const PeerListRow &row) {
				return (row.peer() == user);
			});
		} else {
			delegate()->peerListPrependRow(createRow(user));
			delegate()->peerListRefreshRows();
			if (_onlineSorter) {
				_onlineSorter->sort();
			}
		}
	}, lifetime());

	Auth().data().megagroupParticipantRemoved(
		channel
	) | rpl::start_with_next([=](not_null<UserData*> user) {
		if (const auto row = delegate()->peerListFindRow(user->id)) {
			delegate()->peerListRemoveRow(row);
		}
		delegate()->peerListRefreshRows();
	}, lifetime());
}

auto ParticipantsBoxController::CreateSearchController(
	not_null<PeerData*> peer,
	Role role,
	not_null<Additional*> additional)
-> std::unique_ptr<PeerListSearchController> {
	const auto channel = peer->asChannel();

	// In admins box complex search is used for adding new admins.
	if (channel && (role != Role::Admins || channel->canAddAdmins())) {
		return std::make_unique<ParticipantsBoxSearchController>(
			channel,
			role,
			additional);
	}
	return nullptr;
}

void ParticipantsBoxController::Start(
		not_null<Window::Navigation*> navigation,
		not_null<PeerData*> peer,
		Role role) {
	auto controller = std::make_unique<ParticipantsBoxController>(
		navigation,
		peer,
		role);
	auto initBox = [=, controller = controller.get()](
			not_null<PeerListBox*> box) {
		box->addButton(langFactory(lng_close), [=] { box->closeBox(); });

		const auto chat = peer->asChat();
		const auto channel = peer->asChannel();
		const auto canAddNewItem = [&] {
			Assert(chat != nullptr || channel != nullptr);

			switch (role) {
			case Role::Members:
				return chat
					? chat->canAddMembers()
					: (channel->canAddMembers()
						&& (channel->membersCount() < Global::ChatSizeMax()
							|| channel->isMegagroup()));
			case Role::Admins:
				return chat
					? chat->canAddAdmins()
					: channel->canAddAdmins();
			case Role::Restricted:
			case Role::Kicked:
				return chat
					? chat->canBanMembers()
					: channel->canBanMembers();
			}

			Unexpected("Role value in ParticipantsBoxController::Start()");
		}();
		const auto addNewItemText = [&] {
			switch (role) {
			case Role::Members:
				return langFactory((chat || channel->isMegagroup())
					? lng_channel_add_members
					: lng_channel_add_users);
			case Role::Admins:
				return langFactory(lng_channel_add_admin);
			case Role::Restricted:
				return langFactory(lng_channel_add_restricted);
			case Role::Kicked:
				return langFactory(lng_channel_add_banned);
			}
			Unexpected("Role value in ParticipantsBoxController::Start()");
		}();
		if (canAddNewItem) {
			box->addLeftButton(addNewItemText, [=] {
				controller->addNewItem();
			});
		}
	};
	Ui::show(
		Box<PeerListBox>(std::move(controller), initBox),
		LayerOption::KeepOther);
}

void ParticipantsBoxController::addNewItem() {
	Expects(_role != Role::Profile);

	if (_role == Role::Members) {
		addNewParticipants();
		return;
	}
	auto weak = base::make_weak(this);
	_addBox = Ui::show(
		Box<PeerListBox>(std::make_unique<AddSpecialBoxController>(
			_peer,
			_role,
			[=](not_null<UserData*> user, const MTPChatAdminRights &rights) {
				if (const auto strong = weak.get()) {
					strong->editAdminDone(user, rights);
				}
			}, [=](not_null<UserData*> user, const MTPChatBannedRights &rights) {
				if (const auto strong = weak.get()) {
					strong->editRestrictedDone(user, rights);
				}
			}), [](not_null<PeerListBox*> box) {
				box->addButton(langFactory(lng_cancel), [box] { box->closeBox(); });
			}),
		LayerOption::KeepOther);
}

void ParticipantsBoxController::addNewParticipants() {
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	if (chat) {
		AddParticipantsBoxController::Start(chat);
	} else if (channel->isMegagroup()
		|| channel->membersCount() < Global::ChatSizeMax()) {
		const auto count = delegate()->peerListFullRowsCount();
		auto already = std::vector<not_null<UserData*>>();
		already.reserve(count);
		for (auto i = 0; i != count; ++i) {
			already.push_back(
				delegate()->peerListRowAt(i)->peer()->asUser());
		}
		AddParticipantsBoxController::Start(
			channel,
			{ already.begin(), already.end() });
	} else {
		Ui::show(Box<MaxInviteBox>(channel), LayerOption::KeepOther);
	}
}

void ParticipantsBoxController::peerListSearchAddRow(not_null<PeerData*> peer) {
	PeerListController::peerListSearchAddRow(peer);
	if (_role == Role::Restricted && delegate()->peerListFullRowsCount() > 0) {
		setDescriptionText(QString());
	}
}

std::unique_ptr<PeerListRow> ParticipantsBoxController::createSearchRow(
		not_null<PeerData*> peer) {
	if (auto user = peer->asUser()) {
		return createRow(user);
	}
	return nullptr;
}

std::unique_ptr<PeerListRow> ParticipantsBoxController::createRestoredRow(
		not_null<PeerData*> peer) {
	if (auto user = peer->asUser()) {
		return createRow(user);
	}
	return nullptr;
}

std::unique_ptr<PeerListState> ParticipantsBoxController::saveState() const {
	Expects(_role == Role::Profile);

	auto result = PeerListController::saveState();

	auto my = std::make_unique<SavedState>();
	my->additional = _additional;
	my->offset = _offset;
	my->allLoaded = _allLoaded;
	my->wasLoading = (_loadRequestId != 0);
	if (auto search = searchController()) {
		my->searchState = search->saveState();
	}

	if (_peer->isMegagroup()) {
		const auto channel = _peer->asChannel();

		auto weak = result.get();
		Auth().data().megagroupParticipantAdded(
			channel
		) | rpl::start_with_next([weak](not_null<UserData*> user) {
			if (!weak->list.empty()) {
				if (weak->list[0] == user) {
					return;
				}
			}
			auto pos = ranges::find(weak->list, user);
			if (pos == weak->list.cend()) {
				weak->list.push_back(user);
			}
			ranges::stable_partition(
				weak->list,
				[user](auto peer) { return (peer == user); });
		}, my->lifetime);
		Auth().data().megagroupParticipantRemoved(
			channel
		) | rpl::start_with_next([weak](not_null<UserData*> user) {
			weak->list.erase(std::remove(
				weak->list.begin(),
				weak->list.end(),
				user), weak->list.end());
			weak->filterResults.erase(std::remove(
				weak->filterResults.begin(),
				weak->filterResults.end(),
				user), weak->filterResults.end());
		}, my->lifetime);
	}
	result->controllerState = std::move(my);
	return result;
}

void ParticipantsBoxController::restoreState(
		std::unique_ptr<PeerListState> state) {
	auto typeErasedState = state
		? state->controllerState.get()
		: nullptr;
	if (auto my = dynamic_cast<SavedState*>(typeErasedState)) {
		if (auto requestId = base::take(_loadRequestId)) {
			request(requestId).cancel();
		}

		_additional = std::move(my->additional);
		_offset = my->offset;
		_allLoaded = my->allLoaded;
		if (auto search = searchController()) {
			search->restoreState(std::move(my->searchState));
		}
		if (my->wasLoading) {
			loadMoreRows();
		}
		PeerListController::restoreState(std::move(state));
		if (delegate()->peerListFullRowsCount() > 0) {
			setNonEmptyDescription();
		} else if (_allLoaded) {
			setDescriptionText(lang(lng_blocked_list_not_found));
		}
		if (_onlineSorter) {
			_onlineSorter->sort();
		}
	}
}

rpl::producer<int> ParticipantsBoxController::onlineCountValue() const {
	return _onlineSorter
		? _onlineSorter->onlineCountValue()
		: rpl::single(0);
}

template <typename Callback>
void ParticipantsBoxController::HandleParticipant(const MTPChannelParticipant &participant, Role role, not_null<Additional*> additional, Callback callback) {
	if ((role == Role::Profile
		|| role == Role::Members
		|| role == Role::Admins)
		&& participant.type() == mtpc_channelParticipantAdmin) {
		auto &admin = participant.c_channelParticipantAdmin();
		if (auto user = App::userLoaded(admin.vuser_id.v)) {
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
	} else if ((role == Role::Profile
		|| role == Role::Members
		|| role == Role::Admins)
		&& participant.type() == mtpc_channelParticipantCreator) {
		auto &creator = participant.c_channelParticipantCreator();
		if (auto user = App::userLoaded(creator.vuser_id.v)) {
			additional->creator = user;
			callback(user);
		}
	} else if ((role == Role::Profile
		|| role == Role::Members
		|| role == Role::Restricted
		|| role == Role::Kicked)
		&& participant.type() == mtpc_channelParticipantBanned) {
		auto &banned = participant.c_channelParticipantBanned();
		if (auto user = App::userLoaded(banned.vuser_id.v)) {
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
	} else if ((role == Role::Profile
		|| role == Role::Members)
		&& participant.type() == mtpc_channelParticipant) {
		auto &member = participant.c_channelParticipant();
		if (auto user = App::userLoaded(member.vuser_id.v)) {
			callback(user);
		}
	} else if ((role == Role::Profile
		|| role == Role::Members)
		&& participant.type() == mtpc_channelParticipantSelf) {
		auto &member = participant.c_channelParticipantSelf();
		if (auto user = App::userLoaded(member.vuser_id.v)) {
			callback(user);
		}
	} else {
		LOG(("API Error: Bad participant type got while requesting for participants: %1").arg(participant.type()));
	}
}

void ParticipantsBoxController::prepare() {
	const auto titleKey = [&] {
		switch (_role) {
		case Role::Admins: return lng_channel_admins;
		case Role::Profile:
		case Role::Members: return lng_profile_participants_section;
		case Role::Restricted: return lng_restricted_list_title;
		case Role::Kicked: return lng_banned_list_title;
		}
		Unexpected("Role in ParticipantsBoxController::prepare()");
	}();
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	delegate()->peerListSetTitle(langFactory(titleKey));
	setDescriptionText(lang(lng_contacts_loading));
	setSearchNoResultsText(lang(lng_blocked_list_not_found));

	if (const auto chat = _peer->asChat()) {
		prepareChatRows(chat);
	} else {
		loadMoreRows();
	}
	if (_role == Role::Profile && !_onlineSorter) {
		_onlineSorter = std::make_unique<ParticipantsOnlineSorter>(
			_peer,
			delegate());
	}
	delegate()->peerListRefreshRows();
}

void ParticipantsBoxController::prepareChatRows(not_null<ChatData*> chat) {
	_onlineSorter = std::make_unique<ParticipantsOnlineSorter>(
		chat,
		delegate());

	rebuildChatRows(chat);
	if (!delegate()->peerListFullRowsCount()) {
		chat->updateFullForced();
	}

	using UpdateFlag = Notify::PeerUpdate::Flag;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(
		UpdateFlag::MembersChanged
		| UpdateFlag::AdminsChanged,
		[=](const Notify::PeerUpdate &update) {
			if (update.flags & UpdateFlag::MembersChanged) {
				if (update.peer == chat) {
					rebuildChatRows(chat);
				}
			} else if (update.flags & UpdateFlag::AdminsChanged) {
				if (update.peer == chat) {
					rebuildRowTypes();
				}
			}
		}));
}

void ParticipantsBoxController::rebuildChatRows(not_null<ChatData*> chat) {
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

void ParticipantsBoxController::rebuildRowTypes() {
	const auto count = delegate()->peerListFullRowsCount();
	for (auto i = 0; i != count; ++i) {
		const auto row = static_cast<Row*>(
			delegate()->peerListRowAt(i).get());
		row->setType(computeType(row->user()));
	}
	delegate()->peerListRefreshRows();
}

void ParticipantsBoxController::loadMoreRows() {
	if (searchController() && searchController()->loadMoreRows()) {
		return;
	} else if (!_peer->isChannel() || _loadRequestId || _allLoaded) {
		return;
	}

	const auto channel = _peer->asChannel();
	if (feedMegagroupLastParticipants()) {
		return;
	}

	const auto filter = [&] {
		if (_role == Role::Members || _role == Role::Profile) {
			return MTP_channelParticipantsRecent();
		} else if (_role == Role::Admins) {
			return MTP_channelParticipantsAdmins();
		} else if (_role == Role::Restricted) {
			return MTP_channelParticipantsBanned(MTP_string(QString()));
		}
		return MTP_channelParticipantsKicked(MTP_string(QString()));
	}();

	// First query is small and fast, next loads a lot of rows.
	const auto perPage = (_offset > 0)
		? kParticipantsPerPage
		: kParticipantsFirstPageCount;
	const auto participantsHash = 0;

	_loadRequestId = request(MTPchannels_GetParticipants(
		channel->inputChannel,
		filter,
		MTP_int(_offset),
		MTP_int(perPage),
		MTP_int(participantsHash)
	)).done([=](const MTPchannels_ChannelParticipants &result) {
		const auto firstLoad = !_offset;
		_loadRequestId = 0;

		auto wasRecentRequest = firstLoad
			&& (_role == Role::Members || _role == Role::Profile);
		auto parseParticipants = [&](auto &&result, auto &&callback) {
			if (wasRecentRequest) {
				Auth().api().parseRecentChannelParticipants(
					channel,
					result,
					callback);
			} else {
				Auth().api().parseChannelParticipants(
					channel,
					result,
					callback);
			}
		};
		parseParticipants(result, [&](
				int availableCount,
				const QVector<MTPChannelParticipant> &list) {
			for (auto &participant : list) {
				HandleParticipant(
					participant,
					_role,
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
			if (_onlineSorter) {
				_onlineSorter->sort();
			}
			if (firstLoad) {
				setNonEmptyDescription();
			}
		} else if (_allLoaded) {
			setDescriptionText(lang(lng_blocked_list_not_found));
		}
		delegate()->peerListRefreshRows();
	}).fail([this](const RPCError &error) {
		_loadRequestId = 0;
	}).send();
}

void ParticipantsBoxController::setNonEmptyDescription() {
	setDescriptionText((_role == Role::Kicked)
		? lang((_peer->isChat() || _peer->isMegagroup())
			? lng_group_blocked_list_about
			: lng_channel_blocked_list_about)
		: QString());
}

bool ParticipantsBoxController::feedMegagroupLastParticipants() {
	if ((_role != Role::Members && _role != Role::Profile)
		|| delegate()->peerListFullRowsCount() > 0) {
		return false;
	}
	const auto megagroup = _peer->asMegagroup();
	if (!megagroup) {
		return false;
	}
	auto info = megagroup->mgInfo.get();
	//
	// channelFull and channels_channelParticipants members count is desynced
	// so we almost always have LastParticipantsCountOutdated that is set
	// inside setMembersCount() and so we almost never use lastParticipants.
	//
	// => disable this check temporarily.
	//
	//if (info->lastParticipantsStatus != MegagroupInfo::LastParticipantsUpToDate) {
	//	_channel->updateFull();
	//	return false;
	//}
	if (info->lastParticipants.empty()) {
		return false;
	}

	if (info->creator) {
		_additional.creator = info->creator;
	}
	for_const (auto user, info->lastParticipants) {
		auto admin = info->lastAdmins.find(user);
		if (admin != info->lastAdmins.cend()) {
			_additional.restrictedRights.erase(user);
			if (admin->second.canEdit) {
				_additional.adminCanEdit.emplace(user);
			} else {
				_additional.adminCanEdit.erase(user);
			}
			_additional.adminRights.emplace(user, admin->second.rights);
		} else {
			_additional.adminCanEdit.erase(user);
			_additional.adminRights.erase(user);
			auto restricted = info->lastRestricted.find(user);
			if (restricted != info->lastRestricted.cend()) {
				_additional.restrictedRights.emplace(user, restricted->second.rights);
			} else {
				_additional.restrictedRights.erase(user);
			}
		}
		appendRow(user);

		//
		// Don't count lastParticipants in _offset, because we don't know
		// their exact information (admin / creator / restricted), they
		// could simply be added from the last messages authors.
		//
		//++_offset;
	}
	if (_onlineSorter) {
		_onlineSorter->sort();
	}
	return true;
}

void ParticipantsBoxController::rowClicked(not_null<PeerListRow*> row) {
	auto user = row->peer()->asUser();
	Expects(user != nullptr);

	if (_role == Role::Admins) {
		showAdmin(user);
	} else if (_role == Role::Restricted || _role == Role::Kicked) {
		showRestricted(user);
	} else {
		_navigation->showPeerInfo(row->peer());
	}
}

void ParticipantsBoxController::rowActionClicked(not_null<PeerListRow*> row) {
	auto user = row->peer()->asUser();
	Expects(user != nullptr);

	if (_role == Role::Members || _role == Role::Profile) {
		kickMember(user);
	} else if (_role == Role::Admins) {
		removeAdmin(user);
	} else if (_role == Role::Restricted) {
		showRestricted(user);
	} else {
		removeKicked(row, user);
	}
}

bool ParticipantsBoxController::canEditAdminByRights(
		not_null<UserData*> user) const {
	if (const auto chat = _peer->asChat()) {
		return chat->amCreator() && (user != _additional.creator);
	}
	if (_additional.adminRights.find(user)
		!= _additional.adminRights.cend()) {
		return (_additional.adminCanEdit.find(user)
			!= _additional.adminCanEdit.cend());
	}
	return (user != _additional.creator);
}

bool ParticipantsBoxController::canEditAdmin(
		not_null<UserData*> user) const {
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	if (user->isSelf()) {
		return false;
	} else if (chat ? chat->amCreator() : channel->amCreator()) {
		return true;
	} else if (!canEditAdminByRights(user)) {
		return false;
	}
	return (channel != nullptr)
		&& (channel->adminRights() & ChatAdminRight::f_add_admins);
}

bool ParticipantsBoxController::canRestrictUser(
		not_null<UserData*> user) const {
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	if (user->isSelf()) {
		return false;
	} else if (chat ? chat->amCreator() : channel->amCreator()) {
		return true;
	} else if (!canEditAdminByRights(user)) {
		return false;
	}
	return (channel != nullptr)
		&& (channel->adminRights() & ChatAdminRight::f_ban_users);
}

base::unique_qptr<Ui::PopupMenu> ParticipantsBoxController::rowContextMenu(
		QWidget *parent,
		not_null<PeerListRow*> row) {
	Expects(row->peer()->isUser());

	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	const auto user = row->peer()->asUser();
	auto result = base::make_unique_q<Ui::PopupMenu>(parent);
	result->addAction(
		lang(lng_context_view_profile),
		[weak = base::make_weak(this), user] {
			if (const auto strong = weak.get()) {
				strong->_navigation->showPeerInfo(user);
			}
		});
	if (canEditAdmin(user)) {
		auto it = _additional.adminRights.find(user);
		auto isCreator = (user == _additional.creator);
		auto notAdmin = !isCreator
			&& (it == _additional.adminRights.cend())
			&& (channel || !chat->admins.contains(user));
		auto label = lang(notAdmin
			? lng_context_promote_admin
			: lng_context_edit_permissions);
		result->addAction(
			label,
			[weak = base::make_weak(this), user] {
				if (const auto strong = weak.get()) {
					strong->showAdmin(user);
				}
			});
	}
	if (canRestrictUser(user)) {
		const auto isGroup = _peer->isChat() || _peer->isMegagroup();
		const auto weak = base::make_weak(this);
		if (isGroup) {
			result->addAction(
				lang(lng_context_restrict_user),
				[=]{
					if (const auto strong = weak.get()) {
						strong->showRestricted(user);
					}
				});
		}
		result->addAction(
			lang(isGroup
				? lng_context_remove_from_group
				: lng_profile_kick),
			[=] {
				if (const auto strong = weak.get()) {
					strong->kickMember(user);
				}
			});
	}
	return result;
}

void ParticipantsBoxController::showAdmin(not_null<UserData*> user) {
	const auto it = _additional.adminRights.find(user);
	const auto isCreator = (user == _additional.creator);
	const auto isAdmin = isCreator || (it != _additional.adminRights.cend());
	const auto currentRights = isCreator
		? MTP_chatAdminRights(
			MTP_flags(~MTPDchatAdminRights::Flag::f_add_admins
				| MTPDchatAdminRights::Flag::f_add_admins))
		: isAdmin
		? it->second
		: MTP_chatAdminRights(MTP_flags(0));
	const auto weak = base::make_weak(this);
	auto box = Box<EditAdminBox>(_peer, user, currentRights);
	const auto canEdit = (_additional.adminCanEdit.find(user)
		!= _additional.adminCanEdit.end());
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	const auto canSave = isAdmin
		? canEdit
		: chat
		? chat->canAddAdmins()
		: channel->canAddAdmins();
	if (canSave) {
		if (chat) {
			// #TODO groups autoconv
		} else if (channel) {
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

void ParticipantsBoxController::editAdminDone(
		not_null<UserData*> user,
		const MTPChatAdminRights &rights) {
	if (_editBox) {
		_editBox->closeBox();
	}
	if (_addBox) {
		_addBox->closeBox();
	}
	auto notAdmin = (rights.c_chatAdminRights().vflags.v == 0);
	if (notAdmin) {
		_additional.adminRights.erase(user);
		_additional.adminPromotedBy.erase(user);
		_additional.adminCanEdit.erase(user);
		if (_role == Role::Admins) {
			removeRow(user);
		}
	} else {
		// It won't be replaced if the entry already exists.
		_additional.adminPromotedBy.emplace(user, Auth().user());
		_additional.adminCanEdit.emplace(user);
		_additional.adminRights[user] = rights;
		_additional.kicked.erase(user);
		_additional.restrictedRights.erase(user);
		_additional.restrictedBy.erase(user);
		if (_role == Role::Admins) {
			prependRow(user);
		} else if (_role == Role::Kicked || _role == Role::Restricted) {
			removeRow(user);
		}
	}
	recomputeTypeFor(user);
	delegate()->peerListRefreshRows();
}

void ParticipantsBoxController::showRestricted(not_null<UserData*> user) {
	const auto it = _additional.restrictedRights.find(user);
	const auto currentRights = (it == _additional.restrictedRights.cend())
		? MTP_chatBannedRights(MTP_flags(0), MTP_int(0))
		: it->second;
	const auto weak = base::make_weak(this);
	const auto hasAdminRights = false;
	auto box = Box<EditRestrictedBox>(
		_peer,
		user,
		hasAdminRights,
		currentRights);
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	const auto canSave = chat
		? chat->canBanMembers()
		: channel->canBanMembers();
	if (canSave) {
		if (chat) {
			// #TODO groups autoconv
		} else {
			box->setSaveCallback(SaveRestrictedCallback(channel, user, [=](
					const MTPChatBannedRights &newRights) {
				if (weak) {
					weak->editRestrictedDone(user, newRights);
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

void ParticipantsBoxController::editRestrictedDone(
		not_null<UserData*> user,
		const MTPChatBannedRights &rights) {
	if (_editBox) {
		_editBox->closeBox();
	}
	if (_addBox) {
		_addBox->closeBox();
	}
	auto notBanned = (rights.c_chatBannedRights().vflags.v == 0);
	auto fullBanned = rights.c_chatBannedRights().is_view_messages();
	if (notBanned) {
		_additional.kicked.erase(user);
		_additional.restrictedRights.erase(user);
		_additional.restrictedBy.erase(user);
		if (_role == Role::Kicked || _role == Role::Restricted) {
			removeRow(user);
		}
	} else {
		_additional.adminRights.erase(user);
		_additional.adminCanEdit.erase(user);
		_additional.adminPromotedBy.erase(user);
		_additional.restrictedBy.emplace(user, Auth().user());
		if (fullBanned) {
			_additional.kicked.emplace(user);
			_additional.restrictedRights.erase(user);
			if (_role == Role::Kicked) {
				prependRow(user);
			} else if (_role == Role::Admins
				|| _role == Role::Restricted
				|| _role == Role::Members) {
				removeRow(user);
			}
		} else {
			_additional.restrictedRights[user] = rights;
			_additional.kicked.erase(user);
			if (_role == Role::Restricted) {
				prependRow(user);
			} else if (_role == Role::Kicked
				|| _role == Role::Admins
				|| _role == Role::Members) {
				removeRow(user);
			}
		}
	}
	recomputeTypeFor(user);
	delegate()->peerListRefreshRows();
}

void ParticipantsBoxController::kickMember(not_null<UserData*> user) {
	const auto text = ((_peer->isChat() || _peer->isMegagroup())
		? lng_profile_sure_kick
		: lng_profile_sure_kick_channel)(lt_user, user->firstName);
	const auto weak = base::make_weak(this);
	_editBox = Ui::show(Box<ConfirmBox>(text, lang(lng_box_remove), [=] {
		if (const auto strong = weak.get()) {
			strong->kickMemberSure(user);
		}
	}), LayerOption::KeepOther);
}

void ParticipantsBoxController::kickMemberSure(not_null<UserData*> user) {
	if (_editBox) {
		_editBox->closeBox();
	}
	const auto i = _additional.restrictedRights.find(user);
	const auto currentRights = (i == _additional.restrictedRights.cend())
		? MTP_chatBannedRights(MTP_flags(0), MTP_int(0))
		: i->second;

	if (const auto row = delegate()->peerListFindRow(user->id)) {
		delegate()->peerListRemoveRow(row);
		delegate()->peerListRefreshRows();
	}
	if (const auto chat = _peer->asChat()) {
		Auth().api().kickParticipant(chat, user);
	} else if (const auto channel = _peer->asChannel()) {
		Auth().api().kickParticipant(channel, user, currentRights);
	}
}

void ParticipantsBoxController::removeAdmin(not_null<UserData*> user) {
	const auto text = lng_profile_sure_remove_admin(lt_user, user->firstName);
	const auto weak = base::make_weak(this);
	_editBox = Ui::show(Box<ConfirmBox>(text, lang(lng_box_remove), [=] {
		if (const auto strong = weak.get()) {
			strong->removeAdminSure(user);
		}
	}), LayerOption::KeepOther);
}

void ParticipantsBoxController::removeAdminSure(not_null<UserData*> user) {
	if (_editBox) {
		_editBox->closeBox();
	}
	const auto weak = base::make_weak(this);
	if (const auto chat = _peer->asChat()) {
		EditChatAdmin(chat, user, false, [=] {
			if (const auto strong = weak.get()) {
				const auto newRights = MTP_chatAdminRights(MTP_flags(0));
				strong->editAdminDone(user, newRights);
			}
		});
	} else if (const auto channel = _peer->asChannel()) {
		const auto oldRightsIt = _additional.adminRights.find(user);
		if (oldRightsIt == _additional.adminRights.cend()) {
			return;
		}
		RemoveAdmin(channel, user, oldRightsIt->second, [=] {
			if (const auto strong = weak.get()) {
				const auto newRights = MTP_chatAdminRights(MTP_flags(0));
				strong->editAdminDone(user, newRights);
			}
		});
	}
}

void ParticipantsBoxController::removeKicked(
		not_null<PeerListRow*> row,
		not_null<UserData*> user) {
	delegate()->peerListRemoveRow(row);
	delegate()->peerListRefreshRows();

	if (const auto channel = _peer->asChannel()) {
		Auth().api().unblockParticipant(channel, user);
	}
}

bool ParticipantsBoxController::appendRow(not_null<UserData*> user) {
	if (delegate()->peerListFindRow(user->id)) {
		recomputeTypeFor(user);
		return false;
	}
	delegate()->peerListAppendRow(createRow(user));
	if (_role != Role::Kicked) {
		setDescriptionText(QString());
	}
	return true;
}

bool ParticipantsBoxController::prependRow(not_null<UserData*> user) {
	if (auto row = delegate()->peerListFindRow(user->id)) {
		recomputeTypeFor(user);
		refreshCustomStatus(row);
		if (_role == Role::Admins) {
			// Perhaps we've added a new admin from search.
			delegate()->peerListPrependRowFromSearchResult(row);
		}
		return false;
	}
	delegate()->peerListPrependRow(createRow(user));
	if (_role != Role::Kicked) {
		setDescriptionText(QString());
	}
	return true;
}

bool ParticipantsBoxController::removeRow(not_null<UserData*> user) {
	if (auto row = delegate()->peerListFindRow(user->id)) {
		if (_role == Role::Admins) {
			// Perhaps we are removing an admin from search results.
			row->setCustomStatus(lang(lng_channel_admin_status_not_admin));
			delegate()->peerListConvertRowToSearchResult(row);
		} else {
			delegate()->peerListRemoveRow(row);
		}
		if (_role != Role::Kicked
			&& !delegate()->peerListFullRowsCount()) {
			setDescriptionText(lang(lng_blocked_list_not_found));
		}
		return true;
	}
	return false;
}

std::unique_ptr<PeerListRow> ParticipantsBoxController::createRow(
		not_null<UserData*> user) const {
	if (_role == Role::Profile) {
		return std::make_unique<Row>(user, computeType(user));
	}
	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	auto row = std::make_unique<PeerListRowWithLink>(user);
	refreshCustomStatus(row.get());
	if (_role == Role::Admins
		&& canEditAdminByRights(user)
		&& _additional.adminRights.find(user)
			!= _additional.adminRights.cend()) {
		row->setActionLink(lang(lng_profile_kick));
	} else if (_role == Role::Restricted
		|| (_role == Role::Admins
			&& _additional.adminCanEdit.find(user)
				!= _additional.adminCanEdit.cend())) {
//		row->setActionLink(lang(lng_profile_edit_permissions));
	} else if (_role == Role::Kicked) {
		row->setActionLink(lang(lng_blocked_list_unblock));
	} else if (_role == Role::Members) {
		if ((chat
			&& chat->canBanMembers()
			&& !chat->admins.contains(user))
			|| (channel
				&& channel->canBanMembers()
				&& (_additional.creator != user)
				&& (_additional.adminRights.find(user) == _additional.adminRights.cend()
					|| _additional.adminCanEdit.find(user) != _additional.adminCanEdit.cend()))) {
			row->setActionLink(lang(lng_profile_kick));
		}
	}
	return std::move(row);
}

auto ParticipantsBoxController::computeType(
		not_null<UserData*> user) const -> Type {
	auto isCreator = (user == _additional.creator);
	auto isAdmin = (_additional.adminRights.find(user) != _additional.adminRights.cend());

	auto result = Type();
	result.rights = isCreator
		? Rights::Creator
		: isAdmin
		? Rights::Admin
		: Rights::Normal;
	result.canRemove = canRestrictUser(user);
	return result;
}

void ParticipantsBoxController::recomputeTypeFor(
		not_null<UserData*> user) {
	if (_role != Role::Profile) {
		return;
	}
	if (auto row = delegate()->peerListFindRow(user->id)) {
		static_cast<Row*>(row)->setType(computeType(user));
	}
}

void ParticipantsBoxController::refreshCustomStatus(not_null<PeerListRow*> row) const {
	auto user = row->peer()->asUser();
	if (_role == Role::Admins) {
		auto promotedBy = _additional.adminPromotedBy.find(user);
		if (promotedBy == _additional.adminPromotedBy.cend()) {
			if (user == _additional.creator) {
				row->setCustomStatus(lang(lng_channel_admin_status_creator));
			} else {
				row->setCustomStatus(lang(lng_channel_admin_status_not_admin));
			}
		} else {
			row->setCustomStatus(lng_channel_admin_status_promoted_by(lt_user, App::peerName(promotedBy->second)));
		}
	} else if (_role == Role::Kicked || _role == Role::Restricted) {
		auto restrictedBy = _additional.restrictedBy.find(user);
		if (restrictedBy == _additional.restrictedBy.cend()) {
			row->setCustomStatus(lng_channel_banned_status_restricted_by(lt_user, "Unknown"));
		} else {
			row->setCustomStatus(lng_channel_banned_status_restricted_by(lt_user, App::peerName(restrictedBy->second)));
		}
	}
}

ParticipantsBoxSearchController::ParticipantsBoxSearchController(not_null<ChannelData*> channel, Role role, not_null<Additional*> additional)
: _channel(channel)
, _role(role)
, _additional(additional) {
	_timer.setCallback([this] { searchOnServer(); });
}

void ParticipantsBoxSearchController::searchQuery(const QString &query) {
	if (_query != query) {
		_query = query;
		_offset = 0;
		_requestId = 0;
		_allLoaded = false;
		if (!_query.isEmpty() && !searchInCache()) {
			_timer.callOnce(AutoSearchTimeout);
		} else {
			_timer.cancel();
		}
	}
}

auto ParticipantsBoxSearchController::saveState() const
-> std::unique_ptr<SavedStateBase> {
	auto result = std::make_unique<SavedState>();
	result->query = _query;
	result->offset = _offset;
	result->allLoaded = _allLoaded;
	result->wasLoading = (_requestId != 0);
	return std::move(result);
}

void ParticipantsBoxSearchController::restoreState(
		std::unique_ptr<SavedStateBase> state) {
	if (auto my = dynamic_cast<SavedState*>(state.get())) {
		if (auto requestId = base::take(_requestId)) {
			request(requestId).cancel();
		}
		_cache.clear();
		_queries.clear();

		_allLoaded = my->allLoaded;
		_offset = my->offset;
		_query = my->query;
		if (my->wasLoading) {
			searchOnServer();
		}
	}
}

void ParticipantsBoxSearchController::searchOnServer() {
	Expects(!_query.isEmpty());
	loadMoreRows();
}

bool ParticipantsBoxSearchController::isLoading() {
	return _timer.isActive() || _requestId;
}

bool ParticipantsBoxSearchController::searchInCache() {
	auto it = _cache.find(_query);
	if (it != _cache.cend()) {
		_requestId = 0;
		searchDone(_requestId, it->second.result, it->second.requestedCount);
		return true;
	}
	return false;
}

bool ParticipantsBoxSearchController::loadMoreRows() {
	if (_query.isEmpty()) {
		return false;
	}
	if (!_allLoaded && !isLoading()) {
		auto filter = [this] {
			switch (_role) {
			case Role::Admins: // Search for members, appoint as admin on found.
			case Role::Profile:
			case Role::Members: return MTP_channelParticipantsSearch(MTP_string(_query));
			case Role::Restricted: return MTP_channelParticipantsBanned(MTP_string(_query));
			case Role::Kicked: return MTP_channelParticipantsKicked(MTP_string(_query));
			}
			Unexpected("Role in ParticipantsBoxSearchController::loadMoreRows()");
		}();

		// For search we request a lot of rows from the first query.
		// (because we've waited for search request by timer already,
		// so we don't expect it to be fast, but we want to fill cache).
		auto perPage = kParticipantsPerPage;
		auto participantsHash = 0;

		_requestId = request(MTPchannels_GetParticipants(
			_channel->inputChannel,
			filter,
			MTP_int(_offset),
			MTP_int(perPage),
			MTP_int(participantsHash)
		)).done([this, perPage](const MTPchannels_ChannelParticipants &result, mtpRequestId requestId) {
			searchDone(requestId, result, perPage);
		}).fail([this](const RPCError &error, mtpRequestId requestId) {
			if (_requestId == requestId) {
				_requestId = 0;
				_allLoaded = true;
				delegate()->peerListSearchRefreshRows();
			}
		}).send();

		auto entry = Query();
		entry.text = _query;
		entry.offset = _offset;
		_queries.emplace(_requestId, entry);
	}
	return true;
}

void ParticipantsBoxSearchController::searchDone(
		mtpRequestId requestId,
		const MTPchannels_ChannelParticipants &result,
		int requestedCount) {
	auto query = _query;
	if (requestId) {
		Auth().api().parseChannelParticipants(_channel, result, [&](auto&&...) {
			auto it = _queries.find(requestId);
			if (it != _queries.cend()) {
				query = it->second.text;
				if (it->second.offset == 0) {
					auto &entry = _cache[query];
					entry.result = result;
					entry.requestedCount = requestedCount;
				}
				_queries.erase(it);
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
			_allLoaded = true;
		}
		auto parseRole = (_role == Role::Admins) ? Role::Members : _role;
		for_const (auto &participant, list) {
			ParticipantsBoxController::HandleParticipant(participant, parseRole, _additional, [this](not_null<UserData*> user) {
				delegate()->peerListSearchAddRow(user);
			});
		}
		_offset += list.size();
	}, [&](mtpTypeId type) {
		_allLoaded = true;
	}));

	delegate()->peerListSearchRefreshRows();
}
