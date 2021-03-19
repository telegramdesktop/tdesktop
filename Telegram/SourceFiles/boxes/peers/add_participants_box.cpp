/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/add_participants_box.h"

#include "boxes/peers/edit_participant_box.h"
#include "boxes/peers/edit_peer_type_box.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_folder.h"
#include "data/data_changes.h"
#include "history/history.h"
#include "dialogs/dialogs_indexed_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/padding_wrap.h"
#include "base/unixtime.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "window/window_session_controller.h"
#include "info/profile/info_profile_icon.h"
#include "apiwrap.h"
#include "facades.h" // Ui::showPeerHistory
#include "app.h"
#include "styles/style_boxes.h"

namespace {

constexpr auto kParticipantsFirstPageCount = 16;
constexpr auto kParticipantsPerPage = 200;

base::flat_set<not_null<UserData*>> GetAlreadyInFromPeer(PeerData *peer) {
	if (!peer) {
		return {};
	}
	if (const auto chat = peer->asChat()) {
		return chat->participants;
	} else if (const auto channel = peer->asChannel()) {
		if (channel->isMegagroup()) {
			const auto &participants = channel->mgInfo->lastParticipants;
			return { participants.cbegin(), participants.cend() };
		}
	}
	return {};
}

} // namespace

AddParticipantsBoxController::AddParticipantsBoxController(
	not_null<Main::Session*> session)
: ContactsBoxController(session) {
}

AddParticipantsBoxController::AddParticipantsBoxController(
	not_null<PeerData*> peer)
: AddParticipantsBoxController(
	peer,
	GetAlreadyInFromPeer(peer)) {
}

AddParticipantsBoxController::AddParticipantsBoxController(
	not_null<PeerData*> peer,
	base::flat_set<not_null<UserData*>> &&alreadyIn)
: ContactsBoxController(&peer->session())
, _peer(peer)
, _alreadyIn(std::move(alreadyIn)) {
	if (needsInviteLinkButton()) {
		setStyleOverrides(&st::peerListWithInviteViaLink);
	}
	subscribeToMigration();
}

void AddParticipantsBoxController::subscribeToMigration() {
	Expects(_peer != nullptr);

	SubscribeToMigration(
		_peer,
		lifetime(),
		[=](not_null<ChannelData*> channel) { _peer = channel; });
}

void AddParticipantsBoxController::rowClicked(not_null<PeerListRow*> row) {
	const auto &serverConfig = session().serverConfig();
	auto count = fullCount();
	auto limit = _peer && (_peer->isChat() || _peer->isMegagroup())
		? serverConfig.megagroupSizeMax
		: serverConfig.chatSizeMax;
	if (count < limit || row->checked()) {
		delegate()->peerListSetRowChecked(row, !row->checked());
		updateTitle();
	} else if (const auto channel = _peer ? _peer->asChannel() : nullptr) {
		if (!_peer->isMegagroup()) {
			Ui::show(
				Box<MaxInviteBox>(_peer->asChannel()),
				Ui::LayerOption::KeepOther);
		}
	} else if (count >= serverConfig.chatSizeMax
		&& count < serverConfig.megagroupSizeMax) {
		Ui::show(
			Box<InformBox>(tr::lng_profile_add_more_after_create(tr::now)),
			Ui::LayerOption::KeepOther);
	}
}

void AddParticipantsBoxController::itemDeselectedHook(
		not_null<PeerData*> peer) {
	updateTitle();
}

void AddParticipantsBoxController::prepareViewHook() {
	updateTitle();
}

int AddParticipantsBoxController::alreadyInCount() const {
	if (!_peer) {
		return 1; // self
	}
	if (const auto chat = _peer->asChat()) {
		return qMax(chat->count, 1);
	} else if (const auto channel = _peer->asChannel()) {
		return qMax(channel->membersCount(), int(_alreadyIn.size()));
	}
	Unexpected("User in AddParticipantsBoxController::alreadyInCount");
}

