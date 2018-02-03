/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peer_list_controllers.h"

#include "styles/style_boxes.h"
#include "styles/style_profile.h"
#include "boxes/confirm_box.h"
#include "observer_peer.h"
#include "ui/widgets/checkbox.h"
#include "auth_session.h"
#include "data/data_session.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "lang/lang_keys.h"
#include "history/history.h"
#include "dialogs/dialogs_indexed_list.h"

namespace {

base::flat_set<not_null<UserData*>> GetAlreadyInFromPeer(PeerData *peer) {
	if (!peer) {
		return {};
	}
	if (auto chat = peer->asChat()) {
		auto participants = (
			chat->participants
		) | ranges::view::transform([](auto &&pair) -> not_null<UserData*> {
			return pair.first;
		});
		return { participants.begin(), participants.end() };
	} else if (auto channel = peer->asChannel()) {
		if (channel->isMegagroup()) {
			auto &participants = channel->mgInfo->lastParticipants;
			return { participants.cbegin(), participants.cend() };
		}
	}
	return {};
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

class EditChatAdminsBoxController::LabeledCheckbox : public TWidget, private base::Subscriber {
public:
	LabeledCheckbox(QWidget *parent, const QString &text, bool checked = false, const style::Checkbox &st = st::defaultCheckbox, const style::Check &checkSt = st::defaultCheck);

	base::Observable<bool> checkedChanged;

	bool checked() const {
		return _checkbox->checked();
	}

	void setLabelText(
		bool checked,
		const style::TextStyle &st,
		const QString &text,
		const TextParseOptions &options = _defaultOptions,
		int minResizeWidth = QFIXED_MAX);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	object_ptr<Ui::Checkbox> _checkbox;
	Text _labelUnchecked;
	Text _labelChecked;
	int _labelWidth = 0;

};

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
		if (auto self = App::self()) {
			if (appendRow(App::history(self))) {
				++added;
			}
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

ContactsBoxController::ContactsBoxController(std::unique_ptr<PeerListSearchController> searchController) : PeerListController(std::move(searchController)) {
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

std::unique_ptr<PeerListRow> AddParticipantsBoxController::createRow(not_null<UserData*> user) {
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
	auto initBox = [chat](not_null<PeerListBox*> box) {
		box->addButton(langFactory(lng_participant_invite), [box, chat] {
			auto rows = box->peerListCollectSelectedRows();
			if (!rows.empty()) {
				auto users = std::vector<not_null<UserData*>>();
				for (auto peer : rows) {
					auto user = peer->asUser();
					Assert(user != nullptr);
					Assert(!user->isSelf());
					users.push_back(peer->asUser());
				}
				App::main()->addParticipants(chat, users);
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
		auto subscription = std::make_shared<base::Subscription>();
		box->addButton(langFactory(lng_participant_invite), [box, channel, subscription] {
			auto rows = box->peerListCollectSelectedRows();
			if (!rows.empty()) {
				auto users = std::vector<not_null<UserData*>>();
				for (auto peer : rows) {
					auto user = peer->asUser();
					Assert(user != nullptr);
					Assert(!user->isSelf());
					users.push_back(peer->asUser());
				}
				App::main()->addParticipants(channel, users);
				if (channel->isMegagroup()) {
					Ui::showPeerHistory(channel, ShowAtTheEndMsgId);
				} else {
					box->closeBox();
				}
			}
		});
		box->addButton(langFactory(justCreated ? lng_create_group_skip : lng_cancel), [box] { box->closeBox(); });
		if (justCreated) {
			*subscription = box->boxClosing.add_subscription([channel] {
				Ui::showPeerHistory(channel, ShowAtTheEndMsgId);
			});
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

EditChatAdminsBoxController::LabeledCheckbox::LabeledCheckbox(
	QWidget *parent,
	const QString &text,
	bool checked,
	const style::Checkbox &st,
	const style::Check &checkSt)
: TWidget(parent)
, _checkbox(this, text, checked, st, checkSt) {
	subscribe(_checkbox->checkedChanged, [this](bool value) { checkedChanged.notify(value, true); });
}

void EditChatAdminsBoxController::LabeledCheckbox::setLabelText(
		bool checked,
		const style::TextStyle &st,
		const QString &text,
		const TextParseOptions &options,
		int minResizeWidth) {
	auto &label = (checked ? _labelChecked : _labelUnchecked);
	label = Text(st, text, options, minResizeWidth);
}

int EditChatAdminsBoxController::LabeledCheckbox::resizeGetHeight(int newWidth) {
	_labelWidth = newWidth - st::contactsPadding.left() - st::contactsPadding.right();
	_checkbox->resizeToNaturalWidth(_labelWidth);
	_checkbox->moveToLeft(st::contactsPadding.left(), st::contactsAllAdminsTop);
	auto labelHeight = qMax(
		_labelChecked.countHeight(_labelWidth),
		_labelUnchecked.countHeight(_labelWidth));
	return st::contactsAboutTop + labelHeight + st::contactsAboutBottom;
}

void EditChatAdminsBoxController::LabeledCheckbox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto infoTop = _checkbox->bottomNoMargins() + st::contactsAllAdminsTop - st::lineWidth;

	auto infoRect = rtlrect(0, infoTop, width(), height() - infoTop - st::contactsPadding.bottom(), width());
	p.fillRect(infoRect, st::contactsAboutBg);
	auto dividerFillTop = rtlrect(0, infoRect.y(), width(), st::profileDividerTop.height(), width());
	st::profileDividerTop.fill(p, dividerFillTop);
	auto dividerFillBottom = rtlrect(0, infoRect.y() + infoRect.height() - st::profileDividerBottom.height(), width(), st::profileDividerBottom.height(), width());
	st::profileDividerBottom.fill(p, dividerFillBottom);

	p.setPen(st::contactsAboutFg);
	(checked() ? _labelChecked : _labelUnchecked).draw(p, st::contactsPadding.left(), st::contactsAboutTop, _labelWidth);
}

EditChatAdminsBoxController::EditChatAdminsBoxController(not_null<ChatData*> chat)
: PeerListController()
, _chat(chat) {
}

bool EditChatAdminsBoxController::allAreAdmins() const {
	return _allAdmins->checked();
}

void EditChatAdminsBoxController::prepare() {
	createAllAdminsCheckbox();

	setSearchNoResultsText(lang(lng_blocked_list_not_found));
	delegate()->peerListSetSearchMode(allAreAdmins() ? PeerListSearchMode::Disabled : PeerListSearchMode::Enabled);
	delegate()->peerListSetTitle(langFactory(lng_channel_admins));

	rebuildRows();
	if (!delegate()->peerListFullRowsCount()) {
		Auth().api().requestFullPeer(_chat);
		_adminsUpdatedSubscription = subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(
				Notify::PeerUpdate::Flag::AdminsChanged, [this](
					const Notify::PeerUpdate &update) {
			if (update.peer == _chat) {
				rebuildRows();
				if (delegate()->peerListFullRowsCount()) {
					unsubscribe(_adminsUpdatedSubscription);
				}
			}
		}));
	}

	subscribe(_allAdmins->checkedChanged, [this](bool checked) {
		delegate()->peerListSetSearchMode(checked ? PeerListSearchMode::Disabled : PeerListSearchMode::Enabled);
		for (auto i = 0, count = delegate()->peerListFullRowsCount(); i != count; ++i) {
			auto row = delegate()->peerListRowAt(i);
			auto user = row->peer()->asUser();
			if (checked || user->id == peerFromUser(_chat->creator)) {
				row->setDisabledState(PeerListRow::State::DisabledChecked);
			} else {
				row->setDisabledState(PeerListRow::State::Active);
			}
		}
	});
}

void EditChatAdminsBoxController::createAllAdminsCheckbox() {
	auto labelWidth = st::boxWideWidth - st::contactsPadding.left() - st::contactsPadding.right();
	auto checkbox = object_ptr<LabeledCheckbox>(nullptr, lang(lng_chat_all_members_admins), !_chat->adminsEnabled(), st::defaultBoxCheckbox);
	checkbox->setLabelText(true, st::defaultTextStyle, lang(lng_chat_about_all_admins), _defaultOptions, labelWidth);
	checkbox->setLabelText(false, st::defaultTextStyle, lang(lng_chat_about_admins), _defaultOptions, labelWidth);
	_allAdmins = checkbox;
	delegate()->peerListSetAboveWidget(std::move(checkbox));
}

void EditChatAdminsBoxController::rebuildRows() {
	if (_chat->participants.empty()) {
		return;
	}

	auto allAdmins = allAreAdmins();

	auto admins = std::vector<not_null<UserData*>>();
	auto others = admins;
	admins.reserve(allAdmins ? _chat->participants.size() : _chat->admins.size());
	others.reserve(_chat->participants.size());

	for (auto [user, version] : _chat->participants) {
		if (user->id == peerFromUser(_chat->creator)) continue;
		if (_chat->admins.contains(user)) {
			admins.push_back(user);
		} else {
			others.push_back(user);
		}
	}
	if (!admins.empty()) {
		delegate()->peerListAddSelectedRows(admins);
	}

	if (allAdmins) {
		admins.insert(admins.end(), others.begin(), others.end());
		others.clear();
	}
	auto sortByName = [](not_null<UserData*> a, auto b) {
		return (a->name.compare(b->name, Qt::CaseInsensitive) < 0);
	};
	ranges::sort(admins, sortByName);
	ranges::sort(others, sortByName);

	auto addOne = [this](not_null<UserData*> user) {
		if (auto row = createRow(user)) {
			delegate()->peerListAppendRow(std::move(row));
		}
	};
	if (auto creator = App::userLoaded(_chat->creator)) {
		if (_chat->participants.contains(creator)) {
			addOne(creator);
		}
	}
	ranges::for_each(admins, addOne);
	ranges::for_each(others, addOne);

	delegate()->peerListRefreshRows();
}

std::unique_ptr<PeerListRow> EditChatAdminsBoxController::createRow(not_null<UserData*> user) {
	auto result = std::make_unique<PeerListRow>(user);
	if (allAreAdmins() || user->id == peerFromUser(_chat->creator)) {
		result->setDisabledState(PeerListRow::State::DisabledChecked);
	}
	return result;
}

void EditChatAdminsBoxController::rowClicked(not_null<PeerListRow*> row) {
	delegate()->peerListSetRowChecked(row, !row->checked());
}

void EditChatAdminsBoxController::Start(not_null<ChatData*> chat) {
	auto controller = std::make_unique<EditChatAdminsBoxController>(chat);
	auto initBox = [chat, controller = controller.get()](not_null<PeerListBox*> box) {
		box->addButton(langFactory(lng_settings_save), [box, chat, controller] {
			auto rows = box->peerListCollectSelectedRows();
			auto users = std::vector<not_null<UserData*>>();
			for (auto peer : rows) {
				auto user = peer->asUser();
				Assert(user != nullptr);
				Assert(!user->isSelf());
				users.push_back(peer->asUser());
			}
			Auth().api().editChatAdmins(chat, !controller->allAreAdmins(), { users.cbegin(), users.cend() });
			box->closeBox();
		});
		box->addButton(langFactory(lng_cancel), [box] { box->closeBox(); });
	};
	Ui::show(
		Box<PeerListBox>(std::move(controller), std::move(initBox)),
		LayerOption::KeepOther);
}

void AddBotToGroupBoxController::Start(not_null<UserData*> bot) {
	auto initBox = [bot](not_null<PeerListBox*> box) {
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
	auto send = [weak = base::make_weak(this), bot = _bot, chat] {
		if (!weak) {
			return;
		}
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
	};
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
	if (auto megagroup = chat->asMegagroup()) {
		if (!megagroup->canAddMembers()) {
			Ui::show(
				Box<InformBox>(lang(lng_error_cant_add_member)),
				LayerOption::KeepOther);
			return;
		}
	}
	auto send = [weak = base::make_weak(this), bot = _bot, chat] {
		if (!weak) {
			return;
		}
		if (auto &info = bot->botInfo) {
			if (!info->startGroupToken.isEmpty()) {
				MTP::send(
					MTPmessages_StartBot(
						bot->inputUser,
						chat->input,
						MTP_long(rand_value<uint64>()),
						MTP_string(info->startGroupToken)),
					App::main()->rpcDone(&MainWidget::sentUpdatesReceived),
					App::main()->rpcFail(
						&MainWidget::addParticipantFail,
						{ bot, chat }));
			} else {
				App::main()->addParticipants(
					chat,
					{ 1, bot });
			}
		} else {
			App::main()->addParticipants(
				chat,
				{ 1, bot });
		}
		Ui::hideLayer();
		Ui::showPeerHistory(chat, ShowAtUnreadMsgId);
	};
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
		if (!peer->canWrite()) {
			return false;
		}
		if (auto group = peer->asMegagroup()) {
			if (group->restricted(ChannelRestriction::f_send_games)) {
				return false;
			}
		}
		return true;
	}
	if (auto chat = peer->asChat()) {
		if (chat->canEdit()) {
			return true;
		}
	} else if (auto group = peer->asMegagroup()) {
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
	base::lambda_once<void(not_null<PeerData*>)> callback)
: _callback(std::move(callback)) {
}

void ChooseRecipientBoxController::prepareViewHook() {
	delegate()->peerListSetTitle(langFactory(lng_forward_choose));
}

void ChooseRecipientBoxController::rowClicked(not_null<PeerListRow*> row) {
	_callback(row->peer());
}

auto ChooseRecipientBoxController::createRow(
		not_null<History*> history) -> std::unique_ptr<Row> {
	return std::make_unique<Row>(history);
}
