/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/add_bot_to_chat_box.h"

#include "lang/lang_keys.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_histories.h"
#include "history/history.h"
#include "main/main_session.h"
#include "boxes/peers/edit_participant_box.h"
#include "boxes/peers/edit_participants_box.h"
#include "boxes/filters/edit_filter_chats_list.h"
#include "ui/boxes/confirm_box.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "base/random.h"
#include "base/weak_ptr.h"
#include "api/api_chat_participants.h"
#include "apiwrap.h"
#include "facades.h"
#include "styles/style_boxes.h"

namespace {

class Controller final
	: public PeerListController
	, public base::has_weak_ptr {
public:
	Controller(
		not_null<Main::Session*> session,
		rpl::producer<not_null<PeerData*>> add,
		Fn<void(not_null<PeerData*> chat)> callback);

	Main::Session &session() const override;
	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;

private:
	void addRow(not_null<PeerData*> peer);

	const not_null<Main::Session*> _session;
	Fn<void(not_null<PeerData*> chat)> _callback;
	std::vector<not_null<PeerData*>> _list;
	bool _prepared = false;
	bool _refreshing = false;

	rpl::lifetime _lifetime;

};

void ShareBotGame(
		not_null<UserData*> bot,
		not_null<PeerData*> chat,
		const QString &shortName) {
	const auto history = chat->owner().history(chat);
	auto &histories = history->owner().histories();
	const auto requestType = Data::Histories::RequestType::Send;
	histories.sendRequest(history, requestType, [=](Fn<void()> finish) {
		const auto randomId = base::RandomValue<uint64>();
		const auto api = &chat->session().api();
		history->sendRequestId = api->request(MTPmessages_SendMedia(
			MTP_flags(0),
			chat->input,
			MTP_int(0),
			MTP_inputMediaGame(
				MTP_inputGameShortName(
					bot->inputUser,
					MTP_string(shortName))),
			MTP_string(),
			MTP_long(randomId),
			MTPReplyMarkup(),
			MTPVector<MTPMessageEntity>(),
			MTP_int(0), // schedule_date
			MTPInputPeer() // send_as
		)).done([=](const MTPUpdates &result) {
			api->applyUpdates(result, randomId);
			finish();
		}).fail([=](const MTP::Error &error) {
			api->sendMessageFail(error, chat);
			finish();
		}).afterRequest(
			history->sendRequestId
		).send();
		return history->sendRequestId;
	});
	Ui::hideLayer();
	Ui::showPeerHistory(chat, ShowAtUnreadMsgId);
}

Controller::Controller(
	not_null<Main::Session*> session,
	rpl::producer<not_null<PeerData*>> add,
	Fn<void(not_null<PeerData*> chat)> callback)
: _session(session)
, _callback(std::move(callback)) {
	std::move(
		add
	) | rpl::start_with_next([=](not_null<PeerData*> peer) {
		if (_prepared) {
			addRow(peer);
		} else {
			_list.push_back(peer);
		}
	}, _lifetime);
}

Main::Session &Controller::session() const {
	return *_session;
}

void Controller::prepare() {
	_prepared = true;
	for (const auto &peer : _list) {
		addRow(peer);
	}
}

void Controller::rowClicked(not_null<PeerListRow*> row) {
	_callback(row->peer());
}

void Controller::addRow(not_null<PeerData*> peer) {
	if (delegate()->peerListFindRow(peer->id.value)) {
		return;
	}
	delegate()->peerListAppendRow(std::make_unique<PeerListRow>(peer));
	if (!_refreshing) {
		_refreshing = true;
		Ui::PostponeCall(this, [=] {
			_refreshing = false;
			delegate()->peerListRefreshRows();
		});
	}
}

} // namespace

void AddBotToGroupBoxController::Start(
		not_null<UserData*> bot,
		Scope scope,
		const QString &token,
		ChatAdminRights requestedRights) {
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->addButton(tr::lng_cancel(), [box] { box->closeBox(); });
	};
	Ui::show(Box<PeerListBox>(
		std::make_unique<AddBotToGroupBoxController>(
			bot,
			scope,
			token,
			requestedRights),
		std::move(initBox)));
}

AddBotToGroupBoxController::AddBotToGroupBoxController(
	not_null<UserData*> bot,
	Scope scope,
	const QString &token,
	ChatAdminRights requestedRights)
: ChatsListBoxController((scope == Scope::ShareGame)
	? std::make_unique<PeerListGlobalSearchController>(&bot->session())
	: nullptr)
, _bot(bot)
, _scope(scope)
, _token(token)
, _requestedRights(requestedRights)
, _adminToGroup((scope == Scope::GroupAdmin)
	|| (scope == Scope::All && _bot->botInfo->groupAdminRights != 0))
, _adminToChannel((scope == Scope::ChannelAdmin)
	|| (scope == Scope::All && _bot->botInfo->channelAdminRights != 0))
, _memberToGroup(scope == Scope::All) {
}

Main::Session &AddBotToGroupBoxController::session() const {
	return _bot->session();
}