bool AddParticipantsBoxController::isAlreadyIn(
		not_null<UserData*> user) const {
	if (!_peer) {
		return false;
	}
	if (const auto chat = _peer->asChat()) {
		return _alreadyIn.contains(user)
			|| chat->participants.contains(user);
	} else if (const auto channel = _peer->asChannel()) {
		return _alreadyIn.contains(user)
			|| (channel->isMegagroup()
				&& base::contains(channel->mgInfo->lastParticipants, user));
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
	const auto additional = (_peer
		&& _peer->isChannel()
		&& !_peer->isMegagroup())
		? QString()
		: qsl("%1 / %2"
		).arg(fullCount()
		).arg(session().serverConfig().megagroupSizeMax);
	delegate()->peerListSetTitle(tr::lng_profile_add_participant());
	delegate()->peerListSetAdditionalTitle(rpl::single(additional));

	addInviteLinkButton();
}

bool AddParticipantsBoxController::needsInviteLinkButton() {
	if (!_peer) {
		return false;
	} else if (const auto channel = _peer->asChannel()) {
		return channel->canHaveInviteLink();
	}
	return _peer->asChat()->canHaveInviteLink();
}

void AddParticipantsBoxController::addInviteLinkButton() {
	if (!needsInviteLinkButton()) {
		return;
	}
	auto button = object_ptr<Ui::PaddingWrap<Ui::SettingsButton>>(
		nullptr,
		object_ptr<Ui::SettingsButton>(
			nullptr,
			tr::lng_profile_add_via_link(),
			st::inviteViaLinkButton),
		style::margins(0, st::membersMarginTop, 0, 0));
	object_ptr<Info::Profile::FloatingIcon>(
		button->entity(),
		st::inviteViaLinkIcon,
		st::inviteViaLinkIconPosition);
	button->entity()->setClickedCallback([=] {
		Ui::show(Box<EditPeerTypeBox>(_peer), Ui::LayerOption::KeepOther);
	});
	button->entity()->events(
	) | rpl::filter([=](not_null<QEvent*> e) {
		return (e->type() == QEvent::Enter);
	}) | rpl::start_with_next([=] {
		delegate()->peerListMouseLeftGeometry();
	}, button->lifetime());
	delegate()->peerListSetAboveWidget(std::move(button));
}

bool AddParticipantsBoxController::inviteSelectedUsers(
		not_null<PeerListBox*> box) const {
	Expects(_peer != nullptr);

	const auto rows = box->collectSelectedRows();
	const auto users = ranges::views::all(
		rows
	) | ranges::views::transform([](not_null<PeerData*> peer) {
		Expects(peer->isUser());
		Expects(!peer->isSelf());

		return not_null<UserData*>(peer->asUser());
	}) | ranges::to_vector;
	if (users.empty()) {
		return false;
	}
	_peer->session().api().addChatParticipants(_peer, users);
	return true;
}

void AddParticipantsBoxController::Start(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChatData*> chat) {
	auto controller = std::make_unique<AddParticipantsBoxController>(chat);
	const auto weak = controller.get();
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->addButton(tr::lng_participant_invite(), [=] {
			if (weak->inviteSelectedUsers(box)) {
				Ui::showPeerHistory(chat, ShowAtTheEndMsgId);
			}
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	};
	Ui::show(
		Box<PeerListBox>(
			std::move(controller),
			std::move(initBox)),
		Ui::LayerOption::KeepOther);
}

void AddParticipantsBoxController::Start(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel,
		base::flat_set<not_null<UserData*>> &&alreadyIn,
		bool justCreated) {
	auto controller = std::make_unique<AddParticipantsBoxController>(
		channel,
		std::move(alreadyIn));
	const auto weak = controller.get();
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->addButton(tr::lng_participant_invite(), [=] {
			if (weak->inviteSelectedUsers(box)) {
				if (channel->isMegagroup()) {
					Ui::showPeerHistory(channel, ShowAtTheEndMsgId);
				} else {
					box->closeBox();
				}
			}
		});
		box->addButton(
			justCreated ? tr::lng_create_group_skip() : tr::lng_cancel(),
			[=] { box->closeBox(); });
		if (justCreated) {
			box->boxClosing() | rpl::start_with_next([=] {
				auto params = Window::SectionShow();
				params.activation = anim::activation::background;
				navigation->parentController()->showPeerHistory(
					channel,
					params,
					ShowAtTheEndMsgId);
			}, box->lifetime());
		}
	};
	Ui::show(
		Box<PeerListBox>(
			std::move(controller),
			std::move(initBox)),
		Ui::LayerOption::KeepOther);
}

void AddParticipantsBoxController::Start(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel,
		base::flat_set<not_null<UserData*>> &&alreadyIn) {
	Start(navigation, channel, std::move(alreadyIn), false);
}

void AddParticipantsBoxController::Start(
		not_null<Window::SessionNavigation*> navigation,
		not_null<ChannelData*> channel) {
	Start(navigation, channel, {}, true);
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
, _api(&_peer->session().mtp())
, _role(role)
, _additional(peer, Role::Members)
, _adminDoneCallback(std::move(adminDoneCallback))
, _bannedDoneCallback(std::move(bannedDoneCallback)) {
	subscribeToMigration();
}

Main::Session &AddSpecialBoxController::session() const {
	return _peer->session();
}

void AddSpecialBoxController::subscribeToMigration() {
	const auto chat = _peer->asChat();
	if (!chat) {
		return;
	}
	SubscribeToMigration(
		chat,
		lifetime(),
		[=](not_null<ChannelData*> channel) { migrate(chat, channel); });
}

void AddSpecialBoxController::migrate(
		not_null<ChatData*> chat,
		not_null<ChannelData*> channel) {
	_peer = channel;
	_additional.migrate(chat, channel);
}

std::unique_ptr<PeerListRow> AddSpecialBoxController::createSearchRow(
		not_null<PeerData*> peer) {
	if (_excludeSelf && peer->isSelf()) {
		return nullptr;
	}
	if (const auto user = peer->asUser()) {
		return createRow(user);
	}
	return nullptr;
}

void AddSpecialBoxController::prepare() {
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	auto title = [&] {
		switch (_role) {
		case Role::Members:
			return tr::lng_profile_participants_section();
		case Role::Admins:
			return tr::lng_channel_add_admin();
		case Role::Restricted:
			return tr::lng_channel_add_exception();
		case Role::Kicked:
			return tr::lng_channel_add_removed();
		}
		Unexpected("Role in AddSpecialBoxController::prepare()");
	}();
	delegate()->peerListSetTitle(std::move(title));
	setDescriptionText(tr::lng_contacts_loading(tr::now));
	setSearchNoResultsText(tr::lng_blocked_list_not_found(tr::now));

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

	using UpdateFlag = Data::PeerUpdate::Flag;
	chat->session().changes().peerUpdates(
		chat,
		UpdateFlag::Members | UpdateFlag::Admins
	) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		_additional.fillFromPeer();
		if (update.flags & UpdateFlag::Members) {
			rebuildChatRows(chat);
		}
	}, lifetime());
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
		Assert(row->peer()->isUser());
		auto user = row->peer()->asUser();
		if (participants.contains(user)) {
			++i;
		} else {
			delegate()->peerListRemoveRow(row);
			--count;
		}
	}
	for (const auto user : participants) {
		if (auto row = createRow(user)) {
			delegate()->peerListAppendRow(std::move(row));
		}
	}
	_onlineSorter->sort();

	delegate()->peerListRefreshRows();
	setDescriptionText(QString());
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

	_loadRequestId = _api.request(MTPchannels_GetParticipants(
		channel->inputChannel,
		MTP_channelParticipantsRecent(),
		MTP_int(_offset),
		MTP_int(perPage),
		MTP_int(participantsHash)
	)).done([=](const MTPchannels_ChannelParticipants &result) {
		_loadRequestId = 0;
		auto &session = channel->session();
		session.api().parseChannelParticipants(channel, result, [&](
				int availableCount,
				const QVector<MTPChannelParticipant> &list) {
			for (const auto &data : list) {
				if (const auto participant = _additional.applyParticipant(
						data)) {
					appendRow(participant);
				}
			}
			if (const auto size = list.size()) {
				_offset += size;
			} else {
				// To be sure - wait for a whole empty result list.
				_allLoaded = true;
			}
		});

		if (delegate()->peerListFullRowsCount() > 0) {
			setDescriptionText(QString());
		} else if (_allLoaded) {
			setDescriptionText(tr::lng_blocked_list_not_found(tr::now));
		}
		delegate()->peerListRefreshRows();
	}).fail([this](const MTP::Error &error) {
		_loadRequestId = 0;
	}).send();
}

