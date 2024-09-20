/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peer_list_controllers.h"

#include "api/api_chat_participants.h"
#include "api/api_premium.h"
#include "base/random.h"
#include "boxes/filters/edit_filter_chats_list.h"
#include "settings/settings_premium.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/round_checkbox.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "main/main_session.h"
#include "data/data_peer_values.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_folder.h"
#include "data/data_histories.h"
#include "data/data_changes.h"
#include "dialogs/ui/dialogs_layout.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "lang/lang_keys.h"
#include "history/history.h"
#include "history/history_item.h"
#include "dialogs/dialogs_main_list.h"
#include "ui/effects/outline_segments.h"
#include "ui/wrap/slide_wrap.h"
#include "window/window_session_controller.h" // showAddContact()
#include "base/unixtime.h"
#include "styles/style_boxes.h"
#include "styles/style_profile.h"
#include "styles/style_dialogs.h"
#include "styles/style_chat_helpers.h"

namespace {

constexpr auto kSortByOnlineThrottle = 3 * crl::time(1000);
constexpr auto kSearchPerPage = 50;

} // namespace

object_ptr<Ui::BoxContent> PrepareContactsBox(
		not_null<Window::SessionController*> sessionController) {
	using Mode = ContactsBoxController::SortMode;
	class Controller final : public ContactsBoxController {
	public:
		using ContactsBoxController::ContactsBoxController;

	protected:
		std::unique_ptr<PeerListRow> createRow(
				not_null<UserData*> user) override {
			return !user->isSelf()
				? ContactsBoxController::createRow(user)
				: nullptr;
		}

	};
	auto controller = std::make_unique<Controller>(
		&sessionController->session());
	controller->setStyleOverrides(&st::contactsWithStories);
	controller->setStoriesShown(true);
	const auto raw = controller.get();
	auto init = [=](not_null<PeerListBox*> box) {
		struct State {
			QPointer<::Ui::IconButton> toggleSort;
			rpl::variable<Mode> mode = Mode::Online;
			::Ui::Animations::Simple scrollAnimation;
		};

		const auto state = box->lifetime().make_state<State>();
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
		box->addLeftButton(
			tr::lng_profile_add_contact(),
			[=] { sessionController->showAddContact(); });
		state->toggleSort = box->addTopButton(st::contactsSortButton, [=] {
			const auto online = (state->mode.current() == Mode::Online);
			const auto mode = online ? Mode::Alphabet : Mode::Online;
			state->mode = mode;
			raw->setSortMode(mode);
			state->toggleSort->setIconOverride(
				online ? &st::contactsSortOnlineIcon : nullptr,
				online ? &st::contactsSortOnlineIconOver : nullptr);
		});
		raw->setSortMode(Mode::Online);
	};
	return Box<PeerListBox>(std::move(controller), std::move(init));
}

QBrush PeerListStoriesGradient(const style::PeerList &st) {
	const auto left = st.item.photoPosition.x();
	const auto top = st.item.photoPosition.y();
	const auto size = st.item.photoSize;
	return Ui::UnreadStoryOutlineGradient(QRectF(left, top, size, size));
}