void AddBotToGroupBoxController::rowClicked(not_null<PeerListRow*> row) {
	if (sharingBotGame()) {
		shareBotGame(row->peer());
	} else {
		addBotToGroup(row->peer());
	}
}

void AddBotToGroupBoxController::shareBotGame(not_null<PeerData*> chat) {
	auto send = crl::guard(this, [bot = _bot, chat, token = _token] {
		ShareBotGame(bot, chat, token);
	});
	auto confirmText = [chat] {
		if (chat->isUser()) {
			return tr::lng_bot_sure_share_game(tr::now, lt_user, chat->name);
		}
		return tr::lng_bot_sure_share_game_group(tr::now, lt_group, chat->name);
	}();
	Ui::show(
		Ui::MakeConfirmBox({
			.text = confirmText,
			.confirmed = std::move(send),
		}),
		Ui::LayerOption::KeepOther);
}

void AddBotToGroupBoxController::requestExistingRights(
		not_null<ChannelData*> channel) {
	if (_existingRightsChannel == channel) {
		return;
	}
	_existingRightsChannel = channel;
	_bot->session().api().request(_existingRightsRequestId).cancel();
	_existingRightsRequestId = _bot->session().api().request(
		MTPchannels_GetParticipant(
			_existingRightsChannel->inputChannel,
			_bot->input)
	).done([=](const MTPchannels_ChannelParticipant &result) {
		result.match([&](const MTPDchannels_channelParticipant &data) {
			channel->owner().processUsers(data.vusers());
			const auto participant = Api::ChatParticipant(
				data.vparticipant(),
				channel);
			_existingRights = participant.rights().flags;
			_existingRank = participant.rank();
			addBotToGroup(_existingRightsChannel);
		});
	}).fail([=] {
		_existingRights = ChatAdminRights();
		_existingRank = QString();
		addBotToGroup(_existingRightsChannel);
	}).send();
}

void AddBotToGroupBoxController::addBotToGroup(not_null<PeerData*> chat) {
	if (const auto megagroup = chat->asMegagroup()) {
		if (!megagroup->canAddMembers()) {
			Ui::show(
				Ui::MakeInformBox(tr::lng_error_cant_add_member()),
				Ui::LayerOption::KeepOther);
			return;
		}
	}
	if (_existingRightsChannel != chat) {
		_existingRights = {};
		_existingRank = QString();
		_existingRightsChannel = nullptr;
		_bot->session().api().request(_existingRightsRequestId).cancel();
	}
	const auto requestedAddAdmin = (_scope == Scope::GroupAdmin)
		|| (_scope == Scope::ChannelAdmin);
	if (chat->isChannel()
		&& requestedAddAdmin
		&& !_existingRights.has_value()) {
		requestExistingRights(chat->asChannel());
		return;
	}
	const auto bot = _bot;
	const auto close = [=](auto&&...) {
		Ui::hideLayer();
		Ui::showPeerHistory(chat, ShowAtUnreadMsgId);
	};
	const auto rights = requestedAddAdmin
		? _requestedRights
		: (chat->isBroadcast()
			&& chat->asBroadcast()->canAddAdmins())
		? bot->botInfo->channelAdminRights
		: ((chat->isMegagroup() && chat->asMegagroup()->canAddAdmins())
			|| (chat->isChat() && chat->asChat()->canAddAdmins()))
		? bot->botInfo->groupAdminRights
		: ChatAdminRights();
	const auto addingAdmin = requestedAddAdmin || (rights != 0);
	if (addingAdmin) {
		const auto scope = _scope;
		const auto token = _token;
		const auto done = [=](
				ChatAdminRightsInfo newRights,
				const QString &rank) {
			if (scope == Scope::GroupAdmin) {
				chat->session().api().sendBotStart(bot, chat, token);
			}
			close();
		};
		const auto saveCallback = SaveAdminCallback(
			chat,
			bot,
			done,
			close);
		auto box = Box<EditAdminBox>(
			chat,
			bot,
			ChatAdminRightsInfo(rights),
			_existingRank,
			EditAdminBotFields{
				_token,
				_existingRights.value_or(ChatAdminRights()) });
		box->setSaveCallback(saveCallback);
		Ui::show(std::move(box), Ui::LayerOption::KeepOther);
	} else {
		Ui::show(
			Ui::MakeConfirmBox({
				tr::lng_bot_sure_invite(tr::now, lt_group, chat->name),
				crl::guard(this, [=] { AddBotToGroup(bot, chat, _token); }),
			}),
			Ui::LayerOption::KeepOther);
	}
}

auto AddBotToGroupBoxController::createRow(not_null<History*> history)
-> std::unique_ptr<ChatsListBoxController::Row> {
	if (!needToCreateRow(history->peer)) {
		return nullptr;
	}
	return std::make_unique<Row>(history);
}