void AddSpecialBoxController::rowClicked(not_null<PeerListRow*> row) {
	const auto participant = row->peer();
	const auto user = participant->asUser();
	switch (_role) {
	case Role::Admins:
		Assert(user != nullptr);
		return showAdmin(user);
	case Role::Restricted:
		Assert(user != nullptr);
		return showRestricted(user);
	case Role::Kicked: return kickUser(participant);
	}
	Unexpected("Role in AddSpecialBoxController::rowClicked()");
}

template <typename Callback>
bool AddSpecialBoxController::checkInfoLoaded(
		not_null<PeerData*> participant,
		Callback callback) {
	if (_additional.infoLoaded(participant)) {
		return true;
	}

	// We don't know what this user status is in the group.
	const auto channel = _peer->asChannel();
	_api.request(MTPchannels_GetParticipant(
		channel->inputChannel,
		participant->input
	)).done([=](const MTPchannels_ChannelParticipant &result) {
		result.match([&](const MTPDchannels_channelParticipant &data) {
			channel->owner().processUsers(data.vusers());
			_additional.applyParticipant(data.vparticipant());
		});
		callback();
	}).fail([=](const MTP::Error &error) {
		_additional.setExternal(participant);
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
	_editBox = nullptr;
	if (_editParticipantBox) {
		_editParticipantBox->closeBox();
	}

	const auto chat = _peer->asChat();
	const auto channel = _peer->asChannel();
	const auto showAdminSure = crl::guard(this, [=] {
		showAdmin(user, true);
	});

	// Check restrictions.
	const auto canAddMembers = chat
		? chat->canAddMembers()
		: channel->canAddMembers();
	const auto canBanMembers = chat
		? chat->canBanMembers()
		: channel->canBanMembers();
	const auto adminRights = _additional.adminRights(user);
	if (adminRights.has_value()) {
		// The user is already an admin.
	} else if (_additional.isKicked(user)) {
		// The user is banned.
		if (canAddMembers) {
			if (canBanMembers) {
				if (!sure) {
					_editBox = Ui::show(
						Box<ConfirmBox>(
							tr::lng_sure_add_admin_unremove(tr::now),
							showAdminSure),
						Ui::LayerOption::KeepOther);
					return;
				}
			} else {
				Ui::show(Box<InformBox>(
					tr::lng_error_cant_add_admin_unban(tr::now)),
					Ui::LayerOption::KeepOther);
				return;
			}
		} else {
			Ui::show(Box<InformBox>(
				tr::lng_error_cant_add_admin_invite(tr::now)),
				Ui::LayerOption::KeepOther);
			return;
		}
	} else if (_additional.restrictedRights(user).has_value()) {
		// The user is restricted.
		if (canBanMembers) {
			if (!sure) {
				_editBox = Ui::show(
					Box<ConfirmBox>(
						tr::lng_sure_add_admin_unremove(tr::now),
						showAdminSure),
					Ui::LayerOption::KeepOther);
				return;
			}
		} else {
			Ui::show(Box<InformBox>(
				tr::lng_error_cant_add_admin_unban(tr::now)),
				Ui::LayerOption::KeepOther);
			return;
		}
	} else if (_additional.isExternal(user)) {
		// The user is not in the group yet.
		if (canAddMembers) {
			if (!sure) {
				const auto text = ((_peer->isChat() || _peer->isMegagroup())
					? tr::lng_sure_add_admin_invite
					: tr::lng_sure_add_admin_invite_channel)(tr::now);
				_editBox = Ui::show(
					Box<ConfirmBox>(
						text,
						showAdminSure),
					Ui::LayerOption::KeepOther);
				return;
			}
		} else {
			Ui::show(
				Box<InformBox>(tr::lng_error_cant_add_admin_invite(tr::now)),
				Ui::LayerOption::KeepOther);
			return;
		}
	}

	// Finally show the admin.
	const auto currentRights = adminRights
		? *adminRights
		: MTPChatAdminRights(MTP_chatAdminRights(MTP_flags(0)));
	auto box = Box<EditAdminBox>(
		_peer,
		user,
		currentRights,
		_additional.adminRank(user));
	if (_additional.canAddOrEditAdmin(user)) {
		const auto done = crl::guard(this, [=](
				const MTPChatAdminRights &newRights,
				const QString &rank) {
			editAdminDone(user, newRights, rank);
		});
		const auto fail = crl::guard(this, [=] {
			if (_editParticipantBox) {
				_editParticipantBox->closeBox();
			}
		});
		box->setSaveCallback(SaveAdminCallback(_peer, user, done, fail));
	}
	_editParticipantBox = Ui::show(std::move(box), Ui::LayerOption::KeepOther);
}

void AddSpecialBoxController::editAdminDone(
		not_null<UserData*> user,
		const MTPChatAdminRights &rights,
		const QString &rank) {
	if (_editParticipantBox) {
		_editParticipantBox->closeBox();
	}

	const auto date = base::unixtime::now(); // Incorrect, but ignored.
	if (_additional.isCreator(user) && user->isSelf()) {
		using Flag = MTPDchannelParticipantCreator::Flag;
		_additional.applyParticipant(MTP_channelParticipantCreator(
			MTP_flags(rank.isEmpty() ? Flag(0) : Flag::f_rank),
			MTP_int(user->bareId()),
			rights,
			MTP_string(rank)));
	} else if (rights.c_chatAdminRights().vflags().v == 0) {
		_additional.applyParticipant(MTP_channelParticipant(
			MTP_int(user->bareId()),
			MTP_int(date)));
	} else {
		using Flag = MTPDchannelParticipantAdmin::Flag;
		const auto alreadyPromotedBy = _additional.adminPromotedBy(user);
		_additional.applyParticipant(MTP_channelParticipantAdmin(
			MTP_flags(Flag::f_can_edit
				| (rank.isEmpty() ? Flag(0) : Flag::f_rank)),
			MTP_int(user->bareId()),
			MTPint(), // inviter_id
			MTP_int(alreadyPromotedBy
				? alreadyPromotedBy->bareId()
				: user->session().userId()),
			MTP_int(date),
			rights,
			MTP_string(rank)));
	}
	if (const auto callback = _adminDoneCallback) {
		callback(user, rights, rank);
	}
}

void AddSpecialBoxController::showRestricted(
		not_null<UserData*> user,
		bool sure) {
	if (!checkInfoLoaded(user, [=] { showRestricted(user); })) {
		return;
	}
	_editBox = nullptr;
	if (_editParticipantBox) {
		_editParticipantBox->closeBox();
	}

	const auto showRestrictedSure = crl::guard(this, [=] {
		showRestricted(user, true);
	});

	// Check restrictions.
	const auto restrictedRights = _additional.restrictedRights(user);
	if (restrictedRights.has_value()) {
		// The user is already banned or restricted.
	} else if (_additional.adminRights(user).has_value()
		|| _additional.isCreator(user)) {
		// The user is an admin or creator.
		if (!_additional.isCreator(user) && _additional.canEditAdmin(user)) {
			if (!sure) {
				_editBox = Ui::show(
					Box<ConfirmBox>(
						tr::lng_sure_ban_admin(tr::now),
						showRestrictedSure),
					Ui::LayerOption::KeepOther);
				return;
			}
		} else {
			Ui::show(
				Box<InformBox>(tr::lng_error_cant_ban_admin(tr::now)),
				Ui::LayerOption::KeepOther);
			return;
		}
	}

	// Finally edit the restricted.
	const auto currentRights = restrictedRights
		? *restrictedRights
		: ChannelData::EmptyRestrictedRights(user);
	auto box = Box<EditRestrictedBox>(
		_peer,
		user,
		_additional.adminRights(user).has_value(),
		currentRights);
	if (_additional.canRestrictParticipant(user)) {
		const auto done = crl::guard(this, [=](
				const MTPChatBannedRights &newRights) {
			editRestrictedDone(user, newRights);
		});
		const auto fail = crl::guard(this, [=] {
			if (_editParticipantBox) {
				_editParticipantBox->closeBox();
			}
		});
		box->setSaveCallback(
			SaveRestrictedCallback(_peer, user, done, fail));
	}
	_editParticipantBox = Ui::show(std::move(box), Ui::LayerOption::KeepOther);
}

void AddSpecialBoxController::editRestrictedDone(
		not_null<PeerData*> participant,
		const MTPChatBannedRights &rights) {
	if (_editParticipantBox) {
		_editParticipantBox->closeBox();
	}

	const auto date = base::unixtime::now(); // Incorrect, but ignored.
	if (Data::ChatBannedRightsFlags(rights) == 0) {
		if (const auto user = participant->asUser()) {
			_additional.applyParticipant(MTP_channelParticipant(
				MTP_int(user->bareId()),
				MTP_int(date)));
		} else {
			_additional.setExternal(participant);
		}
	} else {
		const auto kicked = Data::ChatBannedRightsFlags(rights)
			& ChatRestriction::f_view_messages;
		const auto alreadyRestrictedBy = _additional.restrictedBy(
			participant);
		_additional.applyParticipant(MTP_channelParticipantBanned(
			MTP_flags(kicked
				? MTPDchannelParticipantBanned::Flag::f_left
				: MTPDchannelParticipantBanned::Flag(0)),
			(participant->isUser()
				? MTP_peerUser(MTP_int(participant->bareId()))
				: participant->isChat()
				? MTP_peerChat(MTP_int(participant->bareId()))
				: MTP_peerChannel(MTP_int(participant->bareId()))),
			MTP_int(alreadyRestrictedBy
				? alreadyRestrictedBy->bareId()
				: participant->session().userId()),
			MTP_int(date),
			rights));
	}
	if (const auto callback = _bannedDoneCallback) {
		callback(participant, rights);
	}
}

void AddSpecialBoxController::kickUser(
		not_null<PeerData*> participant,
		bool sure) {
	if (!checkInfoLoaded(participant, [=] { kickUser(participant); })) {
		return;
	}

	const auto kickUserSure = crl::guard(this, [=] {
		kickUser(participant, true);
	});

	// Check restrictions.
	const auto user = participant->asUser();
	if (user && (_additional.adminRights(user).has_value()
		|| (_additional.isCreator(user)))) {
		// The user is an admin or creator.
		if (!_additional.isCreator(user) && _additional.canEditAdmin(user)) {
			if (!sure) {
				_editBox = Ui::show(
					Box<ConfirmBox>(
						tr::lng_sure_ban_admin(tr::now),
						kickUserSure),
					Ui::LayerOption::KeepOther);
				return;
			}
		} else {
			Ui::show(
				Box<InformBox>(tr::lng_error_cant_ban_admin(tr::now)),
				Ui::LayerOption::KeepOther);
			return;
		}
	}

	// Finally kick him.
	if (!sure) {
		const auto text = ((_peer->isChat() || _peer->isMegagroup())
			? tr::lng_profile_sure_kick
			: tr::lng_profile_sure_kick_channel)(
				tr::now,
				lt_user,
				participant->name);
		_editBox = Ui::show(
			Box<ConfirmBox>(text, kickUserSure),
			Ui::LayerOption::KeepOther);
		return;
	}

	const auto restrictedRights = _additional.restrictedRights(participant);
	const auto currentRights = restrictedRights
		? *restrictedRights
		: ChannelData::EmptyRestrictedRights(participant);

	const auto done = crl::guard(this, [=](
			const MTPChatBannedRights &newRights) {
		editRestrictedDone(participant, newRights);
	});
	const auto fail = crl::guard(this, [=] {
		_editBox = nullptr;
	});
	const auto callback = SaveRestrictedCallback(
		_peer,
		participant,
		done,
		fail);
	callback(currentRights, ChannelData::KickedRestrictedRights(participant));
}

bool AddSpecialBoxController::appendRow(not_null<PeerData*> participant) {
	if (delegate()->peerListFindRow(participant->id)
		|| (_excludeSelf && participant->isSelf())) {
		return false;
	}
	delegate()->peerListAppendRow(createRow(participant));
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
		not_null<PeerData*> participant) const {
	return std::make_unique<PeerListRow>(participant);
}

AddSpecialBoxSearchController::AddSpecialBoxSearchController(
	not_null<PeerData*> peer,
	not_null<ParticipantsAdditionalData*> additional)
: _peer(peer)
, _additional(additional)
, _api(&_peer->session().mtp())
, _timer([=] { searchOnServer(); }) {
	subscribeToMigration();
}

void AddSpecialBoxSearchController::subscribeToMigration() {
	SubscribeToMigration(
		_peer,
		lifetime(),
		[=](not_null<ChannelData*> channel) { _peer = channel; });
}

void AddSpecialBoxSearchController::searchQuery(const QString &query) {
	if (_query != query) {
		_query = query;
		_offset = 0;
		_requestId = 0;
		_participantsLoaded = false;
		_chatsContactsAdded = false;
		_chatMembersAdded = false;
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
	const auto i = _participantsCache.find(_query);
	if (i != _participantsCache.cend()) {
		_requestId = 0;
		searchParticipantsDone(
			_requestId,
			i->second.result,
			i->second.requestedCount);
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
	if (_participantsLoaded || _chatMembersAdded) {
		if (!_chatsContactsAdded) {
			addChatsContacts();
		}
		if (!isLoading() && !searchGlobalInCache()) {
			requestGlobal();
		}
	} else if (const auto chat = _peer->asChat()) {
		if (!_chatMembersAdded) {
			addChatMembers(chat);
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

	_requestId = _api.request(MTPchannels_GetParticipants(
		channel->inputChannel,
		MTP_channelParticipantsSearch(MTP_string(_query)),
		MTP_int(_offset),
		MTP_int(perPage),
		MTP_int(participantsHash)
	)).done([=](
			const MTPchannels_ChannelParticipants &result,
			mtpRequestId requestId) {
		searchParticipantsDone(requestId, result, perPage);
	}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
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
		const auto addToCache = [&](auto&&...) {
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
		};
		channel->session().api().parseChannelParticipants(
			channel,
			result,
			addToCache);
	}

	if (_requestId != requestId) {
		return;
	}
	_requestId = 0;
	result.match([&](const MTPDchannels_channelParticipants &data) {
		const auto &list = data.vparticipants().v;
		if (list.size() < requestedCount) {
			// We want cache to have full information about a query with
			// small results count (that we don't need the second request).
			// So we don't wait for empty list unlike the non-search case.
			_participantsLoaded = true;
			if (list.empty() && _offset == 0) {
				// No results, request global search immediately.
				loadMoreRows();
			}
		}
		for (const auto &data : list) {
			if (const auto user = _additional->applyParticipant(data)) {
				delegate()->peerListSearchAddRow(user);
			}
		}
		_offset += list.size();
	}, [&](const MTPDchannels_channelParticipantsNotModified &) {
		_participantsLoaded = true;
	});

	delegate()->peerListSearchRefreshRows();
}

void AddSpecialBoxSearchController::requestGlobal() {
	if (_query.isEmpty()) {
		_globalLoaded = true;
		return;
	}

	auto perPage = SearchPeopleLimit;
	_requestId = _api.request(MTPcontacts_Search(
		MTP_string(_query),
		MTP_int(perPage)
	)).done([=](const MTPcontacts_Found &result, mtpRequestId requestId) {
		searchGlobalDone(requestId, result);
	}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
		if (_requestId == requestId) {
			_requestId = 0;
			_globalLoaded = true;
			delegate()->peerListSearchRefreshRows();
		}
	}).send();
	_globalQueries.emplace(_requestId, _query);
}

void AddSpecialBoxSearchController::searchGlobalDone(
		mtpRequestId requestId,
		const MTPcontacts_Found &result) {
	Expects(result.type() == mtpc_contacts_found);

	auto &found = result.c_contacts_found();
	auto query = _query;
	if (requestId) {
		_peer->owner().processUsers(found.vusers());
		_peer->owner().processChats(found.vchats());
		auto it = _globalQueries.find(requestId);
		if (it != _globalQueries.cend()) {
			query = it->second;
			_globalCache[query] = result;
			_globalQueries.erase(it);
		}
	}

	const auto feedList = [&](const MTPVector<MTPPeer> &list) {
		for (const auto &mtpPeer : list.v) {
			const auto peerId = peerFromMTP(mtpPeer);
			if (const auto peer = _peer->owner().peerLoaded(peerId)) {
				if (const auto user = peer->asUser()) {
					_additional->checkForLoaded(user);
					delegate()->peerListSearchAddRow(user);
				}
			}
		}
	};
	if (_requestId == requestId) {
		_requestId = 0;
		_globalLoaded = true;
		feedList(found.vmy_results());
		feedList(found.vresults());
		delegate()->peerListSearchRefreshRows();
	}
}

void AddSpecialBoxSearchController::addChatMembers(
		not_null<ChatData*> chat) {
	if (chat->participants.empty()) {
		return;
	}

	_chatMembersAdded = true;
	const auto wordList = TextUtilities::PrepareSearchWords(_query);
	if (wordList.empty()) {
		return;
	}
	const auto allWordsAreFound = [&](
			const base::flat_set<QString> &nameWords) {
		const auto hasNamePartStartingWith = [&](const QString &word) {
			for (const auto &nameWord : nameWords) {
				if (nameWord.startsWith(word)) {
					return true;
				}
			}
			return false;
		};

		for (const auto &word : wordList) {
			if (!hasNamePartStartingWith(word)) {
				return false;
			}
		}
		return true;
	};

	for (const auto user : chat->participants) {
		if (allWordsAreFound(user->nameWords())) {
			delegate()->peerListSearchAddRow(user);
		}
	}
	delegate()->peerListSearchRefreshRows();
}

void AddSpecialBoxSearchController::addChatsContacts() {
	_chatsContactsAdded = true;
	const auto wordList = TextUtilities::PrepareSearchWords(_query);
	if (wordList.empty()) {
		return;
	}
	const auto allWordsAreFound = [&](
			const base::flat_set<QString> &nameWords) {
		const auto hasNamePartStartingWith = [&](const QString &word) {
			for (const auto &nameWord : nameWords) {
				if (nameWord.startsWith(word)) {
					return true;
				}
			}
			return false;
		};

		for (const auto &word : wordList) {
			if (!hasNamePartStartingWith(word)) {
				return false;
			}
		}
		return true;
	};
	const auto getSmallestIndex = [&](not_null<Dialogs::IndexedList*> list)
	-> const Dialogs::List* {
		if (list->empty()) {
			return nullptr;
		}

		auto result = (const Dialogs::List*)nullptr;
		for (const auto &word : wordList) {
			const auto found = list->filtered(word[0]);
			if (!found || found->empty()) {
				return nullptr;
			}
			if (!result || result->size() > found->size()) {
				result = found;
			}
		}
		return result;
	};
	const auto filterAndAppend = [&](not_null<Dialogs::IndexedList*> list) {
		const auto index = getSmallestIndex(list);
		if (!index) {
			return;
		}
		for (const auto row : *index) {
			if (const auto history = row->history()) {
				if (const auto user = history->peer->asUser()) {
					if (allWordsAreFound(user->nameWords())) {
						delegate()->peerListSearchAddRow(user);
					}
				}
			}
		}
	};
	filterAndAppend(_peer->owner().chatsList()->indexed());
	const auto id = Data::Folder::kId;
	if (const auto folder = _peer->owner().folderLoaded(id)) {
		filterAndAppend(folder->chatsList()->indexed());
	}
	filterAndAppend(_peer->owner().contactsNoChatsList());
	delegate()->peerListSearchRefreshRows();
}