std::vector<Ui::OutlineSegment> PeerListStoriesSegments(
		int count,
		int unread,
		const QBrush &unreadBrush) {
	Expects(unread <= count);
	Expects(count > 0);

	auto result = std::vector<Ui::OutlineSegment>();
	const auto add = [&](bool unread) {
		result.push_back({
			.brush = unread ? unreadBrush : st::dialogsUnreadBgMuted->b,
			.width = (unread
				? st::dialogsStoriesFull.lineTwice / 2.
				: st::dialogsStoriesFull.lineReadTwice / 2.),
		});
	};
	result.reserve(count);
	for (auto i = 0, till = count - unread; i != till; ++i) {
		add(false);
	}
	for (auto i = 0; i != unread; ++i) {
		add(true);
	}
	return result;
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
	p.setFont(actionSelected ? st::linkFontOver : st::linkFont);
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

RecipientRow::RecipientRow(
	not_null<PeerData*> peer,
	const style::PeerListItem *maybeLockedSt,
	History *maybeHistory)
: PeerListRow(peer)
, _maybeHistory(maybeHistory)
, _resolvePremiumRequired(maybeLockedSt != nullptr) {
	if (maybeLockedSt
		&& (Api::ResolveRequiresPremiumToWrite(peer, maybeHistory)
			== Api::RequirePremiumState::Yes)) {
		_lockedSt = maybeLockedSt;
	}
}

PaintRoundImageCallback RecipientRow::generatePaintUserpicCallback(
		bool forceRound) {
	auto result = PeerListRow::generatePaintUserpicCallback(forceRound);
	if (const auto st = _lockedSt) {
		return [=](Painter &p, int x, int y, int outerWidth, int size) {
			result(p, x, y, outerWidth, size);
			PaintPremiumRequiredLock(p, st, x, y, outerWidth, size);
		};
	}
	return result;
}

bool RecipientRow::refreshLock(
		not_null<const style::PeerListItem*> maybeLockedSt) {
	if (const auto user = peer()->asUser()) {
		const auto locked = _resolvePremiumRequired
			&& (Api::ResolveRequiresPremiumToWrite(user, _maybeHistory)
				== Api::RequirePremiumState::Yes);
		if (this->locked() != locked) {
			setLocked(locked ? maybeLockedSt.get() : nullptr);
			return true;
		}
	}
	return false;
}

void RecipientRow::preloadUserpic() {
	PeerListRow::preloadUserpic();

	if (!_resolvePremiumRequired) {
		return;
	} else if (Api::ResolveRequiresPremiumToWrite(peer(), _maybeHistory)
		== Api::RequirePremiumState::Unknown) {
		const auto user = peer()->asUser();
		user->session().api().premium().resolvePremiumRequired(user);
	}
}

void TrackPremiumRequiredChanges(
		not_null<PeerListController*> controller,
		rpl::lifetime &lifetime) {
	const auto session = &controller->session();
	rpl::merge(
		Data::AmPremiumValue(session) | rpl::to_empty,
		session->api().premium().somePremiumRequiredResolved()
	) | rpl::start_with_next([=] {
		const auto st = &controller->computeListSt().item;
		const auto delegate = controller->delegate();
		const auto process = [&](not_null<PeerListRow*> raw) {
			if (static_cast<RecipientRow*>(raw.get())->refreshLock(st)) {
				delegate->peerListUpdateRow(raw);
			}
		};
		auto count = delegate->peerListFullRowsCount();
		for (auto i = 0; i != count; ++i) {
			process(delegate->peerListRowAt(i));
		}
		count = delegate->peerListSearchRowsCount();
		for (auto i = 0; i != count; ++i) {
			process(delegate->peerListSearchRowAt(i));
		}
	}, lifetime);
}

ChatsListBoxController::Row::Row(
	not_null<History*> history,
	const style::PeerListItem *maybeLockedSt)
: RecipientRow(history->peer, maybeLockedSt, history) {
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
	if (!savedMessagesChatStatus().isEmpty()) {
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
		if (!savedMessagesChatStatus().isEmpty()) {
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

std::unique_ptr<PeerListRow> ChatsListBoxController::createSearchRow(
		not_null<PeerData*> peer) {
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

PeerListStories::PeerListStories(
	not_null<PeerListController*> controller,
	not_null<Main::Session*> session)
: _controller(controller)
, _session(session) {
}

void PeerListStories::updateColors() {
	for (auto i = begin(_counts); i != end(_counts); ++i) {
		if (const auto row = _delegate->peerListFindRow(i->first)) {
			if (i->second.count >= 0 && i->second.unread >= 0) {
				applyForRow(row, i->second.count, i->second.unread, true);
			}
		}
	}
}

void PeerListStories::updateFor(
		uint64 id,
		int count,
		int unread) {
	if (const auto row = _delegate->peerListFindRow(id)) {
		applyForRow(row, count, unread);
		_delegate->peerListUpdateRow(row);
	}
}

void PeerListStories::process(not_null<PeerListRow*> row) {
	const auto user = row->peer()->asUser();
	if (!user) {
		return;
	}
	const auto stories = &_session->data().stories();
	const auto source = stories->source(user->id);
	const auto count = source
		? int(source->ids.size())
		: user->hasActiveStories()
		? 1
		: 0;
	const auto unread = source
		? source->info().unreadCount
		: user->hasUnreadStories()
		? 1
		: 0;
	applyForRow(row, count, unread, true);
}

bool PeerListStories::handleClick(not_null<PeerData*> peer) {
	const auto point = _delegate->peerListLastRowMousePosition();
	const auto &st = _controller->computeListSt().item;
	if (point && point->x() < st.photoPosition.x() + st.photoSize) {
		if (const auto window = peer->session().tryResolveWindow()) {
			if (const auto user = peer->asUser()) {
				if (user->hasActiveStories()) {
					window->openPeerStories(peer->id);
					return true;
				}
			}
		}
	}
	return false;
}

void PeerListStories::prepare(not_null<PeerListDelegate*> delegate) {
	_delegate = delegate;

	_unreadBrush = PeerListStoriesGradient(_controller->computeListSt());
	style::PaletteChanged() | rpl::start_with_next([=] {
		_unreadBrush = PeerListStoriesGradient(_controller->computeListSt());
		updateColors();
	}, _lifetime);

	_session->changes().peerUpdates(
		Data::PeerUpdate::Flag::StoriesState
	) | rpl::start_with_next([=](const Data::PeerUpdate &update) {
		const auto id = update.peer->id.value;
		if (const auto row = _delegate->peerListFindRow(id)) {
			process(row);
		}
	}, _lifetime);

	const auto stories = &_session->data().stories();
	stories->sourceChanged() | rpl::start_with_next([=](PeerId id) {
		const auto source = stories->source(id);
		const auto info = source
			? source->info()
			: Data::StoriesSourceInfo();
		updateFor(id.value, info.count, info.unreadCount);
	}, _lifetime);
}

void PeerListStories::applyForRow(
		not_null<PeerListRow*> row,
		int count,
		int unread,
		bool force) {
	auto &counts = _counts[row->id()];
	if (!force && counts.count == count && counts.unread == unread) {
		return;
	}
	counts.count = count;
	counts.unread = unread;
	_delegate->peerListSetRowChecked(row, count > 0);
	if (count > 0) {
		row->setCustomizedCheckSegments(
			PeerListStoriesSegments(count, unread, _unreadBrush));
	}
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

	if (_stories) {
		_stories->prepare(delegate());
	}

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
	const auto peer = row->peer();
	if (_stories && _stories->handleClick(peer)) {
		return;
	} else if (const auto window = peer->session().tryResolveWindow()) {
		window->showPeerHistory(peer);
	}
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

void ContactsBoxController::setStoriesShown(bool shown) {
	_stories = std::make_unique<PeerListStories>(this, _session);
}

void ContactsBoxController::sort() {
	switch (_sortMode) {
	case SortMode::Alphabet: sortByName(); break;
	case SortMode::Online: sortByOnline(); break;
	default: Unexpected("SortMode in ContactsBoxController.");
	}
}

void ContactsBoxController::sortByOnline() {
	const auto now = base::unixtime::now();
	const auto key = [&](const PeerListRow &row) {
		const auto user = row.peer()->asUser();
		return user
			? (std::min(user->lastseen().onlineTill(), now + 1) + 1)
			: TimeId();
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
		const auto raw = row.get();
		delegate()->peerListAppendRow(std::move(row));
		if (_stories) {
			_stories->process(raw);
		}
		return true;
	}
	return false;
}

std::unique_ptr<PeerListRow> ContactsBoxController::createRow(
		not_null<UserData*> user) {
	return std::make_unique<PeerListRow>(user);
}

RecipientPremiumRequiredError WritePremiumRequiredError(
		not_null<UserData*> user) {
	return {
		.text = tr::lng_send_non_premium_message_toast(
			tr::now,
			lt_user,
			TextWithEntities{ user->shortName() },
			lt_link,
			Ui::Text::Link(
				Ui::Text::Bold(
					tr::lng_send_non_premium_message_toast_link(
						tr::now))),
			Ui::Text::RichLangValue),
	};
}

ChooseRecipientBoxController::ChooseRecipientBoxController(
	not_null<Main::Session*> session,
	FnMut<void(not_null<Data::Thread*>)> callback,
	Fn<bool(not_null<Data::Thread*>)> filter)
: ChooseRecipientBoxController({
	.session = session,
	.callback = std::move(callback),
	.filter = std::move(filter),
}) {
}

ChooseRecipientBoxController::ChooseRecipientBoxController(
	ChooseRecipientArgs &&args)
: ChatsListBoxController(args.session)
, _session(args.session)
, _callback(std::move(args.callback))
, _filter(std::move(args.filter))
, _premiumRequiredError(std::move(args.premiumRequiredError)) {
}

Main::Session &ChooseRecipientBoxController::session() const {
	return *_session;
}

void ChooseRecipientBoxController::prepareViewHook() {
	delegate()->peerListSetTitle(tr::lng_forward_choose());

	if (_premiumRequiredError) {
		TrackPremiumRequiredChanges(this, lifetime());
	}
}

bool ChooseRecipientBoxController::showLockedError(
		not_null<PeerListRow*> row) {
	return RecipientRow::ShowLockedError(this, row, _premiumRequiredError);
}

void ChooseRecipientBoxController::rowClicked(not_null<PeerListRow*> row) {
	if (showLockedError(row)) {
		return;
	}
	auto guard = base::make_weak(this);
	const auto peer = row->peer();
	if (const auto forum = peer->forum()) {
		const auto weak = std::make_shared<QPointer<Ui::BoxContent>>();
		auto callback = [=](not_null<Data::ForumTopic*> topic) {
			const auto exists = guard.get();
			if (!exists) {
				if (*weak) {
					(*weak)->closeBox();
				}
				return;
			}
			auto onstack = std::move(_callback);
			onstack(topic);
			if (guard) {
				_callback = std::move(onstack);
			} else if (*weak) {
				(*weak)->closeBox();
			}
		};
		const auto filter = [=](not_null<Data::ForumTopic*> topic) {
			return guard && (!_filter || _filter(topic));
		};
		auto owned = Box<PeerListBox>(
			std::make_unique<ChooseTopicBoxController>(
				forum,
				std::move(callback),
				filter),
			[=](not_null<PeerListBox*> box) {
				box->addButton(tr::lng_cancel(), [=] {
					box->closeBox();
				});

				forum->destroyed(
				) | rpl::start_with_next([=] {
					box->closeBox();
				}, box->lifetime());
			});
		*weak = owned.data();
		delegate()->peerListUiShow()->showBox(std::move(owned));
		return;
	}
	const auto history = peer->owner().history(peer);
	auto callback = std::move(_callback);
	callback(history);
	if (guard) {
		_callback = std::move(callback);
	}
}

bool RecipientRow::ShowLockedError(
		not_null<PeerListController*> controller,
		not_null<PeerListRow*> row,
		Fn<RecipientPremiumRequiredError(not_null<UserData*>)> error) {
	if (!static_cast<RecipientRow*>(row.get())->locked()) {
		return false;
	}
	::Settings::ShowPremiumPromoToast(
		controller->delegate()->peerListUiShow(),
		ChatHelpers::ResolveWindowDefault(),
		error(row->peer()->asUser()).text,
		u"require_premium"_q);
	return true;
}

QString ChooseRecipientBoxController::savedMessagesChatStatus() const {
	return tr::lng_saved_forward_here(tr::now);
}

auto ChooseRecipientBoxController::createRow(
		not_null<History*> history) -> std::unique_ptr<Row> {
	const auto peer = history->peer;
	const auto skip = _filter
		? !_filter(history)
		: ((peer->isBroadcast() && !Data::CanSendAnything(peer))
			|| peer->isRepliesChat()
			|| peer->isVerifyCodes()
			|| (peer->isUser() && (_premiumRequiredError
				? !peer->asUser()->canSendIgnoreRequirePremium()
				: !Data::CanSendAnything(peer))));
	if (skip) {
		return nullptr;
	}
	auto result = std::make_unique<Row>(
		history,
		_premiumRequiredError ? &computeListSt().item : nullptr);
	return result;
}

ChooseTopicSearchController::ChooseTopicSearchController(
	not_null<Data::Forum*> forum)
: _forum(forum)
, _api(&forum->session().mtp())
, _timer([=] { searchOnServer(); }) {
}

void ChooseTopicSearchController::searchQuery(const QString &query) {
	if (_query != query) {
		_query = query;
		_api.request(base::take(_requestId)).cancel();
		_offsetDate = 0;
		_offsetId = 0;
		_offsetTopicId = 0;
		_allLoaded = false;
		if (!_query.isEmpty()) {
			_timer.callOnce(AutoSearchTimeout);
		} else {
			_timer.cancel();
		}
	}
}

void ChooseTopicSearchController::searchOnServer() {
	_requestId = _api.request(MTPchannels_GetForumTopics(
		MTP_flags(MTPchannels_GetForumTopics::Flag::f_q),
		_forum->channel()->inputChannel,
		MTP_string(_query),
		MTP_int(_offsetDate),
		MTP_int(_offsetId),
		MTP_int(_offsetTopicId),
		MTP_int(kSearchPerPage)
	)).done([=](const MTPmessages_ForumTopics &result) {
		_requestId = 0;
		const auto savedTopicId = _offsetTopicId;
		const auto byCreation = result.data().is_order_by_create_date();
		_forum->applyReceivedTopics(result, [&](
				not_null<Data::ForumTopic*> topic) {
			_offsetTopicId = topic->rootId();
			if (byCreation) {
				_offsetDate = topic->creationDate();
				if (const auto last = topic->lastServerMessage()) {
					_offsetId = last->id;
				}
			} else if (const auto last = topic->lastServerMessage()) {
				_offsetId = last->id;
				_offsetDate = last->date();
			}
			delegate()->peerListSearchAddRow(topic->rootId().bare);
		});
		if (_offsetTopicId == savedTopicId) {
			_allLoaded = true;
		}
		delegate()->peerListSearchRefreshRows();
	}).fail([=] {
		_allLoaded = true;
	}).send();
}

bool ChooseTopicSearchController::isLoading() {
	return _timer.isActive() || _requestId;
}

bool ChooseTopicSearchController::loadMoreRows() {
	if (!isLoading()) {
		searchOnServer();
	}
	return !_allLoaded;
}

ChooseTopicBoxController::Row::Row(not_null<Data::ForumTopic*> topic)
: PeerListRow(topic->rootId().bare)
, _topic(topic) {
}

QString ChooseTopicBoxController::Row::generateName() {
	return _topic->title();
}

QString ChooseTopicBoxController::Row::generateShortName() {
	return _topic->title();
}

auto ChooseTopicBoxController::Row::generatePaintUserpicCallback(
	bool forceRound)
-> PaintRoundImageCallback {
	return [=](
			Painter &p,
			int x,
			int y,
			int outerWidth,
			int size) {
		const auto &st = st::forumTopicRow;
		x -= st.padding.left();
		y -= st.padding.top();
		auto view = Ui::PeerUserpicView();
		p.translate(x, y);
		_topic->paintUserpic(p, view, {
			.st = &st,
			.currentBg = st::windowBg,
			.now = crl::now(),
			.width = outerWidth,
			.paused = false,
		});
		p.translate(-x, -y);
	};
}

auto ChooseTopicBoxController::Row::generateNameFirstLetters() const
-> const base::flat_set<QChar> & {
	return _topic->chatListFirstLetters();
}

auto ChooseTopicBoxController::Row::generateNameWords() const
-> const base::flat_set<QString> & {
	return _topic->chatListNameWords();
}

ChooseTopicBoxController::ChooseTopicBoxController(
	not_null<Data::Forum*> forum,
	FnMut<void(not_null<Data::ForumTopic*>)> callback,
	Fn<bool(not_null<Data::ForumTopic*>)> filter)
: PeerListController(std::make_unique<ChooseTopicSearchController>(forum))
, _forum(forum)
, _callback(std::move(callback))
, _filter(std::move(filter)) {
	setStyleOverrides(&st::chooseTopicList);

	_forum->chatsListChanges(
	) | rpl::start_with_next([=] {
		refreshRows();
	}, lifetime());

	_forum->topicDestroyed(
	) | rpl::start_with_next([=](not_null<Data::ForumTopic*> topic) {
		const auto id = PeerListRowId(topic->rootId().bare);
		if (const auto row = delegate()->peerListFindRow(id)) {
			delegate()->peerListRemoveRow(row);
			delegate()->peerListRefreshRows();
		}
	}, lifetime());
}

Main::Session &ChooseTopicBoxController::session() const {
	return _forum->session();
}

void ChooseTopicBoxController::rowClicked(not_null<PeerListRow*> row) {
	const auto weak = base::make_weak(this);
	auto onstack = base::take(_callback);
	onstack(static_cast<Row*>(row.get())->topic());
	if (weak) {
		_callback = std::move(onstack);
	}
}

void ChooseTopicBoxController::prepare() {
	delegate()->peerListSetTitle(tr::lng_forward_choose());
	setSearchNoResultsText(tr::lng_topics_not_found(tr::now));
	delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
	refreshRows(true);

	session().changes().entryUpdates(
		Data::EntryUpdate::Flag::Repaint
	) | rpl::start_with_next([=](const Data::EntryUpdate &update) {
		if (const auto topic = update.entry->asTopic()) {
			if (topic->forum() == _forum) {
				const auto id = topic->rootId().bare;
				if (const auto row = delegate()->peerListFindRow(id)) {
					delegate()->peerListUpdateRow(row);
				}
			}
		}
	}, lifetime());
}

void ChooseTopicBoxController::refreshRows(bool initial) {
	auto added = false;
	for (const auto &row : _forum->topicsList()->indexed()->all()) {
		if (const auto topic = row->topic()) {
			const auto id = topic->rootId().bare;
			auto already = delegate()->peerListFindRow(id);
			if (initial || !already) {
				if (auto created = createRow(topic)) {
					delegate()->peerListAppendRow(std::move(created));
					added = true;
				}
			} else if (already->isSearchResult()) {
				delegate()->peerListAppendFoundRow(already);
				added = true;
			}
		}
	}
	if (added) {
		delegate()->peerListRefreshRows();
	}
}

void ChooseTopicBoxController::loadMoreRows() {
	_forum->requestTopics();
}

std::unique_ptr<PeerListRow> ChooseTopicBoxController::createSearchRow(
		PeerListRowId id) {
	if (const auto topic = _forum->topicFor(MsgId(id))) {
		return std::make_unique<Row>(topic);
	}
	return nullptr;
}

auto ChooseTopicBoxController::createRow(not_null<Data::ForumTopic*> topic)
-> std::unique_ptr<Row> {
	const auto skip = _filter && !_filter(topic);
	return skip ? nullptr : std::make_unique<Row>(topic);
};

void PaintPremiumRequiredLock(
		Painter &p,
		not_null<const style::PeerListItem*> st,
		int x,
		int y,
		int outerWidth,
		int size) {
	auto hq = PainterHighQualityEnabler(p);
	const auto &check = st->checkbox.check;
	auto pen = check.border->p;
	pen.setWidthF(check.width);
	p.setPen(pen);
	p.setBrush(st::premiumButtonBg2);
	const auto &icon = st::stickersPremiumLock;
	const auto width = icon.width();
	const auto height = icon.height();
	const auto rect = QRect(
		QPoint(x + size - width, y + size - height),
		icon.size());
	p.drawEllipse(rect);
	icon.paintInCenter(p, rect);
}