bool AddBotToGroupBoxController::needToCreateRow(
		not_null<PeerData*> peer) const {
	if (sharingBotGame()) {
		if (!peer->canWrite()
			|| peer->amRestricted(ChatRestriction::SendGames)) {
			return false;
		}
		return true;
	}
	if (const auto chat = peer->asChat()) {
		if (onlyAdminToGroup()) {
			return chat->canAddAdmins();
		} else if (_adminToGroup && chat->canAddAdmins()) {
			_groups.fire_copy(peer);
		} else if (!onlyAdminToChannel()) {
			return chat->canAddMembers();
		}
	} else if (const auto group = peer->asMegagroup()) {
		if (onlyAdminToGroup()) {
			return group->canAddAdmins();
		} else if (_adminToGroup && group->canAddAdmins()) {
			_groups.fire_copy(peer);
		} else if (!onlyAdminToChannel()) {
			return group->canAddMembers();
		}
	} else if (const auto channel = peer->asBroadcast()) {
		if (onlyAdminToChannel()) {
			return channel->canAddAdmins();
		} else if (_adminToChannel && channel->canAddAdmins()) {
			_channels.fire_copy(peer);
		}
	}
	return false;
}

bool AddBotToGroupBoxController::sharingBotGame() const {
	return (_scope == Scope::ShareGame);
}

QString AddBotToGroupBoxController::emptyBoxText() const {
	return !session().data().chatsListLoaded()
		? tr::lng_contacts_loading(tr::now)
		: (sharingBotGame() || _adminToChannel)
		? tr::lng_bot_no_chats(tr::now)
		: tr::lng_bot_no_groups(tr::now);
}

QString AddBotToGroupBoxController::noResultsText() const {
	return !session().data().chatsListLoaded()
		? tr::lng_contacts_loading(tr::now)
		: (sharingBotGame() || _adminToChannel)
		? tr::lng_bot_chats_not_found(tr::now)
		: tr::lng_bot_groups_not_found(tr::now);
}

void AddBotToGroupBoxController::updateLabels() {
	setSearchNoResultsText(noResultsText());
}

object_ptr<Ui::RpWidget> AddBotToGroupBoxController::prepareAdminnedChats() {
	auto result = object_ptr<Ui::VerticalLayout>((QWidget*)nullptr);
	const auto container = result.data();

	const auto callback = [=](not_null<PeerData*> chat) {
		addBotToGroup(chat);
	};

	const auto addList = [&](
			tr::phrase<> subtitle,
			rpl::event_stream<not_null<PeerData*>> &items) {
		const auto wrap = container->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				container,
				object_ptr<Ui::VerticalLayout>(container)));
		wrap->hide(anim::type::instant);

		const auto inner = wrap->entity();
		inner->add(CreatePeerListSectionSubtitle(inner, subtitle()));

		const auto delegate = inner->lifetime().make_state<
			PeerListContentDelegateSimple
		>();
		const auto controller = inner->lifetime().make_state<Controller>(
			&session(),
			items.events(),
			callback);
		const auto content = inner->add(object_ptr<PeerListContent>(
			container,
			controller));
		delegate->setContent(content);
		controller->setDelegate(delegate);

		items.events() | rpl::take(1) | rpl::start_with_next([=] {
			wrap->show(anim::type::instant);
		}, inner->lifetime());
	};
	if (_adminToChannel) {
		addList(tr::lng_bot_channels_manage, _channels);
	}
	if (_adminToGroup) {
		addList(tr::lng_bot_groups_manage, _groups);
	}

	rpl::merge(
		_groups.events(),
		_channels.events()
	) | rpl::take(1) | rpl::start_with_next([=] {
		container->add(CreatePeerListSectionSubtitle(
			container,
			tr::lng_bot_groups()));
	}, container->lifetime());

	return result;
}

bool AddBotToGroupBoxController::onlyAdminToGroup() const {
	return _adminToGroup && !_memberToGroup && !_adminToChannel;
}

bool AddBotToGroupBoxController::onlyAdminToChannel() const {
	return _adminToChannel && !_memberToGroup && !_adminToGroup;
}

void AddBotToGroupBoxController::prepareViewHook() {
	delegate()->peerListSetTitle((sharingBotGame() || _adminToChannel)
		? tr::lng_bot_choose_chat()
		: tr::lng_bot_choose_group());
	if ((_adminToGroup && !onlyAdminToGroup())
		|| (_adminToChannel && !onlyAdminToChannel())) {
		delegate()->peerListSetAboveWidget(prepareAdminnedChats());
	}

	updateLabels();
	session().data().chatsListLoadedEvents(
	) | rpl::filter([=](Data::Folder *folder) {
		return !folder;
	}) | rpl::start_with_next([=] {
		updateLabels();
	}, lifetime());
}

void AddBotToGroup(
		not_null<UserData*> bot,
		not_null<PeerData*> chat,
		const QString &startToken) {
	if (!startToken.isEmpty()) {
		chat->session().api().sendBotStart(bot, chat, startToken);
	} else {
		chat->session().api().chatParticipants().add(chat, { 1, bot });
	}
	Ui::hideLayer();
	Ui::showPeerHistory(chat, ShowAtUnreadMsgId);
}
