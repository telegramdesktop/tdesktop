/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peer_list_controllers.h"

#include "boxes/confirm_box.h"
#include "observer_peer.h"
#include "ui/widgets/checkbox.h"
#include "auth_session.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "lang/lang_keys.h"
#include "history/history.h"
#include "dialogs/dialogs_indexed_list.h"
#include "styles/style_boxes.h"
#include "styles/style_profile.h"

namespace {

void ShareBotGame(not_null<UserData*> bot, not_null<PeerData*> chat) {
	const auto history = App::historyLoaded(chat);
	const auto randomId = rand_value<uint64>();
	const auto requestId = MTP::send(
		MTPmessages_SendMedia(
			MTP_flags(0),
			chat->input,
			MTP_int(0),
			MTP_inputMediaGame(
				MTP_inputGameShortName(
					bot->inputUser,
					MTP_string(bot->botInfo->shareGameShortName))),
			MTP_string(""),
			MTP_long(randomId),
			MTPnullMarkup,
			MTPnullEntities),
		App::main()->rpcDone(&MainWidget::sentUpdatesReceived),
		App::main()->rpcFail(&MainWidget::sendMessageFail),
		0,
		0,
		history ? history->sendRequestId : 0);
	if (history) {
		history->sendRequestId = requestId;
	}
	Ui::hideLayer();
	Ui::showPeerHistory(chat, ShowAtUnreadMsgId);
}

void AddBotToGroup(not_null<UserData*> bot, not_null<PeerData*> chat) {
	if (bot->botInfo && !bot->botInfo->startGroupToken.isEmpty()) {
		Auth().api().sendBotStart(bot, chat);
	} else {
		Auth().api().addChatParticipants(chat, { 1, bot });
	}
	Ui::hideLayer();
	Ui::showPeerHistory(chat, ShowAtUnreadMsgId);
}

} // namespace

// Not used for now.
//
//MembersAddButton::MembersAddButton(QWidget *parent, const style::TwoIconButton &st) : RippleButton(parent, st.ripple)
//, _st(st) {
//	resize(_st.width, _st.height);
//	setCursor(style::cur_pointer);
//}
//
//void MembersAddButton::paintEvent(QPaintEvent *e) {
//	Painter p(this);
//
//	auto ms = getms();
//	auto over = isOver();
//	auto down = isDown();
//
//	((over || down) ? _st.iconBelowOver : _st.iconBelow).paint(p, _st.iconPosition, width());
//	paintRipple(p, _st.rippleAreaPosition.x(), _st.rippleAreaPosition.y(), ms);
//	((over || down) ? _st.iconAboveOver : _st.iconAbove).paint(p, _st.iconPosition, width());
//}
//
//QImage MembersAddButton::prepareRippleMask() const {
//	return Ui::RippleAnimation::ellipseMask(QSize(_st.rippleAreaSize, _st.rippleAreaSize));
//}
//
//QPoint MembersAddButton::prepareRippleStartPosition() const {
//	return mapFromGlobal(QCursor::pos()) - _st.rippleAreaPosition;
//}

void PeerListRowWithLink::setActionLink(const QString &action) {
	_action = action;
	refreshActionLink();
}

void PeerListRowWithLink::refreshActionLink() {
	if (!isInitialized()) return;
	_actionWidth = _action.isEmpty() ? 0 : st::normalFont->width(_action);
}

void PeerListRowWithLink::lazyInitialize(const style::PeerListItem &st) {
	PeerListRow::lazyInitialize(st);
	refreshActionLink();
}

QSize PeerListRowWithLink::actionSize() const {
	return QSize(_actionWidth, st::normalFont->height);
}

QMargins PeerListRowWithLink::actionMargins() const {
	return QMargins(
		st::contactsCheckPosition.x(),
		(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom() - st::normalFont->height) / 2,
		st::defaultPeerListItem.photoPosition.x() + st::contactsCheckPosition.x(),
		0);
}

void PeerListRowWithLink::paintAction(
		Painter &p,
		TimeMs ms,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	p.setFont(actionSelected ? st::linkOverFont : st::linkFont);
	p.setPen(actionSelected ? st::defaultLinkButton.overColor : st::defaultLinkButton.color);
	p.drawTextLeft(x, y, outerWidth, _action, _actionWidth);
}

