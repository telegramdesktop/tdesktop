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
#include "dialogs/ui/dialogs_layout.h"
#include "history/history.h"
#include "history/history_item.h"
#include "ui/painter.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "lang/lang_keys.h"
#include "storage/storage_facade.h"
#include "core/application.h"
#include "core/core_settings.h"
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

[[nodiscard]] TextWithEntities ComposeFolderListEntryText(
		not_null<Folder*> folder) {
	const auto &list = folder->lastHistories();
	if (list.empty()) {
		if (const auto storiesUnread = folder->storiesUnreadCount()) {
			return {
				tr::lng_contacts_stories_status_new(
					tr::now,
					lt_count,
					storiesUnread),
			};
		} else if (const auto storiesCount = folder->storiesCount()) {
			return {
				tr::lng_contacts_stories_status(
					tr::now,
					lt_count,
					storiesCount),
			};
		}
		return {};
	}

	const auto count = std::max(
		int(list.size()),
		folder->chatsList()->fullSize().current());

	const auto throwAwayLastName = (list.size() > 1)
		&& (count == list.size() + 1);
	auto &&peers = ranges::views::all(
		list
	) | ranges::views::take(
		list.size() - (throwAwayLastName ? 1 : 0)
	);
	const auto wrapName = [](not_null<History*> history) {
		const auto name = history->peer->name();
		return TextWithEntities{
			.text = name,
			.entities = (history->chatListBadgesState().unread
				? EntitiesInText{
					{ EntityType::Semibold, 0, int(name.size()), QString() },
					{ EntityType::Colorized, 0, int(name.size()), QString() },
				}
				: EntitiesInText{}),
		};
	};
	const auto shown = int(peers.size());
	const auto accumulated = [&] {
		Expects(shown > 0);

		auto i = peers.begin();
		auto result = wrapName(*i);
		for (++i; i != peers.end(); ++i) {
			result = tr::lng_archived_last_list(
				tr::now,
				lt_accumulated,
				result,
				lt_chat,
				wrapName(*i),
				Ui::Text::WithEntities);
		}
		return result;
	}();
	return (shown < count)
		? tr::lng_archived_last(
			tr::now,
			lt_count,
			(count - shown),
			lt_chats,
			accumulated,
			Ui::Text::WithEntities)
		: accumulated;
}

} // namespace

Folder::Folder(not_null<Session*> owner, FolderId id)
: Entry(owner, Type::Folder)
, _id(id)
, _chatsList(
	&owner->session(),
	FilterId(),
	owner->maxPinnedChatsLimitValue(this))
, _name(tr::lng_archived_name(tr::now)) {
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
	}, _lifetime);

	_chatsList.fullSize().changes(
	) | rpl::start_with_next([=] {
		updateChatListEntryPostponed();
	}, _lifetime);
}

FolderId Folder::id() const {
	return _id;
}

void Folder::indexNameParts() {
	// We don't want archive to be filtered in the chats list.
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
	reorderLastHistories();
}

void Folder::unregisterOne(not_null<History*> history) {
	if (_chatsList.empty()) {
		updateChatListExistence();
	}
	reorderLastHistories();
}

int Folder::chatListNameVersion() const {
	return 1;
}

void Folder::oneListMessageChanged(HistoryItem *from, HistoryItem *to) {
	if (from || to) {
		reorderLastHistories();
	}
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
	_lastHistories.clear();
	_lastHistories.reserve(kShowChatNamesCount + 1);
	auto &&histories = ranges::views::all(
		*_chatsList.indexed()
	) | ranges::views::transform([](not_null<Dialogs::Row*> row) {
		return row->history();
	}) | ranges::views::filter([](History *history) {
		return (history != nullptr);
	});
	auto nonPinnedChecked = 0;
	for (const auto history : histories) {
		const auto i = ranges::upper_bound(
			_lastHistories,
			not_null(history),
			pred);
		if (size(_lastHistories) < kShowChatNamesCount
			|| i != end(_lastHistories)) {
			_lastHistories.insert(i, history);
		}
		if (size(_lastHistories) > kShowChatNamesCount) {
			_lastHistories.pop_back();
		}
		if (!history->isPinnedDialog(FilterId())
			&& ++nonPinnedChecked >= kShowChatNamesCount) {
			break;
		}
	}
	++_chatListViewVersion;
	updateChatListEntry();
}

not_null<Dialogs::MainList*> Folder::chatsList() {
	return &_chatsList;
}

void Folder::clearChatsList() {
	_chatsList.clear();
}

void Folder::chatListPreloadData() {
}

void Folder::paintUserpic(
		Painter &p,
		Ui::PeerUserpicView &view,
		const Dialogs::Ui::PaintContext &context) const {
	paintUserpic(
		p,
		context.st->padding.left(),
		context.st->padding.top(),
		context.st->photoSize);
}

void Folder::paintUserpic(Painter &p, int x, int y, int size) const {
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
	if (size == st::defaultDialogRow.photoSize) {
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
		const auto ratio = size / float64(st::defaultDialogRow.photoSize);
		p.translate(x + size / 2., y + size / 2.);
		p.scale(ratio, ratio);
		const auto skip = st::defaultDialogRow.photoSize;
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

void Folder::validateListEntryCache() {
	if (_listEntryCacheVersion == _chatListViewVersion) {
		return;
	}
	_listEntryCacheVersion = _chatListViewVersion;
	_listEntryCache.setMarkedText(
		st::dialogsTextStyle,
		ComposeFolderListEntryText(this),
		// Use rich options as long as the entry text does not have user text.
		Ui::ItemTextDefaultOptions());
}

void Folder::updateStoriesCount(int count, int unread) {
	if (_storiesCount == count && _storiesUnreadCount == unread) {
		return;
	}
	const auto limit = (1 << 16) - 1;
	const auto was = (_storiesCount > 0);
	_storiesCount = std::min(count, limit);
	_storiesUnreadCount = std::min(unread, limit);
	const auto now = (_storiesCount > 0);
	if (was == now) {
		updateChatListEntryPostponed();
	} else if (now) {
		updateChatListSortPosition();
	} else {
		updateChatListExistence();
	}
	++_chatListViewVersion;
}

int Folder::storiesCount() const {
	return _storiesCount;
}

int Folder::storiesUnreadCount() const {
	return _storiesUnreadCount;
}

void Folder::requestChatListMessage() {
	if (!chatListMessageKnown()) {
		owner().histories().requestDialogEntry(this);
	}
}

TimeId Folder::adjustedChatListTimeId() const {
	return chatListTimeId();
}

void Folder::applyDialog(const MTPDdialogFolder &data) {
	_chatsList.updateCloudUnread(data);
	if (const auto peerId = peerFromMTP(data.vpeer())) {
		const auto history = owner().history(peerId);
		const auto fullId = FullMsgId(peerId, data.vtop_message().v);
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
	return !_chatsList.empty() || (_storiesCount > 0);
}

Dialogs::UnreadState Folder::chatListUnreadState() const {
	return _chatsList.unreadState();
}

Dialogs::BadgesState Folder::chatListBadgesState() const {
	auto result = Dialogs::BadgesForUnread(
		chatListUnreadState(),
		Dialogs::CountInBadge::Chats,
		Dialogs::IncludeInBadge::All);
	result.unreadMuted = result.mentionMuted = result.reactionMuted = true;
	if (result.unread && !result.unreadCounter) {
		result.unreadCounter = 1;
	}
	return result;
}

HistoryItem *Folder::chatListMessage() const {
	return nullptr;
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
	static const auto empty = QString();
	return empty;
}

} // namespace Data
