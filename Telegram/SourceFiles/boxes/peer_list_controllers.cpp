/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peer_list_controllers.h"

#include "api/api_chat_participants.h"
#include "base/random.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/checkbox.h"
#include "ui/ui_utility.h"
#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_folder.h"
#include "data/data_histories.h"
#include "data/data_changes.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "lang/lang_keys.h"
#include "history/history.h"
#include "dialogs/dialogs_main_list.h"
#include "window/window_session_controller.h" // showAddContact()
#include "base/unixtime.h"
#include "facades.h"
#include "styles/style_boxes.h"
#include "styles/style_profile.h"

namespace {

constexpr auto kSortByOnlineThrottle = 3 * crl::time(1000);

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
//	auto ms = crl::now();
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

object_ptr<Ui::BoxContent> PrepareContactsBox(
		not_null<Window::SessionController*> sessionController) {
	using Mode = ContactsBoxController::SortMode;
	auto controller = std::make_unique<ContactsBoxController>(
		&sessionController->session());
	const auto raw = controller.get();
	auto init = [=](not_null<PeerListBox*> box) {
		struct State {
			QPointer<Ui::IconButton> toggleSort;
			Mode mode = ContactsBoxController::SortMode::Online;
		};
		const auto state = box->lifetime().make_state<State>();
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
		box->addLeftButton(
			tr::lng_profile_add_contact(),
			[=] { sessionController->showAddContact(); });
		state->toggleSort = box->addTopButton(st::contactsSortButton, [=] {
			const auto online = (state->mode == Mode::Online);
			state->mode = online ? Mode::Alphabet : Mode::Online;
			raw->setSortMode(state->mode);
			state->toggleSort->setIconOverride(
				online ? &st::contactsSortOnlineIcon : nullptr,
				online ? &st::contactsSortOnlineIconOver : nullptr);
		});
		raw->setSortMode(Mode::Online);
	};
	return Box<PeerListBox>(std::move(controller), std::move(init));
}

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

QSize PeerListRowWithLink::rightActionSize() const {
	return QSize(_actionWidth, st::normalFont->height);
}

QMargins PeerListRowWithLink::rightActionMargins() const {
	return QMargins(
		st::contactsCheckPosition.x(),
		(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom() - st::normalFont->height) / 2,
		st::defaultPeerListItem.photoPosition.x() + st::contactsCheckPosition.x(),
		0);
}

void PeerListRowWithLink::rightActionPaint(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		bool selected,
		bool actionSelected) {
	p.setFont(actionSelected ? st::linkOverFont : st::linkFont);
	p.setPen(actionSelected ? st::defaultLinkButton.overColor : st::defaultLinkButton.color);
	p.drawTextLeft(x, y, outerWidth, _action, _actionWidth);
}

PeerListGlobalSearchController::PeerListGlobalSearchController(
	not_null<Main::Session*> session)
: _session(session)
, _api(&session->mtp()) {
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
	_requestId = _api.request(MTPcontacts_Search(
		MTP_string(_query),
		MTP_int(SearchPeopleLimit)
	)).done([=](const MTPcontacts_Found &result, mtpRequestId requestId) {
		searchDone(result, requestId);
	}).fail([=](const MTP::Error &error, mtpRequestId requestId) {
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
		_session->data().processUsers(contacts.vusers());
		_session->data().processChats(contacts.vchats());
		auto it = _queries.find(requestId);
		if (it != _queries.cend()) {
			query = it->second;
			_cache[query] = result;
			_queries.erase(it);
		}
	}
	const auto feedList = [&](const MTPVector<MTPPeer> &list) {
		for (const auto &mtpPeer : list.v) {
			const auto peer = _session->data().peerLoaded(
				peerFromMTP(mtpPeer));
			if (peer) {
				delegate()->peerListSearchAddRow(peer);
			}
		}
	};
	if (_requestId == requestId) {
		_requestId = 0;
		feedList(contacts.vmy_results());
		feedList(contacts.vresults());
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
	not_null<Main::Session*> session)
: ChatsListBoxController(
	std::make_unique<PeerListGlobalSearchController>(session)) {
}

ChatsListBoxController::ChatsListBoxController(
	std::unique_ptr<PeerListSearchController> searchController)
: PeerListController(std::move(searchController)) {
}

void ChatsListBoxController::prepare() {
	setSearchNoResultsText(tr::lng_blocked_list_not_found(tr::now));
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);

	prepareViewHook();

	if (!session().data().chatsListLoaded()) {
		session().data().chatsListLoadedEvents(
		) | rpl::filter([=](Data::Folder *folder) {
			return !folder;
		}) | rpl::start_with_next([=] {
			checkForEmptyRows();
		}, lifetime());
	}

	session().data().chatsListChanges(
	) | rpl::start_with_next([=] {
		rebuildRows();
	}, lifetime());

	session().data().contactsLoaded().value(
	) | rpl::start_with_next([=] {
		rebuildRows();
	}, lifetime());
}

void ChatsListBoxController::rebuildRows() {
	auto wasEmpty = !delegate()->peerListFullRowsCount();
	auto appendList = [this](auto chats) {
		auto count = 0;
		for (const auto &row : chats->all()) {
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
		if (appendRow(session().data().history(session().user()))) {
			++added;
		}
	}
	added += appendList(session().data().chatsList()->indexed());
	const auto id = Data::Folder::kId;
	if (const auto folder = session().data().folderLoaded(id)) {
		added += appendList(folder->chatsList()->indexed());
	}
	added += appendList(session().data().contactsNoChatsList());
	if (!wasEmpty && added > 0) {
		// Place dialogs list before contactsNoDialogs list.
		delegate()->peerListPartitionRows([](const PeerListRow &a) {
			const auto history = static_cast<const Row&>(a).history();
			return history->inChatList();
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
		const auto loaded = session().data().contactsLoaded().current()
			&& session().data().chatsListLoaded();
		setDescriptionText(loaded ? emptyBoxText() : tr::lng_contacts_loading(tr::now));
	}
}

QString ChatsListBoxController::emptyBoxText() const {
	return tr::lng_contacts_not_found(tr::now);
}

std::unique_ptr<PeerListRow> ChatsListBoxController::createSearchRow(not_null<PeerData*> peer) {
	return createRow(peer->owner().history(peer));
}

bool ChatsListBoxController::appendRow(not_null<History*> history) {
	if (auto row = delegate()->peerListFindRow(history->peer->id.value)) {
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
	not_null<Main::Session*> session)
: ContactsBoxController(
	session,
	std::make_unique<PeerListGlobalSearchController>(session)) {
}

ContactsBoxController::ContactsBoxController(
	not_null<Main::Session*> session,
	std::unique_ptr<PeerListSearchController> searchController)
: PeerListController(std::move(searchController))
, _session(session)
, _sortByOnlineTimer([=] { sort(); }) {
}

Main::Session &ContactsBoxController::session() const {
	return *_session;
}

void ContactsBoxController::prepare() {
	setSearchNoResultsText(tr::lng_blocked_list_not_found(tr::now));
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	delegate()->peerListSetTitle(tr::lng_contacts_header());

	prepareViewHook();

	session().data().contactsLoaded().value(
	) | rpl::start_with_next([=] {
		rebuildRows();
	}, lifetime());
}

void ContactsBoxController::rebuildRows() {
	const auto appendList = [&](auto chats) {
		auto count = 0;
		for (const auto &row : chats->all()) {
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
	appendList(session().data().contactsList());
	checkForEmptyRows();
	sort();
	delegate()->peerListRefreshRows();
}

void ContactsBoxController::checkForEmptyRows() {
	setDescriptionText(delegate()->peerListFullRowsCount()
		? QString()
		: session().data().contactsLoaded().current()
		? tr::lng_contacts_not_found(tr::now)
		: tr::lng_contacts_loading(tr::now));
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

void ContactsBoxController::setSortMode(SortMode mode) {
	if (_sortMode == mode) {
		return;
	}
	_sortMode = mode;
	sort();
	if (_sortMode == SortMode::Online) {
		session().changes().peerUpdates(
			Data::PeerUpdate::Flag::OnlineStatus
		) | rpl::filter([=](const Data::PeerUpdate &update) {
			return !_sortByOnlineTimer.isActive()
				&& delegate()->peerListFindRow(update.peer->id.value);
		}) | rpl::start_with_next([=] {
			_sortByOnlineTimer.callOnce(kSortByOnlineThrottle);
		}, _sortByOnlineLifetime);
	} else {
		_sortByOnlineTimer.cancel();
		_sortByOnlineLifetime.destroy();
	}
}

void ContactsBoxController::sort() {
	switch (_sortMode) {
	case SortMode::Alphabet: sortByName(); break;
	case SortMode::Online: sortByOnline(); break;
	default: Unexpected("SortMode in ContactsBoxController.");
	}
}

void ContactsBoxController::sortByName() {
	auto keys = base::flat_map<PeerListRowId, QString>();
	keys.reserve(delegate()->peerListFullRowsCount());
	const auto key = [&](const PeerListRow &row) {
		const auto id = row.id();
		const auto i = keys.find(id);
		if (i != end(keys)) {
			return i->second;
		}
		const auto peer = row.peer();
		const auto history = peer->owner().history(peer);
		return keys.emplace(id, history->chatListNameSortKey()).first->second;
	};
	const auto predicate = [&](const PeerListRow &a, const PeerListRow &b) {
		return (key(a).compare(key(b)) < 0);
	};
	delegate()->peerListSortRows(predicate);
}

void ContactsBoxController::sortByOnline() {
	const auto now = base::unixtime::now();
	const auto key = [&](const PeerListRow &row) {
		const auto user = row.peer()->asUser();
		return user ? (std::min(user->onlineTill, now) + 1) : TimeId();
	};
	const auto predicate = [&](const PeerListRow &a, const PeerListRow &b) {
		return key(a) > key(b);
	};
	delegate()->peerListSortRows(predicate);
}

bool ContactsBoxController::appendRow(not_null<UserData*> user) {
	if (auto row = delegate()->peerListFindRow(user->id.value)) {
		updateRowHook(row);
		return false;
	}
	if (auto row = createRow(user)) {
		delegate()->peerListAppendRow(std::move(row));
		return true;
	}
	return false;
}

std::unique_ptr<PeerListRow> ContactsBoxController::createRow(
		not_null<UserData*> user) {
	return std::make_unique<PeerListRow>(user);
}

ChooseRecipientBoxController::ChooseRecipientBoxController(
	not_null<Main::Session*> session,
	FnMut<void(not_null<PeerData*>)> callback)
: ChatsListBoxController(session)
, _session(session)
, _callback(std::move(callback)) {
}

Main::Session &ChooseRecipientBoxController::session() const {
	return *_session;
}

void ChooseRecipientBoxController::prepareViewHook() {
	delegate()->peerListSetTitle(tr::lng_forward_choose());
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
	const auto peer = history->peer;
	const auto skip = (peer->isBroadcast() && !peer->canWrite())
		|| peer->isRepliesChat();
	return skip ? nullptr : std::make_unique<Row>(history);
}