PeerListGlobalSearchController::PeerListGlobalSearchController() {
	_timer.setCallback([this] { searchOnServer(); });
}

void PeerListGlobalSearchController::searchQuery(const QString &query) {
	if (_query != query) {
		_query = query;
		_requestId = 0;
		if (!_query.isEmpty() && !searchInCache()) {
			_timer.callOnce(AutoSearchTimeout);
		} else {
			_timer.cancel();
		}
	}
}

bool PeerListGlobalSearchController::searchInCache() {
	auto it = _cache.find(_query);
	if (it != _cache.cend()) {
		_requestId = 0;
		searchDone(it->second, _requestId);
		return true;
	}
	return false;
}

void PeerListGlobalSearchController::searchOnServer() {
	_requestId = request(MTPcontacts_Search(
		MTP_string(_query),
		MTP_int(SearchPeopleLimit)
	)).done([=](const MTPcontacts_Found &result, mtpRequestId requestId) {
		searchDone(result, requestId);
	}).fail([=](const RPCError &error, mtpRequestId requestId) {
		if (_requestId == requestId) {
			_requestId = 0;
			delegate()->peerListSearchRefreshRows();
		}
	}).send();
	_queries.emplace(_requestId, _query);
}

void PeerListGlobalSearchController::searchDone(
		const MTPcontacts_Found &result,
		mtpRequestId requestId) {
	Expects(result.type() == mtpc_contacts_found);

	auto &contacts = result.c_contacts_found();
	auto query = _query;
	if (requestId) {
		App::feedUsers(contacts.vusers);
		App::feedChats(contacts.vchats);
		auto it = _queries.find(requestId);
		if (it != _queries.cend()) {
			query = it->second;
			_cache[query] = result;
			_queries.erase(it);
		}
	}
	const auto feedList = [&](const MTPVector<MTPPeer> &list) {
		for (const auto &mtpPeer : list.v) {
			if (const auto peer = App::peerLoaded(peerFromMTP(mtpPeer))) {
				delegate()->peerListSearchAddRow(peer);
			}
		}
	};
	if (_requestId == requestId) {
		_requestId = 0;
		feedList(contacts.vmy_results);
		feedList(contacts.vresults);
		delegate()->peerListSearchRefreshRows();
	}
}

bool PeerListGlobalSearchController::isLoading() {
	return _timer.isActive() || _requestId;
}

ChatsListBoxController::Row::Row(not_null<History*> history)
: PeerListRow(history->peer)
, _history(history) {
}

ChatsListBoxController::ChatsListBoxController(
	std::unique_ptr<PeerListSearchController> searchController)
: PeerListController(std::move(searchController)) {
}

void ChatsListBoxController::prepare() {
	setSearchNoResultsText(lang(lng_blocked_list_not_found));
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);

	prepareViewHook();

	rebuildRows();

	auto &sessionData = Auth().data();
	subscribe(sessionData.contactsLoaded(), [this](bool loaded) {
		rebuildRows();
	});
	subscribe(sessionData.moreChatsLoaded(), [this] {
		rebuildRows();
	});
	subscribe(sessionData.allChatsLoaded(), [this](bool loaded) {
		checkForEmptyRows();
	});
}

void ChatsListBoxController::rebuildRows() {
	auto wasEmpty = !delegate()->peerListFullRowsCount();
	auto appendList = [this](auto chats) {
		auto count = 0;
		for (const auto row : chats->all()) {
			if (const auto history = row->history()) {
				if (appendRow(history)) {
					++count;
				}
			}
		}
		return count;
	};
	auto added = 0;
	if (respectSavedMessagesChat()) {
		if (appendRow(App::history(Auth().user()))) {
			++added;
		}
	}
	added += appendList(App::main()->dialogsList());
	added += appendList(App::main()->contactsNoDialogsList());
	if (!wasEmpty && added > 0) {
		// Place dialogs list before contactsNoDialogs list.
		delegate()->peerListPartitionRows([](const PeerListRow &a) {
			auto history = static_cast<const Row&>(a).history();
			return history->inChatList(Dialogs::Mode::All);
		});
		if (respectSavedMessagesChat()) {
			delegate()->peerListPartitionRows([](const PeerListRow &a) {
				return a.peer()->isSelf();
			});
		}
	}
	checkForEmptyRows();
	delegate()->peerListRefreshRows();
}

