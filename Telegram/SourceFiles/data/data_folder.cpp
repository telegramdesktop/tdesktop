/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_folder.h"

#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_histories.h"
#include "data/data_changes.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "storage/storage_facade.h"
#include "core/application.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "styles/style_dialogs.h"

namespace Data {
namespace {

constexpr auto kLoadedChatsMinCount = 20;
constexpr auto kShowChatNamesCount = 8;

} // namespace

Folder::Folder(not_null<Data::Session*> owner, FolderId id)
: Entry(owner, Type::Folder)
, _id(id)
, _chatsList(
	&owner->session(),
	FilterId(),
	owner->session().serverConfig().pinnedDialogsInFolderMax.value())
, _name(tr::lng_archived_name(tr::now))
, _chatListNameSortKey(owner->nameSortKey(_name)) {
	indexNameParts();

	session().changes().peerUpdates(
		PeerUpdate::Flag::Name
	) | rpl::filter([=](const PeerUpdate &update) {
		return ranges::contains(_lastHistories, update.peer, &History::peer);
	}) | rpl::start_with_next([=] {
		++_chatListViewVersion;
		updateChatListEntryPostponed();
	}, _lifetime);

	_chatsList.setAllAreMuted(true);

	_chatsList.unreadStateChanges(
	) | rpl::filter([=] {
		return inChatList();
	}) | rpl::start_with_next([=](const Dialogs::UnreadState &old) {
		++_chatListViewVersion;
		notifyUnreadStateChange(old);
		updateChatListEntryPostponed();
	}, _lifetime);

	_chatsList.fullSize().changes(
	) | rpl::start_with_next([=] {
		updateChatListEntryPostponed();
	}, _lifetime);
}

void Folder::updateChatListEntryPostponed() {
	if (_updateChatListEntryPostponed) {
		return;
	}
	_updateChatListEntryPostponed = true;
	Ui::PostponeCall(this, [=] {
		updateChatListEntry();
		_updateChatListEntryPostponed = false;
	});
}

FolderId Folder::id() const {
	return _id;
}

void Folder::indexNameParts() {
	// We don't want archive to be filtered in the chats list.
	//_nameWords.clear();
	//_nameFirstLetters.clear();
	//auto toIndexList = QStringList();
	//auto appendToIndex = [&](const QString &value) {
	//	if (!value.isEmpty()) {
	//		toIndexList.push_back(TextUtilities::RemoveAccents(value));
	//	}
	//};

	//appendToIndex(_name);
	//const auto appendTranslit = !toIndexList.isEmpty()
	//	&& cRussianLetters().match(toIndexList.front()).hasMatch();
	//if (appendTranslit) {
	//	appendToIndex(translitRusEng(toIndexList.front()));
	//}
	//auto toIndex = toIndexList.join(' ');
	//toIndex += ' ' + rusKeyboardLayoutSwitch(toIndex);

	//const auto namesList = TextUtilities::PrepareSearchWords(toIndex);
	//for (const auto &name : namesList) {
	//	_nameWords.insert(name);
	//	_nameFirstLetters.insert(name[0]);
	//}
}

void Folder::registerOne(not_null<History*> history) {
	if (_chatsList.indexed()->size() == 1) {
		updateChatListSortPosition();
		if (!_chatsList.cloudUnreadKnown()) {
			owner().histories().requestDialogEntry(this);
		}
	} else {
		updateChatListEntry();
	}
	applyChatListMessage(history->chatListMessage());
	reorderLastHistories();
}

void Folder::unregisterOne(not_null<History*> history) {
	if (_chatsList.empty()) {
		updateChatListExistence();
	}
	if (_chatListMessage && _chatListMessage->history() == history) {
		computeChatListMessage();
	}
	reorderLastHistories();
}

void Folder::oneListMessageChanged(HistoryItem *from, HistoryItem *to) {
	if (!applyChatListMessage(to) && _chatListMessage == from) {
		computeChatListMessage();
	}
	if (from || to) {
		reorderLastHistories();
	}
}

bool Folder::applyChatListMessage(HistoryItem *item) {
	if (!item) {
		return false;
	} else if (_chatListMessage
		&& _chatListMessage->date() >= item->date()) {
		return false;
	}
	_chatListMessage = item;
	updateChatListEntry();
	return true;
}

void Folder::computeChatListMessage() {
	auto &&items = ranges::views::all(
		*_chatsList.indexed()
	) | ranges::views::filter([](not_null<Dialogs::Row*> row) {
		return row->entry()->chatListMessage() != nullptr;
	});
	const auto chatListDate = [](not_null<Dialogs::Row*> row) {
		return row->entry()->chatListMessage()->date();
	};
	const auto top = ranges::max_element(
		items,
		ranges::less(),
		chatListDate);
	if (top == items.end()) {
		_chatListMessage = nullptr;
	} else {
		_chatListMessage = (*top)->entry()->chatListMessage();
	}
	updateChatListEntry();
}

void Folder::reorderLastHistories() {
	// We want first kShowChatNamesCount histories, by last message date.
	const auto pred = [](not_null<History*> a, not_null<History*> b) {
		const auto aItem = a->chatListMessage();
		const auto bItem = b->chatListMessage();
		const auto aDate = aItem ? aItem->date() : TimeId(0);
		const auto bDate = bItem ? bItem->date() : TimeId(0);
		return aDate > bDate;
	};
	_lastHistories.erase(_lastHistories.begin(), _lastHistories.end());
	_lastHistories.reserve(kShowChatNamesCount + 1);
	auto &&histories = ranges::views::all(
		*_chatsList.indexed()
	) | ranges::views::transform([](not_null<Dialogs::Row*> row) {
		return row->history();
	}) | ranges::views::filter([](History *history) {
		return (history != nullptr);
	}) | ranges::views::transform([](History *history) {
		return not_null<History*>(history);
	});
	for (const auto history : histories) {
		const auto i = ranges::upper_bound(_lastHistories, history, pred);
		if (size(_lastHistories) < kShowChatNamesCount
			|| i != end(_lastHistories)) {
			_lastHistories.insert(i, history);
		}
		if (size(_lastHistories) > kShowChatNamesCount) {
			_lastHistories.pop_back();
		}
	}
	++_chatListViewVersion;
	updateChatListEntry();
}

not_null<Dialogs::MainList*> Folder::chatsList() {
	return &_chatsList;
}

void Folder::loadUserpic() {
}

void Folder::paintUserpic(
		Painter &p,
		std::shared_ptr<Data::CloudImageView> &view,
		int x,
		int y,
		int size) const {
	paintUserpic(p, x, y, size, nullptr, nullptr);
}

void Folder::paintUserpic(
		Painter &p,
		int x,
		int y,
		int size,
		const style::color &bg,
		const style::color &fg) const {
	paintUserpic(p, x, y, size, &bg, &fg);
}

void Folder::paintUserpic(
		Painter &p,
		int x,
		int y,
		int size,
		const style::color *overrideBg,
		const style::color *overrideFg) const {
	p.setPen(Qt::NoPen);
	p.setBrush(overrideBg ? *overrideBg : st::historyPeerArchiveUserpicBg);
	{
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(x, y, size, size);
	}
	if (size == st::dialogsPhotoSize) {
		const auto rect = QRect{ x, y, size, size };
		if (overrideFg) {
			st::dialogsArchiveUserpic.paintInCenter(
				p,
				rect,
				(*overrideFg)->c);
		} else {
			st::dialogsArchiveUserpic.paintInCenter(p, rect);
		}
	} else {
		p.save();
		const auto ratio = size / float64(st::dialogsPhotoSize);
		p.translate(x + size / 2., y + size / 2.);
		p.scale(ratio, ratio);
		const auto skip = st::dialogsPhotoSize;
		const auto rect = QRect{ -skip, -skip, 2 * skip, 2 * skip };
		if (overrideFg) {
			st::dialogsArchiveUserpic.paintInCenter(
				p,
				rect,
				(*overrideFg)->c);
		} else {
			st::dialogsArchiveUserpic.paintInCenter(p, rect);
		}
		p.restore();
	}
}

const std::vector<not_null<History*>> &Folder::lastHistories() const {
	return _lastHistories;
}

uint32 Folder::chatListViewVersion() const {
	return _chatListViewVersion;
}

void Folder::requestChatListMessage() {
	if (!chatListMessageKnown()) {
		owner().histories().requestDialogEntry(this);
	}
}

TimeId Folder::adjustedChatListTimeId() const {
	return _chatListMessage ? _chatListMessage->date() : chatListTimeId();
}

void Folder::applyDialog(const MTPDdialogFolder &data) {
	_chatsList.updateCloudUnread(data);
	if (const auto peerId = peerFromMTP(data.vpeer())) {
		const auto history = owner().history(peerId);
		const auto fullId = FullMsgId(
			peerToChannel(peerId),
			data.vtop_message().v);
		history->setFolder(this, owner().message(fullId));
	} else {
		_chatsList.clear();
		updateChatListExistence();
	}
	if (_chatsList.indexed()->size() < kLoadedChatsMinCount) {
		session().api().requestDialogs(this);
	}
}

void Folder::applyPinnedUpdate(const MTPDupdateDialogPinned &data) {
	const auto folderId = data.vfolder_id().value_or_empty();
	if (folderId != 0) {
		LOG(("API Error: Nested folders detected."));
	}
	owner().setChatPinned(this, FilterId(), data.is_pinned());
}

int Folder::fixedOnTopIndex() const {
	return kArchiveFixOnTopIndex;
}

bool Folder::shouldBeInChatList() const {
	return !_chatsList.empty();
}

int Folder::chatListUnreadCount() const {
	const auto state = chatListUnreadState();
	return state.marks
		+ (Core::App().settings().countUnreadMessages()
			? state.messages
			: state.chats);
}

Dialogs::UnreadState Folder::chatListUnreadState() const {
	return _chatsList.unreadState();
}

bool Folder::chatListUnreadMark() const {
	return false;
}

bool Folder::chatListMutedBadge() const {
	return true;
}

HistoryItem *Folder::chatListMessage() const {
	return _chatListMessage;
}

bool Folder::chatListMessageKnown() const {
	return true;
}

const QString &Folder::chatListName() const {
	return _name;
}

const base::flat_set<QString> &Folder::chatListNameWords() const {
	return _nameWords;
}

const base::flat_set<QChar> &Folder::chatListFirstLetters() const {
	return _nameFirstLetters;
}

const QString &Folder::chatListNameSortKey() const {
	return _chatListNameSortKey;
}

} // namespace Data