void ChatsListBoxController::checkForEmptyRows() {
	if (delegate()->peerListFullRowsCount()) {
		setDescriptionText(QString());
	} else {
		auto &sessionData = Auth().data();
		auto loaded = sessionData.contactsLoaded().value() && sessionData.allChatsLoaded().value();
		setDescriptionText(loaded ? emptyBoxText() : lang(lng_contacts_loading));
	}
}

QString ChatsListBoxController::emptyBoxText() const {
	return lang(lng_contacts_not_found);
}

std::unique_ptr<PeerListRow> ChatsListBoxController::createSearchRow(not_null<PeerData*> peer) {
	return createRow(App::history(peer));
}

bool ChatsListBoxController::appendRow(not_null<History*> history) {
	if (auto row = delegate()->peerListFindRow(history->peer->id)) {
		updateRowHook(static_cast<Row*>(row));
		return false;
	}
	if (auto row = createRow(history)) {
		delegate()->peerListAppendRow(std::move(row));
		return true;
	}
	return false;
}

ContactsBoxController::ContactsBoxController(
	std::unique_ptr<PeerListSearchController> searchController)
: PeerListController(std::move(searchController)) {
}

void ContactsBoxController::prepare() {
	setSearchNoResultsText(lang(lng_blocked_list_not_found));
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	delegate()->peerListSetTitle(langFactory(lng_contacts_header));

	prepareViewHook();

	rebuildRows();

	auto &sessionData = Auth().data();
	subscribe(sessionData.contactsLoaded(), [this](bool loaded) {
		rebuildRows();
	});
}

void ContactsBoxController::rebuildRows() {
	auto appendList = [this](auto chats) {
		auto count = 0;
		for (const auto row : chats->all()) {
			if (const auto history = row->history()) {
				if (const auto user = history->peer->asUser()) {
					if (appendRow(user)) {
						++count;
					}
				}
			}
		}
		return count;
	};
	appendList(App::main()->contactsList());
	checkForEmptyRows();
	delegate()->peerListRefreshRows();
}

void ContactsBoxController::checkForEmptyRows() {
	if (delegate()->peerListFullRowsCount()) {
		setDescriptionText(QString());
	} else {
		auto &sessionData = Auth().data();
		auto loaded = sessionData.contactsLoaded().value();
		setDescriptionText(lang(loaded ? lng_contacts_not_found : lng_contacts_loading));
	}
}

std::unique_ptr<PeerListRow> ContactsBoxController::createSearchRow(
		not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return createRow(user);
	}
	return nullptr;
}

void ContactsBoxController::rowClicked(not_null<PeerListRow*> row) {
	Ui::showPeerHistory(row->peer(), ShowAtUnreadMsgId);
}

bool ContactsBoxController::appendRow(not_null<UserData*> user) {
	if (auto row = delegate()->peerListFindRow(user->id)) {
		updateRowHook(row);
		return false;
	}
	if (auto row = createRow(user)) {
		delegate()->peerListAppendRow(std::move(row));
		return true;
	}
	return false;
}

std::unique_ptr<PeerListRow> ContactsBoxController::createRow(not_null<UserData*> user) {
	return std::make_unique<PeerListRow>(user);
}

void AddBotToGroupBoxController::Start(not_null<UserData*> bot) {
	auto initBox = [=](not_null<PeerListBox*> box) {
		box->addButton(langFactory(lng_cancel), [box] { box->closeBox(); });
	};
	Ui::show(Box<PeerListBox>(std::make_unique<AddBotToGroupBoxController>(bot), std::move(initBox)));
}

AddBotToGroupBoxController::AddBotToGroupBoxController(not_null<UserData*> bot)
: ChatsListBoxController(SharingBotGame(bot)
	? std::make_unique<PeerListGlobalSearchController>()
	: nullptr)
, _bot(bot) {
}

void AddBotToGroupBoxController::rowClicked(not_null<PeerListRow*> row) {
	if (sharingBotGame()) {
		shareBotGame(row->peer());
	} else {
		addBotToGroup(row->peer());
	}
}

void AddBotToGroupBoxController::shareBotGame(not_null<PeerData*> chat) {
	auto send = crl::guard(this, [bot = _bot, chat] {
		ShareBotGame(bot, chat);
	});
	auto confirmText = [chat] {
		if (chat->isUser()) {
			return lng_bot_sure_share_game(lt_user, App::peerName(chat));
		}
		return lng_bot_sure_share_game_group(lt_group, chat->name);
	}();
	Ui::show(
		Box<ConfirmBox>(confirmText, std::move(send)),
		LayerOption::KeepOther);
}

void AddBotToGroupBoxController::addBotToGroup(not_null<PeerData*> chat) {
	if (const auto megagroup = chat->asMegagroup()) {
		if (!megagroup->canAddMembers()) {
			Ui::show(
				Box<InformBox>(lang(lng_error_cant_add_member)),
				LayerOption::KeepOther);
			return;
		}
	}
	auto send = crl::guard(this, [bot = _bot, chat] {
		AddBotToGroup(bot, chat);
	});
	auto confirmText = lng_bot_sure_invite(lt_group, chat->name);
	Ui::show(
		Box<ConfirmBox>(confirmText, send),
		LayerOption::KeepOther);
}

std::unique_ptr<ChatsListBoxController::Row> AddBotToGroupBoxController::createRow(not_null<History*> history) {
	if (!needToCreateRow(history->peer)) {
		return nullptr;
	}
	return std::make_unique<Row>(history);
}

bool AddBotToGroupBoxController::needToCreateRow(not_null<PeerData*> peer) const {
	if (sharingBotGame()) {
		if (!peer->canWrite()
			|| peer->amRestricted(ChatRestriction::f_send_games)) {
			return false;
		}
		return true;
	}
	if (const auto chat = peer->asChat()) {
		return chat->canAddMembers();
	} else if (const auto group = peer->asMegagroup()) {
		return group->canAddMembers();
	}
	return false;
}

bool AddBotToGroupBoxController::SharingBotGame(not_null<UserData*> bot) {
	auto &info = bot->botInfo;
	return (info && !info->shareGameShortName.isEmpty());
}

bool AddBotToGroupBoxController::sharingBotGame() const {
	return SharingBotGame(_bot);
}

QString AddBotToGroupBoxController::emptyBoxText() const {
	return lang(Auth().data().allChatsLoaded().value()
		? (sharingBotGame() ? lng_bot_no_chats : lng_bot_no_groups)
		: lng_contacts_loading);
}

QString AddBotToGroupBoxController::noResultsText() const {
	return lang(Auth().data().allChatsLoaded().value()
		? (sharingBotGame() ? lng_bot_chats_not_found : lng_bot_groups_not_found)
		: lng_contacts_loading);
}

void AddBotToGroupBoxController::updateLabels() {
	setSearchNoResultsText(noResultsText());
}

void AddBotToGroupBoxController::prepareViewHook() {
	delegate()->peerListSetTitle(langFactory(sharingBotGame()
		? lng_bot_choose_chat
		: lng_bot_choose_group));
	updateLabels();
	subscribe(Auth().data().allChatsLoaded(), [this](bool) { updateLabels(); });
}

ChooseRecipientBoxController::ChooseRecipientBoxController(
	FnMut<void(not_null<PeerData*>)> callback)
: _callback(std::move(callback)) {
}

void ChooseRecipientBoxController::prepareViewHook() {
	delegate()->peerListSetTitle(langFactory(lng_forward_choose));
}

void ChooseRecipientBoxController::rowClicked(not_null<PeerListRow*> row) {
	auto weak = base::make_weak(this);
	auto callback = std::move(_callback);
	callback(row->peer());
	if (weak) {
		_callback = std::move(callback);
	}
}

auto ChooseRecipientBoxController::createRow(
		not_null<History*> history) -> std::unique_ptr<Row> {
	return std::make_unique<Row>(history);
}
