/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history.h"

#include "history/view/history_view_element.h"
#include "history/view/history_view_item_preview.h"
#include "history/view/history_view_translate_tracker.h"
#include "dialogs/dialogs_indexed_list.h"
#include "history/history_inner_widget.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "history/history_translation.h"
#include "history/history_unread_things.h"
#include "dialogs/ui/dialogs_layout.h"
#include "data/notify/data_notify_settings.h"
#include "data/stickers/data_stickers.h"
#include "data/data_drafts.h"
#include "data/data_session.h"
#include "data/data_media_types.h"
#include "data/data_channel_admins.h"
#include "data/data_changes.h"
#include "data/data_chat_filters.h"
#include "data/data_scheduled_messages.h"
#include "data/data_sponsored_messages.h"
#include "data/data_send_action.h"
#include "data/data_folder.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_photo.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_document.h"
#include "data/data_histories.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "api/api_chat_participants.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "window/notifications_manager.h"
#include "calls/calls_instance.h"
#include "spellcheck/spellcheck_types.h"
#include "storage/localstorage.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "storage/storage_account.h"
#include "support/support_helper.h"
#include "ui/image/image.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "payments/payments_checkout_process.h"
#include "core/crash_reports.h"
#include "core/application.h"
#include "base/unixtime.h"
#include "base/qt/qt_common_adapters.h"
#include "styles/style_dialogs.h"

namespace {

constexpr auto kNewBlockEachMessage = 50;
constexpr auto kSkipCloudDraftsFor = TimeId(2);

using UpdateFlag = Data::HistoryUpdate::Flag;

} // namespace

History::History(not_null<Data::Session*> owner, PeerId peerId)
: Thread(owner, Type::History)
, peer(owner->peer(peerId))
, _delegateMixin(HistoryInner::DelegateMixin())
, _chatListNameSortKey(owner->nameSortKey(peer->name()))
, _sendActionPainter(this) {
	Thread::setMuted(owner->notifySettings().isMuted(peer));

	if (const auto user = peer->asUser()) {
		if (user->isBot()) {
			_outboxReadBefore = std::numeric_limits<MsgId>::max();
		}
	}
}

History::~History() = default;

void History::clearLastKeyboard() {
	if (lastKeyboardId) {
		if (lastKeyboardId == lastKeyboardHiddenId) {
			lastKeyboardHiddenId = 0;
		}
		lastKeyboardId = 0;
		session().changes().historyUpdated(this, UpdateFlag::BotKeyboard);
	}
	lastKeyboardInited = true;
	lastKeyboardFrom = 0;
}

int History::height() const {
	return _height;
}

bool History::hasPendingResizedItems() const {
	return _flags & Flag::HasPendingResizedItems;
}

void History::setHasPendingResizedItems() {
	_flags |= Flag::HasPendingResizedItems;
}

void History::itemRemoved(not_null<HistoryItem*> item) {
	if (item == _joinedMessage) {
		_joinedMessage = nullptr;
	}
	item->removeMainView();
	if (_lastServerMessage == item) {
		_lastServerMessage = std::nullopt;
	}
	if (lastMessage() == item) {
		_lastMessage = std::nullopt;
		if (loadedAtBottom()) {
			if (const auto last = lastAvailableMessage()) {
				setLastMessage(last);
			}
		}
	}
	checkChatListMessageRemoved(item);
	itemVanished(item);
	if (IsClientMsgId(item->id)) {
		unregisterClientSideMessage(item);
	}
	if (const auto topic = item->topic()) {
		topic->applyItemRemoved(item->id);
	}
	if (const auto chat = peer->asChat()) {
		if (const auto to = chat->getMigrateToChannel()) {
			if (const auto history = owner().historyLoaded(to)) {
				history->checkChatListMessageRemoved(item);
			}
		}
	}
}

void History::checkChatListMessageRemoved(not_null<HistoryItem*> item) {
	if (chatListMessage() != item) {
		return;
	}
	setChatListMessageUnknown();
	refreshChatListMessage();
}

void History::itemVanished(not_null<HistoryItem*> item) {
	item->notificationThread()->removeNotification(item);
	if (lastKeyboardId == item->id) {
		clearLastKeyboard();
	}
	if ((!item->out() || item->isPost())
		&& item->unread(this)
		&& unreadCount() > 0) {
		setUnreadCount(unreadCount() - 1);
	}
}

void History::takeLocalDraft(not_null<History*> from) {
	const auto topicRootId = MsgId(0);
	const auto i = from->_drafts.find(Data::DraftKey::Local(topicRootId));
	if (i == end(from->_drafts)) {
		return;
	}
	auto &draft = i->second;
	if (!draft->textWithTags.text.isEmpty()
		&& !_drafts.contains(Data::DraftKey::Local(topicRootId))) {
		// Edit and reply to drafts can't migrate.
		// Cloud drafts do not migrate automatically.
		draft->msgId = 0;

		setLocalDraft(std::move(draft));
	}
	from->clearLocalDraft(topicRootId);
	session().api().saveDraftToCloudDelayed(from);
}

void History::createLocalDraftFromCloud(MsgId topicRootId) {
	const auto draft = cloudDraft(topicRootId);
	if (!draft) {
		clearLocalDraft(topicRootId);
		return;
	} else if (Data::DraftIsNull(draft) || !draft->date) {
		return;
	}

	auto existing = localDraft(topicRootId);
	if (Data::DraftIsNull(existing)
		|| !existing->date
		|| draft->date >= existing->date) {
		if (!existing) {
			setLocalDraft(std::make_unique<Data::Draft>(
				draft->textWithTags,
				draft->msgId,
				topicRootId,
				draft->cursor,
				draft->previewState));
			existing = localDraft(topicRootId);
		} else if (existing != draft) {
			existing->textWithTags = draft->textWithTags;
			existing->msgId = draft->msgId;
			existing->topicRootId = draft->topicRootId;
			existing->cursor = draft->cursor;
			existing->previewState = draft->previewState;
		}
		existing->date = draft->date;
	}
}

Data::Draft *History::draft(Data::DraftKey key) const {
	if (!key) {
		return nullptr;
	}
	const auto i = _drafts.find(key);
	return (i != _drafts.end()) ? i->second.get() : nullptr;
}

void History::setDraft(
		Data::DraftKey key,
		std::unique_ptr<Data::Draft> &&draft) {
	if (!key) {
		return;
	}
	const auto cloudThread = key.isCloud()
		? threadFor(key.topicRootId())
		: nullptr;
	if (cloudThread) {
		cloudThread->cloudDraftTextCache().clear();
	}
	if (draft) {
		_drafts[key] = std::move(draft);
	} else if (_drafts.remove(key) && cloudThread) {
		cloudThread->updateChatListSortPosition();
	}
}

const Data::HistoryDrafts &History::draftsMap() const {
	return _drafts;
}

void History::setDraftsMap(Data::HistoryDrafts &&map) {
	for (auto &[key, draft] : _drafts) {
		map[key] = std::move(draft);
	}
	_drafts = std::move(map);
}

void History::clearDraft(Data::DraftKey key) {
	setDraft(key, nullptr);
}

void History::clearDrafts() {
	for (auto &[key, draft] : base::take(_drafts)) {
		const auto cloudThread = key.isCloud()
			? threadFor(key.topicRootId())
			: nullptr;
		if (cloudThread) {
			cloudThread->cloudDraftTextCache().clear();
			cloudThread->updateChatListSortPosition();
		}
	}
}

Data::Draft *History::createCloudDraft(
		MsgId topicRootId,
		const Data::Draft *fromDraft) {
	if (Data::DraftIsNull(fromDraft)) {
		setCloudDraft(std::make_unique<Data::Draft>(
			TextWithTags(),
			0,
			topicRootId,
			MessageCursor(),
			Data::PreviewState::Allowed));
		cloudDraft(topicRootId)->date = TimeId(0);
	} else {
		auto existing = cloudDraft(topicRootId);
		if (!existing) {
			setCloudDraft(std::make_unique<Data::Draft>(
				fromDraft->textWithTags,
				fromDraft->msgId,
				topicRootId,
				fromDraft->cursor,
				fromDraft->previewState));
			existing = cloudDraft(topicRootId);
		} else if (existing != fromDraft) {
			existing->textWithTags = fromDraft->textWithTags;
			existing->msgId = fromDraft->msgId;
			existing->cursor = fromDraft->cursor;
			existing->previewState = fromDraft->previewState;
		}
		existing->date = base::unixtime::now();
	}

	if (const auto thread = threadFor(topicRootId)) {
		thread->cloudDraftTextCache().clear();
		thread->updateChatListSortPosition();
	}

	return cloudDraft(topicRootId);
}

bool History::skipCloudDraftUpdate(MsgId topicRootId, TimeId date) const {
	const auto i = _acceptCloudDraftsAfter.find(topicRootId);
	return _savingCloudDraftRequests.contains(topicRootId)
		|| (i != _acceptCloudDraftsAfter.end() && date < i->second);
}

void History::startSavingCloudDraft(MsgId topicRootId) {
	++_savingCloudDraftRequests[topicRootId];
}

void History::finishSavingCloudDraft(MsgId topicRootId, TimeId savedAt) {
	const auto i = _savingCloudDraftRequests.find(topicRootId);
	if (i != _savingCloudDraftRequests.end()) {
		if (--i->second <= 0) {
			_savingCloudDraftRequests.erase(i);
		}
	}
	auto &after = _acceptCloudDraftsAfter[topicRootId];
	after = std::max(after, savedAt + kSkipCloudDraftsFor);
}

void History::applyCloudDraft(MsgId topicRootId) {
	if (!topicRootId && session().supportMode()) {
		updateChatListEntry();
		session().supportHelper().cloudDraftChanged(this);
	} else {
		createLocalDraftFromCloud(topicRootId);
		if (const auto thread = threadFor(topicRootId)) {
			thread->updateChatListSortPosition();
			if (!topicRootId) {
				session().changes().historyUpdated(
					this,
					UpdateFlag::CloudDraft);
			} else {
				session().changes().topicUpdated(
					thread->asTopic(),
					Data::TopicUpdate::Flag::CloudDraft);
			}
		}
	}
}

void History::draftSavedToCloud(MsgId topicRootId) {
	if (const auto thread = threadFor(topicRootId)) {
		thread->updateChatListEntry();
	}
	session().local().writeDrafts(this);
}

const Data::ForwardDraft &History::forwardDraft(
		MsgId topicRootId) const {
	static const auto kEmpty = Data::ForwardDraft();
	const auto i = _forwardDrafts.find(topicRootId);
	return (i != end(_forwardDrafts)) ? i->second : kEmpty;
}

Data::ResolvedForwardDraft History::resolveForwardDraft(
		const Data::ForwardDraft &draft) const {
	return Data::ResolvedForwardDraft{
		.items = owner().idsToItems(draft.ids),
		.options = draft.options,
	};
}

Data::ResolvedForwardDraft History::resolveForwardDraft(
		MsgId topicRootId) {
	const auto &draft = forwardDraft(topicRootId);
	auto result = resolveForwardDraft(draft);
	if (result.items.size() != draft.ids.size()) {
		setForwardDraft(topicRootId, {
			.ids = owner().itemsToIds(result.items),
			.options = result.options,
		});
	}
	return result;
}

void History::setForwardDraft(
		MsgId topicRootId,
		Data::ForwardDraft &&draft) {
	auto changed = false;
	if (draft.ids.empty()) {
		changed = _forwardDrafts.remove(topicRootId);
	} else {
		auto &now = _forwardDrafts[topicRootId];
		if (now != draft) {
			now = std::move(draft);
			changed = true;
		}
	}
	if (changed) {
		const auto entry = topicRootId
			? peer->forumTopicFor(topicRootId)
			: (Dialogs::Entry*)this;
		if (entry) {
			session().changes().entryUpdated(
				entry,
				Data::EntryUpdate::Flag::ForwardDraft);
		}
	}
}

not_null<HistoryItem*> History::createItem(
		MsgId id,
		const MTPMessage &message,
		MessageFlags localFlags,
		bool detachExistingItem) {
	if (const auto result = owner().message(peer, id)) {
		if (detachExistingItem) {
			result->removeMainView();
		}
		return result;
	}
	return message.match([&](const auto &data) {
		return makeMessage(id, data, localFlags);
	});
}

std::vector<not_null<HistoryItem*>> History::createItems(
		const QVector<MTPMessage> &data) {
	auto result = std::vector<not_null<HistoryItem*>>();
	result.reserve(data.size());
	const auto localFlags = MessageFlags();
	const auto detachExistingItem = true;
	for (auto i = data.cend(), e = data.cbegin(); i != e;) {
		const auto &data = *--i;
		result.emplace_back(createItem(
			IdFromMessage(data),
			data,
			localFlags,
			detachExistingItem));
	}
	return result;
}

not_null<HistoryItem*> History::addNewMessage(
		MsgId id,
		const MTPMessage &msg,
		MessageFlags localFlags,
		NewMessageType type) {
	const auto detachExistingItem = (type == NewMessageType::Unread);
	const auto item = createItem(id, msg, localFlags, detachExistingItem);
	if (type == NewMessageType::Existing || item->mainView()) {
		return item;
	}
	const auto unread = (type == NewMessageType::Unread);
	if (unread && item->isHistoryEntry()) {
		applyMessageChanges(item, msg);
	}
	return addNewItem(item, unread);
}

not_null<HistoryItem*> History::insertItem(
		std::unique_ptr<HistoryItem> item) {
	Expects(item != nullptr);

	const auto [i, ok] = _messages.insert(std::move(item));

	const auto result = i->get();
	owner().registerMessage(result);

	Ensures(ok);
	return result;
}

void History::destroyMessage(not_null<HistoryItem*> item) {
	Expects(item->isHistoryEntry() || !item->mainView());

	const auto peerId = peer->id;
	if (item->isHistoryEntry()) {
		// All this must be done for all items manually in History::clear()!
		item->destroyHistoryEntry();
		if (item->isRegular()) {
			if (const auto types = item->sharedMediaTypes()) {
				session().storage().remove(Storage::SharedMediaRemoveOne(
					peerId,
					types,
					item->id));
			}
		}
		itemRemoved(item);
	}
	if (item->isSending()) {
		session().api().cancelLocalItem(item);
	}

	const auto documentToCancel = [&] {
		const auto media = item->isAdminLogEntry()
			? nullptr
			: item->media();
		return media ? media->document() : nullptr;
	}();

	owner().unregisterMessage(item);
	Core::App().notifications().clearFromItem(item);

	auto hack = std::unique_ptr<HistoryItem>(item.get());
	const auto i = _messages.find(hack);
	hack.release();

	Assert(i != end(_messages));
	_messages.erase(i);

	if (documentToCancel) {
		session().data().documentMessageRemoved(documentToCancel);
	}
}

void History::destroyMessagesByDates(TimeId minDate, TimeId maxDate) {
	auto toDestroy = std::vector<not_null<HistoryItem*>>();
	toDestroy.reserve(_messages.size());
	for (const auto &message : _messages) {
		if (message->isRegular()
			&& message->date() > minDate
			&& message->date() < maxDate) {
			toDestroy.push_back(message.get());
		}
	}
	for (const auto item : toDestroy) {
		item->destroy();
	}
}

void History::destroyMessagesByTopic(MsgId topicRootId) {
	auto toDestroy = std::vector<not_null<HistoryItem*>>();
	toDestroy.reserve(_messages.size());
	for (const auto &message : _messages) {
		if (message->topicRootId() == topicRootId) {
			toDestroy.push_back(message.get());
		}
	}
	for (const auto item : toDestroy) {
		item->destroy();
	}
}

void History::unpinMessagesFor(MsgId topicRootId) {
	if (!topicRootId) {
		session().storage().remove(
			Storage::SharedMediaRemoveAll(
				peer->id,
				Storage::SharedMediaType::Pinned));
		setHasPinnedMessages(false);
		if (const auto forum = peer->forum()) {
			forum->enumerateTopics([](not_null<Data::ForumTopic*> topic) {
				topic->setHasPinnedMessages(false);
			});
		}
		for (const auto &item : _messages) {
			if (item->isPinned()) {
				item->setIsPinned(false);
			}
		}
	} else {
		session().storage().remove(
			Storage::SharedMediaRemoveAll(
				peer->id,
				topicRootId,
				Storage::SharedMediaType::Pinned));
		if (const auto topic = peer->forumTopicFor(topicRootId)) {
			topic->setHasPinnedMessages(false);
		}
		for (const auto &item : _messages) {
			if (item->isPinned() && item->topicRootId() == topicRootId) {
				item->setIsPinned(false);
			}
		}
	}
}

not_null<HistoryItem*> History::addNewItem(
		not_null<HistoryItem*> item,
		bool unread) {
	if (item->isScheduled()) {
		owner().scheduledMessages().appendSending(item);
		return item;
	} else if (!item->isHistoryEntry()) {
		return item;
	}

	// In case we've loaded a new 'last' message
	// and it is not in blocks and we think that
	// we have all the messages till the bottom
	// we should unload known history or mark
	// currently loaded slice as not reaching bottom.
	const auto shouldMarkBottomNotLoaded = loadedAtBottom()
		&& !unread
		&& !isEmpty();
	if (shouldMarkBottomNotLoaded) {
		setNotLoadedAtBottom();
	}

	if (!loadedAtBottom() || peer->migrateTo()) {
		setLastMessage(item);
		if (unread) {
			newItemAdded(item);
		}
	} else {
		addNewToBack(item, unread);
		checkForLoadedAtTop(item);
	}
	return item;
}

void History::checkForLoadedAtTop(not_null<HistoryItem*> added) {
	if (peer->isChat()) {
		if (added->isGroupEssential() && !added->isGroupMigrate()) {
			// We added the first message about group creation.
			_loadedAtTop = true;
			addEdgesToSharedMedia();
		}
	} else if (peer->isChannel()) {
		if (added->id == 1) {
			_loadedAtTop = true;
			checkLocalMessages();
			addEdgesToSharedMedia();
		}
	}
}

not_null<HistoryItem*> History::addNewLocalMessage(
		MsgId id,
		MessageFlags flags,
		UserId viaBotId,
		FullReplyTo replyTo,
		TimeId date,
		PeerId from,
		const QString &postAuthor,
		const TextWithEntities &text,
		const MTPMessageMedia &media,
		HistoryMessageMarkupData &&markup,
		uint64 groupedId) {
	return addNewItem(
		makeMessage(
			id,
			flags | MessageFlag::Local,
			replyTo,
			viaBotId,
			date,
			from,
			postAuthor,
			text,
			media,
			std::move(markup),
			groupedId),
		true);
}

not_null<HistoryItem*> History::addNewLocalMessage(
		MsgId id,
		MessageFlags flags,
		TimeId date,
		PeerId from,
		const QString &postAuthor,
		not_null<HistoryItem*> forwardOriginal,
		MsgId topicRootId) {
	return addNewItem(
		makeMessage(
			id,
			flags | MessageFlag::Local,
			date,
			from,
			postAuthor,
			forwardOriginal,
			topicRootId),
		true);
}

not_null<HistoryItem*> History::addNewLocalMessage(
		MsgId id,
		MessageFlags flags,
		UserId viaBotId,
		FullReplyTo replyTo,
		TimeId date,
		PeerId from,
		const QString &postAuthor,
		not_null<DocumentData*> document,
		const TextWithEntities &caption,
		HistoryMessageMarkupData &&markup) {
	return addNewItem(
		makeMessage(
			id,
			flags | MessageFlag::Local,
			replyTo,
			viaBotId,
			date,
			from,
			postAuthor,
			document,
			caption,
			std::move(markup)),
		true);
}

not_null<HistoryItem*> History::addNewLocalMessage(
		MsgId id,
		MessageFlags flags,
		UserId viaBotId,
		FullReplyTo replyTo,
		TimeId date,
		PeerId from,
		const QString &postAuthor,
		not_null<PhotoData*> photo,
		const TextWithEntities &caption,
		HistoryMessageMarkupData &&markup) {
	return addNewItem(
		makeMessage(
			id,
			flags | MessageFlag::Local,
			replyTo,
			viaBotId,
			date,
			from,
			postAuthor,
			photo,
			caption,
			std::move(markup)),
		true);
}

not_null<HistoryItem*> History::addNewLocalMessage(
		MsgId id,
		MessageFlags flags,
		UserId viaBotId,
		FullReplyTo replyTo,
		TimeId date,
		PeerId from,
		const QString &postAuthor,
		not_null<GameData*> game,
		HistoryMessageMarkupData &&markup) {
	return addNewItem(
		makeMessage(
			id,
			flags | MessageFlag::Local,
			replyTo,
			viaBotId,
			date,
			from,
			postAuthor,
			game,
			std::move(markup)),
		true);
}

not_null<HistoryItem*> History::addNewLocalMessage(
		MsgId id,
		Data::SponsoredFrom from,
		const TextWithEntities &textWithEntities) {
	return addNewItem(
		makeMessage(
			id,
			from,
			textWithEntities,
			nullptr),
		true);
}

void History::clearUnreadMentionsFor(MsgId topicRootId) {
	const auto forum = peer->forum();
	if (!topicRootId) {
		if (forum) {
			forum->clearAllUnreadMentions();
		}
		unreadMentions().clear();
		return;
	} else if (forum) {
		if (const auto topic = forum->topicFor(topicRootId)) {
			topic->unreadMentions().clear();
		}
	}
	const auto &ids = unreadMentionsIds();
	if (ids.empty()) {
		return;
	}
	const auto owner = &this->owner();
	const auto peerId = peer->id;
	auto items = base::flat_set<MsgId>();
	items.reserve(ids.size());
	for (const auto &id : ids) {
		if (const auto item = owner->message(peerId, id)) {
			if (item->topicRootId() == topicRootId) {
				items.emplace(id);
			}
		}
	}
	for (const auto &id : items) {
		unreadMentions().erase(id);
	}
}

void History::clearUnreadReactionsFor(MsgId topicRootId) {
	const auto forum = peer->forum();
	if (!topicRootId) {
		if (forum) {
			forum->clearAllUnreadReactions();
		}
		unreadReactions().clear();
		return;
	} else if (forum) {
		if (const auto topic = forum->topicFor(topicRootId)) {
			topic->unreadReactions().clear();
		}
	}
	const auto &ids = unreadReactionsIds();
	if (ids.empty()) {
		return;
	}
	const auto owner = &this->owner();
	const auto peerId = peer->id;
	auto items = base::flat_set<MsgId>();
	items.reserve(ids.size());
	for (const auto &id : ids) {
		if (const auto item = owner->message(peerId, id)) {
			if (item->topicRootId() == topicRootId) {
				items.emplace(id);
			}
		}
	}
	for (const auto &id : items) {
		unreadReactions().erase(id);
	}
}

not_null<HistoryItem*> History::addNewToBack(
		not_null<HistoryItem*> item,
		bool unread) {
	Expects(!isBuildingFrontBlock());

	addItemToBlock(item);

	if (!unread && item->isRegular()) {
		if (const auto types = item->sharedMediaTypes()) {
			auto from = loadedAtTop() ? 0 : minMsgId();
			auto till = loadedAtBottom() ? ServerMaxMsgId : maxMsgId();
			auto &storage = session().storage();
			storage.add(Storage::SharedMediaAddExisting(
				peer->id,
				MsgId(0), // topicRootId
				types,
				item->id,
				{ from, till }));
			const auto pinned = types.test(Storage::SharedMediaType::Pinned);
			if (pinned) {
				setHasPinnedMessages(true);
			}
			if (const auto topic = item->topic()) {
				storage.add(Storage::SharedMediaAddExisting(
					peer->id,
					topic->rootId(),
					types,
					item->id,
					{ item->id, item->id}));
				if (pinned) {
					topic->setHasPinnedMessages(true);
				}
			}
		}
	}
	if (item->from()->id) {
		if (auto user = item->from()->asUser()) {
			auto getLastAuthors = [this]() -> std::deque<not_null<UserData*>>* {
				if (auto chat = peer->asChat()) {
					return &chat->lastAuthors;
				} else if (auto channel = peer->asMegagroup()) {
					return channel->canViewMembers()
						? &channel->mgInfo->lastParticipants
						: nullptr;
				}
				return nullptr;
			};
			if (auto megagroup = peer->asMegagroup()) {
				if (user->isBot()) {
					auto mgInfo = megagroup->mgInfo.get();
					Assert(mgInfo != nullptr);
					mgInfo->bots.insert(user);
					if (mgInfo->botStatus != 0 && mgInfo->botStatus < 2) {
						mgInfo->botStatus = 2;
					}
				}
			}
			if (auto lastAuthors = getLastAuthors()) {
				auto prev = ranges::find(
					*lastAuthors,
					user,
					[](not_null<UserData*> user) { return user.get(); });
				auto index = (prev != lastAuthors->end())
					? (lastAuthors->end() - prev)
					: -1;
				if (index > 0) {
					lastAuthors->erase(prev);
				} else if (index < 0 && peer->isMegagroup()) { // nothing is outdated if just reordering
					// admins information outdated
				}
				if (index) {
					lastAuthors->push_front(user);
				}
				if (auto megagroup = peer->asMegagroup()) {
					session().changes().peerUpdated(
						peer,
						Data::PeerUpdate::Flag::Members);
					owner().addNewMegagroupParticipant(megagroup, user);
				}
			}
		}
		if (item->definesReplyKeyboard()) {
			auto markupFlags = item->replyKeyboardFlags();
			if (!(markupFlags & ReplyMarkupFlag::Selective)
				|| item->mentionsMe()) {
				auto getMarkupSenders = [this]() -> base::flat_set<not_null<PeerData*>>* {
					if (auto chat = peer->asChat()) {
						return &chat->markupSenders;
					} else if (auto channel = peer->asMegagroup()) {
						return &channel->mgInfo->markupSenders;
					}
					return nullptr;
				};
				if (auto markupSenders = getMarkupSenders()) {
					markupSenders->insert(item->from());
				}
				if (markupFlags & ReplyMarkupFlag::None) {
					// None markup means replyKeyboardHide.
					if (lastKeyboardFrom == item->from()->id
						|| (!lastKeyboardInited
							&& !peer->isChat()
							&& !peer->isMegagroup()
							&& !item->out())) {
						clearLastKeyboard();
					}
				} else {
					bool botNotInChat = false;
					if (peer->isChat()) {
						botNotInChat = item->from()->isUser()
							&& (!peer->asChat()->participants.empty()
								|| !Data::CanSendAnything(peer))
							&& !peer->asChat()->participants.contains(
								item->from()->asUser());
					} else if (peer->isMegagroup()) {
						botNotInChat = item->from()->isUser()
							&& (peer->asChannel()->mgInfo->botStatus != 0
								|| !Data::CanSendAnything(peer))
							&& !peer->asChannel()->mgInfo->bots.contains(
								item->from()->asUser());
					}
					if (botNotInChat) {
						clearLastKeyboard();
					} else {
						lastKeyboardInited = true;
						lastKeyboardId = item->id;
						lastKeyboardFrom = item->from()->id;
						lastKeyboardUsed = false;
					}
				}
			}
		}
	}

	setLastMessage(item);
	if (unread) {
		newItemAdded(item);
	}

	owner().notifyHistoryChangeDelayed(this);
	return item;
}

void History::applyMessageChanges(
		not_null<HistoryItem*> item,
		const MTPMessage &data) {
	if (data.type() == mtpc_messageService) {
		applyServiceChanges(item, data.c_messageService());
	}
	owner().stickers().checkSavedGif(item);
	session().changes().messageUpdated(
		item,
		Data::MessageUpdate::Flag::NewAdded);
}

void History::applyServiceChanges(
		not_null<HistoryItem*> item,
		const MTPDmessageService &data) {
	const auto replyTo = data.vreply_to();
	const auto processJoinedUser = [&](
			not_null<ChannelData*> megagroup,
			not_null<MegagroupInfo*> mgInfo,
			not_null<UserData*> user) {
		if (!base::contains(mgInfo->lastParticipants, user)
			&& megagroup->canViewMembers()) {
			mgInfo->lastParticipants.push_front(user);
			session().changes().peerUpdated(
				peer,
				Data::PeerUpdate::Flag::Members);
			owner().addNewMegagroupParticipant(megagroup, user);
		}
		if (user->isBot()) {
			mgInfo->bots.insert(user);
			if (mgInfo->botStatus != 0 && mgInfo->botStatus < 2) {
				mgInfo->botStatus = 2;
			}
		}
	};
	const auto processJoinedPeer = [&](not_null<PeerData*> joined) {
		if (const auto megagroup = peer->asMegagroup()) {
			const auto mgInfo = megagroup->mgInfo.get();
			Assert(mgInfo != nullptr);
			if (const auto user = joined->asUser()) {
				processJoinedUser(megagroup, mgInfo, user);
			}
		}
	};
	data.vaction().match([&](const MTPDmessageActionChatAddUser &data) {
		if (const auto megagroup = peer->asMegagroup()) {
			const auto mgInfo = megagroup->mgInfo.get();
			Assert(mgInfo != nullptr);
			for (const auto &userId : data.vusers().v) {
				if (const auto user = owner().userLoaded(userId.v)) {
					processJoinedUser(megagroup, mgInfo, user);
				}
			}
		}
	}, [&](const MTPDmessageActionChatJoinedByLink &data) {
		processJoinedPeer(item->from());
	}, [&](const MTPDmessageActionChatDeletePhoto &data) {
		if (const auto chat = peer->asChat()) {
			chat->setPhoto(MTP_chatPhotoEmpty());
		}
	}, [&](const MTPDmessageActionChatDeleteUser &data) {
		const auto uid = data.vuser_id().v;
		if (lastKeyboardFrom == peerFromUser(uid)) {
			clearLastKeyboard();
		}
		if (const auto megagroup = peer->asMegagroup()) {
			if (const auto user = owner().userLoaded(uid)) {
				const auto mgInfo = megagroup->mgInfo.get();
				Assert(mgInfo != nullptr);
				const auto i = ranges::find(
					mgInfo->lastParticipants,
					user,
					[](not_null<UserData*> user) { return user.get(); });
				if (i != mgInfo->lastParticipants.end()) {
					mgInfo->lastParticipants.erase(i);
					session().changes().peerUpdated(
						peer,
						Data::PeerUpdate::Flag::Members);
				}
				owner().removeMegagroupParticipant(megagroup, user);
				if (megagroup->membersCount() > 1) {
					megagroup->setMembersCount(
						megagroup->membersCount() - 1);
				} else {
					mgInfo->lastParticipantsStatus
						|= MegagroupInfo::LastParticipantsCountOutdated;
					mgInfo->lastParticipantsCount = 0;
				}
				if (mgInfo->lastAdmins.contains(user)) {
					mgInfo->lastAdmins.remove(user);
					if (megagroup->adminsCount() > 1) {
						megagroup->setAdminsCount(
							megagroup->adminsCount() - 1);
					}
					session().changes().peerUpdated(
						peer,
						Data::PeerUpdate::Flag::Admins);
				}
				mgInfo->bots.remove(user);
				if (mgInfo->bots.empty() && mgInfo->botStatus > 0) {
					mgInfo->botStatus = -1;
				}
			}
			Data::ChannelAdminChanges(megagroup).remove(uid);
		}
	}, [&](const MTPDmessageActionChatEditPhoto &data) {
		data.vphoto().match([&](const MTPDphoto &data) {
			using Flag = MTPDchatPhoto::Flag;
			const auto photo = owner().processPhoto(data);
			photo->peer = peer;
			const auto chatPhoto = MTP_chatPhoto(
				MTP_flags((photo->hasVideo() ? Flag::f_has_video : Flag(0))
					| (photo->inlineThumbnailBytes().isEmpty()
						? Flag(0)
						: Flag::f_stripped_thumb)),
				MTP_long(photo->id),
				MTP_bytes(photo->inlineThumbnailBytes()),
				data.vdc_id());
			if (const auto chat = peer->asChat()) {
				chat->setPhoto(chatPhoto);
			} else if (const auto channel = peer->asChannel()) {
				channel->setPhoto(chatPhoto);
			}
			peer->loadUserpic();
		}, [&](const MTPDphotoEmpty &data) {
			if (const auto chat = peer->asChat()) {
				chat->setPhoto(MTP_chatPhotoEmpty());
			} else if (const auto channel = peer->asChannel()) {
				channel->setPhoto(MTP_chatPhotoEmpty());
			}
		});
	}, [&](const MTPDmessageActionChatEditTitle &data) {
		if (const auto chat = peer->asChat()) {
			chat->setName(qs(data.vtitle()));
		}
	}, [&](const MTPDmessageActionChatMigrateTo &data) {
		if (const auto chat = peer->asChat()) {
			chat->addFlags(ChatDataFlag::Deactivated);
			if (const auto channel = owner().channelLoaded(
					data.vchannel_id().v)) {
				Data::ApplyMigration(chat, channel);
			}
		}
	}, [&](const MTPDmessageActionChannelMigrateFrom &data) {
		if (const auto channel = peer->asChannel()) {
			channel->addFlags(ChannelDataFlag::Megagroup);
			if (const auto chat = owner().chatLoaded(data.vchat_id().v)) {
				Data::ApplyMigration(chat, channel);
			}
		}
	}, [&](const MTPDmessageActionPinMessage &data) {
		if (replyTo) {
			replyTo->match([&](const MTPDmessageReplyHeader &data) {
				const auto id = data.vreply_to_msg_id().v;
				if (item) {
					session().storage().add(Storage::SharedMediaAddSlice(
						peer->id,
						MsgId(0),
						Storage::SharedMediaType::Pinned,
						{ id },
						{ id, ServerMaxMsgId }));
					setHasPinnedMessages(true);
					if (const auto topic = item->topic()) {
						session().storage().add(Storage::SharedMediaAddSlice(
							peer->id,
							topic->rootId(),
							Storage::SharedMediaType::Pinned,
							{ id },
							{ id, ServerMaxMsgId }));
						topic->setHasPinnedMessages(true);
					}
				}
			}, [&](const MTPDmessageReplyStoryHeader &data) {
				LOG(("API Error: story reply in messageActionPinMessage."));
			});
		}
	}, [&](const MTPDmessageActionGroupCall &data) {
		if (const auto channel = peer->asChannel()) {
			channel->setGroupCall(data.vcall());
		} else if (const auto chat = peer->asChat()) {
			chat->setGroupCall(data.vcall());
		}
	}, [&](const MTPDmessageActionGroupCallScheduled &data) {
		if (const auto channel = peer->asChannel()) {
			channel->setGroupCall(data.vcall(), data.vschedule_date().v);
		} else if (const auto chat = peer->asChat()) {
			chat->setGroupCall(data.vcall(), data.vschedule_date().v);
		}
	}, [&](const MTPDmessageActionPaymentSent &data) {
		if (const auto payment = item->Get<HistoryServicePayment>()) {
			auto paid = std::optional<Payments::PaidInvoice>();
			if (const auto message = payment->msg) {
				if (const auto media = message->media()) {
					if (const auto invoice = media->invoice()) {
						paid = Payments::CheckoutProcess::InvoicePaid(
							message);
					}
				}
			} else if (!payment->slug.isEmpty()) {
				using Payments::CheckoutProcess;
				paid = Payments::CheckoutProcess::InvoicePaid(
					&session(),
					payment->slug);
			}
			if (paid) {
				// Toast on a current active window.
				Ui::Toast::Show({
					.text = tr::lng_payments_success(
						tr::now,
						lt_amount,
						Ui::Text::Bold(payment->amount),
						lt_title,
						Ui::Text::Bold(paid->title),
						Ui::Text::WithEntities),
				});
			}
		}
	}, [&](const MTPDmessageActionSetChatTheme &data) {
		peer->setThemeEmoji(qs(data.vemoticon()));
	}, [&](const MTPDmessageActionChatJoinedByRequest &data) {
		processJoinedPeer(item->from());
	}, [&](const MTPDmessageActionTopicCreate &data) {
		if (const auto forum = peer->forum()) {
			forum->applyTopicAdded(
				item->id,
				qs(data.vtitle()),
				data.vicon_color().v,
				data.vicon_emoji_id().value_or(DocumentId()),
				item->from()->id,
				item->date(),
				item->out());
		}
	}, [&](const MTPDmessageActionTopicEdit &data) {
		if (const auto topic = item->topic()) {
			if (const auto &title = data.vtitle()) {
				topic->applyTitle(qs(*title));
			}
			if (const auto icon = data.vicon_emoji_id()) {
				topic->applyIconId(icon->v);
			}
			if (const auto closed = data.vclosed()) {
				topic->setClosed(mtpIsTrue(*closed));
			}
			if (const auto hidden = data.vhidden()) {
				topic->setHidden(mtpIsTrue(*hidden));
			}
		}
	}, [](const auto &) {
	});
}

void History::mainViewRemoved(
		not_null<HistoryBlock*> block,
		not_null<HistoryView::Element*> view) {
	Expects(_joinedMessage != view->data());

	if (_firstUnreadView == view) {
		getNextFirstUnreadMessage();
	}
	if (_unreadBarView == view) {
		_unreadBarView = nullptr;
	}
	if (scrollTopItem == view) {
		getNextScrollTopItem(block, view->indexInBlock());
	}
}

void History::newItemAdded(not_null<HistoryItem*> item) {
	item->indexAsNewItem();
	if (const auto from = item->from() ? item->from()->asUser() : nullptr) {
		if (from == item->author()) {
			_sendActionPainter.clear(from);
			owner().sendActionManager().repliesPaintersClear(this, from);
		}
		from->madeAction(item->date());
	}
	item->contributeToSlowmode();
	auto notification = Data::ItemNotification{
		.item = item,
		.type = Data::ItemNotificationType::Message,
	};
	if (item->showNotification()) {
		item->notificationThread()->pushNotification(notification);
	}
	owner().notifyNewItemAdded(item);
	const auto stillShow = item->showNotification(); // Could be read already.
	if (stillShow) {
		Core::App().notifications().schedule(notification);
	}
	if (item->out()) {
		if (item->isFromScheduled() && unreadCountRefreshNeeded(item->id)) {
			if (unreadCountKnown()) {
				setUnreadCount(unreadCount() + 1);
			} else if (!isForum()) {
				owner().histories().requestDialogEntry(this);
			}
		} else {
			destroyUnreadBar();
		}
		if (!item->unread(this)) {
			outboxRead(item);
		}
		if (item->changesWallPaper()) {
			peer->updateFullForced();
		}
	} else {
		if (item->unread(this)) {
			if (unreadCountKnown()) {
				setUnreadCount(unreadCount() + 1);
			} else if (!isForum()) {
				owner().histories().requestDialogEntry(this);
			}
		} else {
			inboxRead(item);
		}
	}
	item->incrementReplyToTopCounter();
	if (!folderKnown()) {
		owner().histories().requestDialogEntry(this);
	}
	if (const auto topic = item->topic()) {
		topic->applyItemAdded(item);
	}
}

void History::registerClientSideMessage(not_null<HistoryItem*> item) {
	Expects(item->isHistoryEntry());
	Expects(IsClientMsgId(item->id));

	_clientSideMessages.emplace(item);
	session().changes().historyUpdated(this, UpdateFlag::ClientSideMessages);
}

void History::unregisterClientSideMessage(not_null<HistoryItem*> item) {
	const auto removed = _clientSideMessages.remove(item);
	Assert(removed);

	session().changes().historyUpdated(this, UpdateFlag::ClientSideMessages);
}

const base::flat_set<not_null<HistoryItem*>> &History::clientSideMessages() {
	return _clientSideMessages;
}

HistoryItem *History::latestSendingMessage() const {
	auto sending = ranges::views::all(
		_clientSideMessages
	) | ranges::views::filter([](not_null<HistoryItem*> item) {
		return item->isSending();
	});
	const auto i = ranges::max_element(sending, ranges::less(), [](
			not_null<HistoryItem*> item) {
		return std::pair(item->date(), item->id.bare);
	});
	return (i == sending.end()) ? nullptr : i->get();
}

HistoryBlock *History::prepareBlockForAddingItem() {
	if (isBuildingFrontBlock()) {
		if (_buildingFrontBlock->block) {
			return _buildingFrontBlock->block;
		}

		blocks.push_front(std::make_unique<HistoryBlock>(this));
		for (auto i = 0, l = int(blocks.size()); i != l; ++i) {
			blocks[i]->setIndexInHistory(i);
		}
		_buildingFrontBlock->block = blocks.front().get();
		if (_buildingFrontBlock->expectedItemsCount > 0) {
			_buildingFrontBlock->block->messages.reserve(
				_buildingFrontBlock->expectedItemsCount + 1);
		}
		return _buildingFrontBlock->block;
	}

	const auto addNewBlock = blocks.empty()
		|| (blocks.back()->messages.size() >= kNewBlockEachMessage);
	if (addNewBlock) {
		blocks.push_back(std::make_unique<HistoryBlock>(this));
		blocks.back()->setIndexInHistory(blocks.size() - 1);
		blocks.back()->messages.reserve(kNewBlockEachMessage);
	}
	return blocks.back().get();
}

void History::viewReplaced(not_null<const Element*> was, Element *now) {
	if (scrollTopItem == was) scrollTopItem = now;
	if (_firstUnreadView == was) _firstUnreadView = now;
	if (_unreadBarView == was) _unreadBarView = now;
}

void History::addItemToBlock(not_null<HistoryItem*> item) {
	Expects(!item->mainView());

	auto block = prepareBlockForAddingItem();

	block->messages.push_back(item->createView(_delegateMixin->delegate()));
	const auto view = block->messages.back().get();
	view->attachToBlock(block, block->messages.size() - 1);

	if (isBuildingFrontBlock() && _buildingFrontBlock->expectedItemsCount > 0) {
		--_buildingFrontBlock->expectedItemsCount;
	}
}

void History::addEdgesToSharedMedia() {
	auto from = loadedAtTop() ? 0 : minMsgId();
	auto till = loadedAtBottom() ? ServerMaxMsgId : maxMsgId();
	for (auto i = 0; i != Storage::kSharedMediaTypeCount; ++i) {
		const auto type = static_cast<Storage::SharedMediaType>(i);
		session().storage().add(Storage::SharedMediaAddSlice(
			peer->id,
			MsgId(0), // topicRootId
			type,
			{},
			{ from, till }));
	}
}

void History::addOlderSlice(const QVector<MTPMessage> &slice) {
	if (slice.isEmpty()) {
		_loadedAtTop = true;
		checkLocalMessages();
		return;
	}

	if (const auto added = createItems(slice); !added.empty()) {
		addCreatedOlderSlice(added);
	} else {
		// If no items were added it means we've loaded everything old.
		_loadedAtTop = true;
		addEdgesToSharedMedia();
	}
	checkLocalMessages();
	checkLastMessage();
}

void History::addCreatedOlderSlice(
		const std::vector<not_null<HistoryItem*>> &items) {
	startBuildingFrontBlock(items.size());
	for (const auto &item : items) {
		addItemToBlock(item);
	}
	finishBuildingFrontBlock();

	if (loadedAtBottom()) {
		// Add photos to overview and authors to lastAuthors.
		addItemsToLists(items);
	}
	addToSharedMedia(items);
}

void History::addNewerSlice(const QVector<MTPMessage> &slice) {
	bool wasLoadedAtBottom = loadedAtBottom();

	if (slice.isEmpty()) {
		_loadedAtBottom = true;
		if (!lastMessage()) {
			setLastMessage(lastAvailableMessage());
		}
	}

	if (const auto added = createItems(slice); !added.empty()) {
		Assert(!isBuildingFrontBlock());

		for (const auto &item : added) {
			addItemToBlock(item);
		}

		addToSharedMedia(added);
	} else {
		_loadedAtBottom = true;
		setLastMessage(lastAvailableMessage());
		addEdgesToSharedMedia();
	}

	if (!wasLoadedAtBottom) {
		checkAddAllToUnreadMentions();
	}

	checkLocalMessages();
	checkLastMessage();
}

void History::checkLastMessage() {
	if (const auto last = lastMessage()) {
		if (!_loadedAtBottom && last->mainView()) {
			_loadedAtBottom = true;
			checkAddAllToUnreadMentions();
		}
	} else if (_loadedAtBottom) {
		setLastMessage(lastAvailableMessage());
	}
}

void History::addItemsToLists(
		const std::vector<not_null<HistoryItem*>> &items) {
	std::deque<not_null<UserData*>> *lastAuthors = nullptr;
	base::flat_set<not_null<PeerData*>> *markupSenders = nullptr;
	if (peer->isChat()) {
		lastAuthors = &peer->asChat()->lastAuthors;
		markupSenders = &peer->asChat()->markupSenders;
	} else if (peer->isMegagroup()) {
		// We don't add users to mgInfo->lastParticipants here.
		// We're scrolling back and we see messages from users that
		// could be gone from the megagroup already. It is fine for
		// chat->lastAuthors, because they're used only for field
		// autocomplete, but this is bad for megagroups, because its
		// lastParticipants are displayed in Profile as members list.
		markupSenders = &peer->asChannel()->mgInfo->markupSenders;
	}
	for (const auto &item : ranges::views::reverse(items)) {
		item->addToUnreadThings(HistoryUnreadThings::AddType::Existing);
		if (item->from()->id) {
			if (lastAuthors) { // chats
				if (auto user = item->from()->asUser()) {
					if (!base::contains(*lastAuthors, user)) {
						lastAuthors->push_back(user);
					}
				}
			}
		}
		if (item->author()->id) {
			if (markupSenders) { // chats with bots
				if (!lastKeyboardInited && item->definesReplyKeyboard() && !item->out()) {
					const auto markupFlags = item->replyKeyboardFlags();
					if (!(markupFlags & ReplyMarkupFlag::Selective) || item->mentionsMe()) {
						bool wasKeyboardHide = markupSenders->contains(item->author());
						if (!wasKeyboardHide) {
							markupSenders->insert(item->author());
						}
						if (!(markupFlags & ReplyMarkupFlag::None)) {
							if (!lastKeyboardInited) {
								bool botNotInChat = false;
								if (peer->isChat()) {
									botNotInChat = (!Data::CanSendAnything(peer)
										|| !peer->asChat()->participants.empty())
										&& item->author()->isUser()
										&& !peer->asChat()->participants.contains(item->author()->asUser());
								} else if (peer->isMegagroup()) {
									botNotInChat = (!Data::CanSendAnything(peer)
										|| peer->asChannel()->mgInfo->botStatus != 0)
										&& item->author()->isUser()
										&& !peer->asChannel()->mgInfo->bots.contains(item->author()->asUser());
								}
								if (wasKeyboardHide || botNotInChat) {
									clearLastKeyboard();
								} else {
									lastKeyboardInited = true;
									lastKeyboardId = item->id;
									lastKeyboardFrom = item->author()->id;
									lastKeyboardUsed = false;
								}
							}
						}
					}
				}
			} else if (!lastKeyboardInited && item->definesReplyKeyboard() && !item->out()) { // conversations with bots
				const auto markupFlags = item->replyKeyboardFlags();
				if (!(markupFlags & ReplyMarkupFlag::Selective) || item->mentionsMe()) {
					if (markupFlags & ReplyMarkupFlag::None) {
						clearLastKeyboard();
					} else {
						lastKeyboardInited = true;
						lastKeyboardId = item->id;
						lastKeyboardFrom = item->author()->id;
						lastKeyboardUsed = false;
					}
				}
			}
		}
	}

}

void History::checkAddAllToUnreadMentions() {
	if (!loadedAtBottom()) {
		return;
	}

	for (const auto &block : blocks) {
		for (const auto &message : block->messages) {
			const auto item = message->data();
			item->addToUnreadThings(HistoryUnreadThings::AddType::Existing);
		}
	}
}

void History::addToSharedMedia(
		const std::vector<not_null<HistoryItem*>> &items) {
	std::vector<MsgId> medias[Storage::kSharedMediaTypeCount];
	auto topicsWithPinned = base::flat_set<not_null<Data::ForumTopic*>>();
	for (const auto &item : items) {
		if (const auto types = item->sharedMediaTypes()) {
			for (auto i = 0; i != Storage::kSharedMediaTypeCount; ++i) {
				const auto type = static_cast<Storage::SharedMediaType>(i);
				if (types.test(type)) {
					if (medias[i].empty()) {
						medias[i].reserve(items.size());
					}
					medias[i].push_back(item->id);
					if (type == Storage::SharedMediaType::Pinned) {
						if (const auto topic = item->topic()) {
							if (!topic->hasPinnedMessages()) {
								topicsWithPinned.emplace(topic);
							}
						}
					}
				}
			}
		}
	}
	const auto from = loadedAtTop() ? 0 : minMsgId();
	const auto till = loadedAtBottom() ? ServerMaxMsgId : maxMsgId();
	for (auto i = 0; i != Storage::kSharedMediaTypeCount; ++i) {
		if (!medias[i].empty()) {
			const auto type = static_cast<Storage::SharedMediaType>(i);
			session().storage().add(Storage::SharedMediaAddSlice(
				peer->id,
				MsgId(0), // topicRootId
				type,
				std::move(medias[i]),
				{ from, till }));
			if (type == Storage::SharedMediaType::Pinned) {
				setHasPinnedMessages(true);
			}
		}
	}
	for (const auto &topic : topicsWithPinned) {
		topic->setHasPinnedMessages(true);
	}
}

void History::calculateFirstUnreadMessage() {
	if (!_inboxReadBefore) {
		return;
	}

	_firstUnreadView = nullptr;
	if (!unreadCount() || !trackUnreadMessages()) {
		return;
	}
	for (const auto &block : ranges::views::reverse(blocks)) {
		for (const auto &message : ranges::views::reverse(block->messages)) {
			const auto item = message->data();
			if (!item->isRegular()) {
				continue;
			} else if (!item->out()) {
				if (item->id >= *_inboxReadBefore) {
					_firstUnreadView = message.get();
				} else {
					return;
				}
			}
		}
	}
}

bool History::readInboxTillNeedsRequest(MsgId tillId) {
	Expects(!tillId || IsServerMsgId(tillId));

	readClientSideMessages();
	if (unreadMark()) {
		owner().histories().changeDialogUnreadMark(this, false);
	}
	DEBUG_LOG(("Reading: readInboxTillNeedsRequest is_server %1, before %2."
		).arg(Logs::b(IsServerMsgId(tillId))
		).arg(_inboxReadBefore.value_or(-666).bare));
	return IsServerMsgId(tillId) && (_inboxReadBefore.value_or(1) <= tillId);
}

void History::readClientSideMessages() {
	auto &histories = owner().histories();
	for (const auto &item : _clientSideMessages) {
		histories.readClientSideMessage(item);
	}
}

bool History::unreadCountRefreshNeeded(MsgId readTillId) const {
	return !unreadCountKnown()
		|| ((readTillId + 1) > _inboxReadBefore.value_or(0));
}

std::optional<int> History::countStillUnreadLocal(MsgId readTillId) const {
	if (isEmpty() || !folderKnown()) {
		DEBUG_LOG(("Reading: countStillUnreadLocal unknown %1 and %2.").arg(
			Logs::b(isEmpty()),
			Logs::b(folderKnown())));
		return std::nullopt;
	}
	if (_inboxReadBefore) {
		const auto before = *_inboxReadBefore;
		DEBUG_LOG(("Reading: check before %1 with min %2 and max %3."
			).arg(before.bare
			).arg(minMsgId().bare
			).arg(maxMsgId().bare));
		if (minMsgId() <= before && maxMsgId() >= readTillId) {
			auto result = 0;
			for (const auto &block : blocks) {
				for (const auto &message : block->messages) {
					const auto item = message->data();
					if (!item->isRegular()
						|| (item->out() && !item->isFromScheduled())) {
						continue;
					} else if (item->id > readTillId) {
						break;
					} else if (item->id >= before) {
						++result;
					}
				}
			}
			DEBUG_LOG(("Reading: check before result %1 with existing %2"
				).arg(result
				).arg(_unreadCount.value_or(-666)));
			if (_unreadCount) {
				return std::max(*_unreadCount - result, 0);
			}
		}
	}
	const auto minimalServerId = minMsgId();
	DEBUG_LOG(("Reading: check at end loaded from %1 loaded %2 - %3").arg(
		QString::number(minimalServerId.bare),
		Logs::b(loadedAtBottom()),
		Logs::b(loadedAtTop())));
	if (!loadedAtBottom()
		|| (!loadedAtTop() && !minimalServerId)
		|| minimalServerId > readTillId) {
		return std::nullopt;
	}
	auto result = 0;
	for (const auto &block : ranges::views::reverse(blocks)) {
		for (const auto &message : ranges::views::reverse(block->messages)) {
			const auto item = message->data();
			if (item->isRegular()) {
				if (item->id <= readTillId) {
					return result;
				} else if (!item->out()) {
					++result;
				}
			}
		}
	}
	DEBUG_LOG(("Reading: check at end counted %1").arg(result));
	return result;
}

void History::applyInboxReadUpdate(
		FolderId folderId,
		MsgId upTo,
		int stillUnread,
		int32 channelPts) {
	const auto folder = folderId ? owner().folderLoaded(folderId) : nullptr;
	if (folder && this->folder() != folder) {
		// If history folder is unknown or not synced, request both.
		owner().histories().requestDialogEntry(this);
		owner().histories().requestDialogEntry(folder);
	}
	if (_inboxReadBefore.value_or(1) <= upTo) {
		if (!peer->isChannel() || peer->asChannel()->pts() == channelPts) {
			inboxRead(upTo, stillUnread);
		} else {
			inboxRead(upTo);
		}
	}
}

void History::inboxRead(MsgId upTo, std::optional<int> stillUnread) {
	if (stillUnread.has_value() && folderKnown()) {
		setUnreadCount(*stillUnread);
	} else if (const auto still = countStillUnreadLocal(upTo)) {
		setUnreadCount(*still);
	} else {
		owner().histories().requestDialogEntry(this);
	}
	setInboxReadTill(upTo);
	updateChatListEntry();
	if (const auto to = peer->migrateTo()) {
		if (const auto migrated = peer->owner().historyLoaded(to->id)) {
			migrated->updateChatListEntry();
		}
	}

	_firstUnreadView = nullptr;
	Core::App().notifications().clearIncomingFromHistory(this);
}

void History::inboxRead(not_null<const HistoryItem*> wasRead) {
	if (wasRead->isRegular()) {
		inboxRead(wasRead->id);
	}
}

void History::outboxRead(MsgId upTo) {
	setOutboxReadTill(upTo);
	if (const auto last = chatListMessage()) {
		if (last->out() && last->isRegular() && last->id <= upTo) {
			session().changes().messageUpdated(
				last,
				Data::MessageUpdate::Flag::DialogRowRepaint);
		}
	}
	updateChatListEntry();
	session().changes().historyUpdated(this, UpdateFlag::OutboxRead);
}

void History::outboxRead(not_null<const HistoryItem*> wasRead) {
	if (wasRead->isRegular()) {
		outboxRead(wasRead->id);
	}
}

MsgId History::loadAroundId() const {
	if (_unreadCount && *_unreadCount > 0 && _inboxReadBefore) {
		return *_inboxReadBefore;
	}
	return MsgId(0);
}

MsgId History::inboxReadTillId() const {
	return _inboxReadBefore.value_or(1) - 1;
}

MsgId History::outboxReadTillId() const {
	return _outboxReadBefore.value_or(1) - 1;
}

HistoryItem *History::lastAvailableMessage() const {
	return isEmpty() ? nullptr : blocks.back()->messages.back()->data().get();
}

int History::unreadCount() const {
	return _unreadCount ? *_unreadCount : 0;
}

bool History::unreadCountKnown() const {
	return _unreadCount.has_value();
}

void History::setUnreadCount(int newUnreadCount) {
	Expects(folderKnown());

	if (_unreadCount == newUnreadCount) {
		return;
	}
	const auto notifier = unreadStateChangeNotifier(!isForum());
	_unreadCount = newUnreadCount;

	const auto lastOutgoing = [&] {
		const auto last = lastMessage();
		return last
			&& last->isRegular()
			&& loadedAtBottom()
			&& !isEmpty()
			&& blocks.back()->messages.back()->data() == last
			&& last->out();
	}();
	if (newUnreadCount == 1 && !lastOutgoing) {
		if (loadedAtBottom()) {
			_firstUnreadView = !isEmpty()
				? blocks.back()->messages.back().get()
				: nullptr;
		}
		if (const auto last = msgIdForRead()) {
			setInboxReadTill(last - 1);
		}
	} else if (!newUnreadCount) {
		_firstUnreadView = nullptr;
		if (const auto last = msgIdForRead()) {
			setInboxReadTill(last);
		}
	} else if (!_firstUnreadView && !_unreadBarView && loadedAtBottom()) {
		calculateFirstUnreadMessage();
	}
}

void History::setUnreadMark(bool unread) {
	if (clearUnreadOnClientSide()) {
		unread = false;
	}
	if (unreadMark() == unread) {
		return;
	}
	const auto notifier = unreadStateChangeNotifier(
		!unreadCount() && !isForum());
	Thread::setUnreadMarkFlag(unread);
}

void History::setFakeUnreadWhileOpened(bool enabled) {
	if (fakeUnreadWhileOpened() == enabled) {
		return;
	} else if (enabled) {
		if (!inChatList()) {
			return;
		}
		const auto state = chatListBadgesState();
		if (!state.unread && !state.mention) {
			return;
		}
	}
	if (enabled) {
		_flags |= Flag::FakeUnreadWhileOpened;
	} else {
		_flags &= ~Flag::FakeUnreadWhileOpened;
	}
	owner().chatsFilters().refreshHistory(this);
}

[[nodiscard]] bool History::fakeUnreadWhileOpened() const {
	return (_flags & Flag::FakeUnreadWhileOpened);
}

void History::setMuted(bool muted) {
	if (this->muted() == muted) {
		return;
	} else {
		const auto state = isForum()
			? Dialogs::BadgesState()
			: computeBadgesState();
		const auto notify = (state.unread || state.reaction);
		const auto notifier = unreadStateChangeNotifier(notify);
		Thread::setMuted(muted);
	}
	session().changes().peerUpdated(
		peer,
		Data::PeerUpdate::Flag::Notifications);
	owner().chatsFilters().refreshHistory(this);
	if (const auto forum = peer->forum()) {
		owner().notifySettings().forumParentMuteUpdated(forum);
	}
}

void History::getNextFirstUnreadMessage() {
	Expects(_firstUnreadView != nullptr);

	const auto block = _firstUnreadView->block();
	const auto index = _firstUnreadView->indexInBlock();
	const auto setFromMessage = [&](const auto &view) {
		if (view->data()->isRegular()) {
			_firstUnreadView = view.get();
			return true;
		}
		return false;
	};
	if (index >= 0) {
		const auto count = int(block->messages.size());
		for (auto i = index + 1; i != count; ++i) {
			const auto &message = block->messages[i];
			if (setFromMessage(message)) {
				return;
			}
		}
	}

	const auto count = int(blocks.size());
	for (auto j = block->indexInHistory() + 1; j != count; ++j) {
		for (const auto &message : blocks[j]->messages) {
			if (setFromMessage(message)) {
				return;
			}
		}
	}
	_firstUnreadView = nullptr;
}

MsgId History::nextNonHistoryEntryId() {
	return owner().nextNonHistoryEntryId();
}

bool History::folderKnown() const {
	return _folder.has_value();
}

Data::Folder *History::folder() const {
	return _folder.value_or(nullptr);
}

void History::setFolder(
		not_null<Data::Folder*> folder,
		HistoryItem *folderDialogItem) {
	setFolderPointer(folder);
	if (folderDialogItem) {
		setLastServerMessage(folderDialogItem);
	}
}

void History::clearFolder() {
	setFolderPointer(nullptr);
}

void History::setFolderPointer(Data::Folder *folder) {
	if (_folder == folder) {
		return;
	}
	if (isPinnedDialog(FilterId())) {
		owner().setChatPinned(this, FilterId(), false);
	}
	const auto wasKnown = folderKnown();
	const auto wasInList = inChatList();
	if (wasInList) {
		removeFromChatList(0, owner().chatsList(this->folder()));
	}
	const auto was = _folder.value_or(nullptr);
	_folder = folder;
	if (was) {
		was->unregisterOne(this);
	}
	if (wasInList) {
		addToChatList(0, owner().chatsList(folder));

		owner().chatsFilters().refreshHistory(this);
		updateChatListEntry();

		owner().chatsListChanged(was);
		owner().chatsListChanged(folder);
	} else if (!wasKnown) {
		updateChatListSortPosition();
	}
	if (folder) {
		folder->registerOne(this);
	}
	session().changes().historyUpdated(this, UpdateFlag::Folder);
}

int History::chatListNameVersion() const {
	return peer->nameVersion();
}

void History::hasUnreadMentionChanged(bool has) {
	if (isForum()) {
		return;
	}
	auto was = chatListUnreadState();
	if (has) {
		was.mentions = 0;
	} else {
		was.mentions = 1;
	}
	notifyUnreadStateChange(was);
}

void History::hasUnreadReactionChanged(bool has) {
	if (isForum()) {
		return;
	}
	auto was = chatListUnreadState();
	if (has) {
		was.reactions = was.reactionsMuted = 0;
	} else {
		was.reactions = 1;
		was.reactionsMuted = muted() ? was.reactions : 0;
	}
	notifyUnreadStateChange(was);
}

void History::applyPinnedUpdate(const MTPDupdateDialogPinned &data) {
	const auto folderId = data.vfolder_id().value_or_empty();
	if (!folderKnown()) {
		if (folderId) {
			setFolder(owner().folder(folderId));
		} else {
			clearFolder();
		}
	}
	owner().setChatPinned(this, FilterId(), data.is_pinned());
}

TimeId History::adjustedChatListTimeId() const {
	const auto result = chatListTimeId();
	if (const auto draft = cloudDraft(MsgId(0))) {
		if (!peer->forum()
			&& !Data::DraftIsNull(draft)
			&& !session().supportMode()) {
			return std::max(result, draft->date);
		}
	}
	return result;
}

void History::countScrollState(int top) {
	std::tie(scrollTopItem, scrollTopOffset) = findItemAndOffset(top);
}

auto History::findItemAndOffset(int top) const -> std::pair<Element*, int> {
	if (const auto element = findScrollTopItem(top)) {
		return { element, (top - element->block()->y() - element->y()) };
	}
	return {};
}

auto History::findScrollTopItem(int top) const -> Element* {
	if (isEmpty()) {
		return nullptr;
	}

	auto itemIndex = 0;
	auto blockIndex = 0;
	auto itemTop = 0;
	if (scrollTopItem) {
		itemIndex = scrollTopItem->indexInBlock();
		blockIndex = scrollTopItem->block()->indexInHistory();
		itemTop = blocks[blockIndex]->y() + scrollTopItem->y();
	}
	if (itemTop > top) {
		// go backward through history while we don't find an item that starts above
		do {
			const auto &block = blocks[blockIndex];
			for (--itemIndex; itemIndex >= 0; --itemIndex) {
				const auto view = block->messages[itemIndex].get();
				itemTop = block->y() + view->y();
				if (itemTop <= top) {
					return view;
				}
			}
			if (--blockIndex >= 0) {
				itemIndex = blocks[blockIndex]->messages.size();
			} else {
				break;
			}
		} while (true);

		return blocks.front()->messages.front().get();
	}
	// go forward through history while we don't find the last item that starts above
	for (auto blocksCount = int(blocks.size()); blockIndex < blocksCount; ++blockIndex) {
		const auto &block = blocks[blockIndex];
		for (auto itemsCount = int(block->messages.size()); itemIndex < itemsCount; ++itemIndex) {
			itemTop = block->y() + block->messages[itemIndex]->y();
			if (itemTop > top) {
				Assert(itemIndex > 0 || blockIndex > 0);
				if (itemIndex > 0) {
					return block->messages[itemIndex - 1].get();
				}
				return blocks[blockIndex - 1]->messages.back().get();
			}
		}
		itemIndex = 0;
	}
	return blocks.back()->messages.back().get();
}

void History::getNextScrollTopItem(HistoryBlock *block, int32 i) {
	++i;
	if (i > 0 && i < block->messages.size()) {
		scrollTopItem = block->messages[i].get();
		return;
	}
	int j = block->indexInHistory() + 1;
	if (j > 0 && j < blocks.size()) {
		scrollTopItem = blocks[j]->messages.front().get();
		return;
	}
	scrollTopItem = nullptr;
}

void History::addUnreadBar() {
	if (!_unreadBarView && _firstUnreadView && unreadCount()) {
		_unreadBarView = _firstUnreadView;
		_unreadBarView->createUnreadBar(tr::lng_unread_bar_some());
	}
}

void History::destroyUnreadBar() {
	if (const auto view = base::take(_unreadBarView)) {
		view->destroyUnreadBar();
	}
}

void History::unsetFirstUnreadMessage() {
	_firstUnreadView = nullptr;
}

HistoryView::Element *History::unreadBar() const {
	return _unreadBarView;
}

HistoryView::Element *History::firstUnreadMessage() const {
	return _firstUnreadView;
}

not_null<HistoryItem*> History::addNewInTheMiddle(
		not_null<HistoryItem*> item,
		int blockIndex,
		int itemIndex) {
	Expects(blockIndex >= 0);
	Expects(blockIndex < blocks.size());
	Expects(itemIndex >= 0);
	Expects(itemIndex <= blocks[blockIndex]->messages.size());

	const auto &block = blocks[blockIndex];

	const auto it = block->messages.insert(
		block->messages.begin() + itemIndex,
		item->createView(_delegateMixin->delegate()));
	(*it)->attachToBlock(block.get(), itemIndex);
	if (itemIndex + 1 < block->messages.size()) {
		for (auto i = itemIndex + 1, l = int(block->messages.size()); i != l; ++i) {
			block->messages[i]->setIndexInBlock(i);
		}
		block->messages[itemIndex + 1]->previousInBlocksChanged();
	} else if (blockIndex + 1 < blocks.size() && !blocks[blockIndex + 1]->messages.empty()) {
		blocks[blockIndex + 1]->messages.front()->previousInBlocksChanged();
	} else {
		(*it)->nextInBlocksRemoved();
	}

	return item;
}

History *History::migrateSibling() const {
	const auto addFromId = [&] {
		if (const auto from = peer->migrateFrom()) {
			return from->id;
		} else if (const auto to = peer->migrateTo()) {
			return to->id;
		}
		return PeerId(0);
	}();
	return owner().historyLoaded(addFromId);
}

Dialogs::UnreadState History::chatListUnreadState() const {
	if (const auto forum = peer->forum()) {
		return forum->topicsList()->unreadState();
	}
	return computeUnreadState();
}

Dialogs::BadgesState History::chatListBadgesState() const {
	if (const auto forum = peer->forum()) {
		return adjustBadgesStateByFolder(
			Dialogs::BadgesForUnread(
				forum->topicsList()->unreadState(),
				Dialogs::CountInBadge::Chats,
				Dialogs::IncludeInBadge::UnmutedOrAll));
	}
	return computeBadgesState();
}

Dialogs::BadgesState History::computeBadgesState() const {
	return adjustBadgesStateByFolder(
		Dialogs::BadgesForUnread(
			computeUnreadState(),
			Dialogs::CountInBadge::Messages,
			Dialogs::IncludeInBadge::All));
}

Dialogs::BadgesState History::adjustBadgesStateByFolder(
		Dialogs::BadgesState state) const {
	if (folder()) {
		state.mentionMuted = state.reactionMuted = state.unreadMuted = true;
	}
	return state;
}

Dialogs::UnreadState History::computeUnreadState() const {
	auto result = Dialogs::UnreadState();
	const auto count = _unreadCount.value_or(0);
	const auto mark = !count && unreadMark();
	const auto muted = this->muted();
	result.messages = count;
	result.chats = count ? 1 : 0;
	result.marks = mark ? 1 : 0;
	result.mentions = unreadMentions().has() ? 1 : 0;
	result.reactions = unreadReactions().has() ? 1 : 0;
	result.messagesMuted = muted ? result.messages : 0;
	result.chatsMuted = muted ? result.chats : 0;
	result.marksMuted = muted ? result.marks : 0;
	result.reactionsMuted = muted ? result.reactions : 0;
	result.known = _unreadCount.has_value();
	return result;
}

void History::allowChatListMessageResolve() {
	if (_flags & Flag::ResolveChatListMessage) {
		return;
	}
	_flags |= Flag::ResolveChatListMessage;
	if (!chatListMessageKnown()) {
		requestChatListMessage();
	} else {
		resolveChatListMessageGroup();
	}
}

void History::resolveChatListMessageGroup() {
	const auto item = _chatListMessage.value_or(nullptr);
	if (!(_flags & Flag::ResolveChatListMessage)
		|| !item
		|| !hasOrphanMediaGroupPart()) {
		return;
	}
	// If we set a single album part, request the full album.
	const auto withImages = !item->toPreview({
		.hideSender = true,
		.hideCaption = true }).images.empty();
	if (withImages) {
		owner().histories().requestGroupAround(item);
	}
	if (unreadCountKnown() && !unreadCount()) {
		// When we add just one last item, like we do while loading dialogs,
		// we want to remove a single added grouped media, otherwise it will
		// jump once we open the message history (first we show only that
		// media, then we load the rest of the group and show the group).
		//
		// That way when we open the message history we show nothing until a
		// whole history part is loaded, it certainly will contain the group.
		clear(ClearType::Unload);
	}
}

HistoryItem *History::chatListMessage() const {
	return _chatListMessage.value_or(nullptr);
}

bool History::chatListMessageKnown() const {
	return _chatListMessage.has_value();
}

const QString &History::chatListName() const {
	return peer->name();
}

const QString &History::chatListNameSortKey() const {
	return _chatListNameSortKey;
}

void History::refreshChatListNameSortKey() {
	_chatListNameSortKey = owner().nameSortKey(peer->name());
}

const base::flat_set<QString> &History::chatListNameWords() const {
	return peer->nameWords();
}

const base::flat_set<QChar> &History::chatListFirstLetters() const {
	return peer->nameFirstLetters();
}

void History::chatListPreloadData() {
	peer->loadUserpic();
	allowChatListMessageResolve();
}

void History::paintUserpic(
		Painter &p,
		Ui::PeerUserpicView &view,
		const Dialogs::Ui::PaintContext &context) const {
	peer->paintUserpic(
		p,
		view,
		context.st->padding.left(),
		context.st->padding.top(),
		context.st->photoSize);
}

void History::startBuildingFrontBlock(int expectedItemsCount) {
	Assert(!isBuildingFrontBlock());
	Assert(expectedItemsCount > 0);

	_buildingFrontBlock = std::make_unique<BuildingBlock>();
	_buildingFrontBlock->expectedItemsCount = expectedItemsCount;
}

void History::finishBuildingFrontBlock() {
	Expects(isBuildingFrontBlock());

	// Some checks if there was some message history already
	if (const auto block = base::take(_buildingFrontBlock)->block) {
		if (blocks.size() > 1) {
			// ... item, item, item, last ], [ first, item, item ...
			const auto first = blocks[1]->messages.front().get();

			// we've added a new front block, so previous item for
			// the old first item of a first block was changed
			first->previousInBlocksChanged();
		} else {
			block->messages.back()->nextInBlocksRemoved();
		}
	}
}

bool History::loadedAtBottom() const {
	return _loadedAtBottom;
}

bool History::loadedAtTop() const {
	return _loadedAtTop;
}

bool History::isReadyFor(MsgId msgId) {
	if (msgId < 0 && -msgId < ServerMaxMsgId && peer->migrateFrom()) {
		// Old group history.
		return owner().history(peer->migrateFrom()->id)->isReadyFor(-msgId);
	}

	if (msgId == ShowAtTheEndMsgId) {
		return loadedAtBottom();
	}
	if (msgId == ShowAtUnreadMsgId) {
		if (const auto migratePeer = peer->migrateFrom()) {
			if (const auto migrated = owner().historyLoaded(migratePeer)) {
				if (migrated->unreadCount()) {
					return migrated->isReadyFor(msgId);
				}
			}
		}
		if (unreadCount() && _inboxReadBefore) {
			if (!isEmpty()) {
				return (loadedAtTop() || minMsgId() <= *_inboxReadBefore)
					&& (loadedAtBottom() || maxMsgId() >= *_inboxReadBefore);
			}
			return false;
		}
		return loadedAtBottom();
	}
	const auto item = owner().message(peer, msgId);
	return item && (item->history() == this) && item->mainView();
}

void History::getReadyFor(MsgId msgId) {
	if (msgId < 0 && -msgId < ServerMaxMsgId && peer->migrateFrom()) {
		const auto migrated = owner().history(peer->migrateFrom()->id);
		migrated->getReadyFor(-msgId);
		if (migrated->isEmpty()) {
			clear(ClearType::Unload);
		}
		return;
	}
	if (msgId == ShowAtUnreadMsgId) {
		if (const auto migratePeer = peer->migrateFrom()) {
			if (const auto migrated = owner().historyLoaded(migratePeer)) {
				if (migrated->unreadCount()) {
					clear(ClearType::Unload);
					migrated->getReadyFor(msgId);
					return;
				}
			}
		}
	}
	if (!isReadyFor(msgId)) {
		clear(ClearType::Unload);
		if (const auto migratePeer = peer->migrateFrom()) {
			if (const auto migrated = owner().historyLoaded(migratePeer)) {
				migrated->clear(ClearType::Unload);
			}
		}
		if ((msgId == ShowAtTheEndMsgId)
			|| (msgId == ShowAtUnreadMsgId && !unreadCount())) {
			_loadedAtBottom = true;
		}
	}
}

void History::setNotLoadedAtBottom() {
	_loadedAtBottom = false;

	session().storage().invalidate(
		Storage::SharedMediaInvalidateBottom(peer->id));
}

void History::clearSharedMedia() {
	session().storage().remove(
		Storage::SharedMediaRemoveAll(peer->id));
}

void History::setLastServerMessage(HistoryItem *item) {
	_lastServerMessage = item;
	if (_lastMessage
		&& *_lastMessage
		&& !(*_lastMessage)->isRegular()
		&& (!item || (*_lastMessage)->date() > item->date())) {
		return;
	}
	setLastMessage(item);
}

void History::setLastMessage(HistoryItem *item) {
	if (_lastMessage && *_lastMessage == item) {
		return;
	}
	_lastMessage = item;
	if (!item || item->isRegular()) {
		_lastServerMessage = item;
	}
	if (peer->migrateTo()) {
		// We don't want to request last message for all deactivated chats.
		// This is a heavy request for them, because we need to get last
		// two items by messages.getHistory to skip the migration message.
		setChatListMessageUnknown();
	} else {
		setChatListMessageFromLast();
		if (!chatListMessageKnown()) {
			setFakeChatListMessage();
		}
	}
}

void History::refreshChatListMessage() {
	const auto known = chatListMessageKnown();
	setChatListMessageFromLast();
	if (known && !_chatListMessage) {
		requestChatListMessage();
	}
}

void History::setChatListMessage(HistoryItem *item) {
	if (_chatListMessage && *_chatListMessage == item) {
		return;
	}
	const auto was = _chatListMessage.value_or(nullptr);
	if (item) {
		if (item->isSponsored()) {
			return;
		}
		if (_chatListMessage
			&& *_chatListMessage
			&& !(*_chatListMessage)->isRegular()
			&& (*_chatListMessage)->date() > item->date()) {
			return;
		}
		_chatListMessage = item;
		setChatListTimeId(item->date());
		resolveChatListMessageGroup();
	} else if (!_chatListMessage || *_chatListMessage) {
		_chatListMessage = nullptr;
		updateChatListEntry();
	}
	if (const auto folder = this->folder()) {
		folder->oneListMessageChanged(was, item);
	}
	if (const auto to = peer->migrateTo()) {
		if (const auto history = owner().historyLoaded(to)) {
			if (!history->chatListMessageKnown()) {
				history->requestChatListMessage();
			}
		}
	}
}

auto History::computeChatListMessageFromLast() const
-> std::optional<HistoryItem*> {
	if (!_lastMessage) {
		return _lastMessage;
	}

	// In migrated groups we want to skip essential message
	// about migration in the chats list and display the last
	// non-migration message from the original legacy group.
	const auto last = lastMessage();
	if (!last || !last->isGroupMigrate()) {
		return _lastMessage;
	}
	if (const auto chat = peer->asChat()) {
		// In chats we try to take the item before the 'last', which
		// is the empty-displayed migration message.
		if (!loadedAtBottom()) {
			// We don't know the tail of the history.
			return std::nullopt;
		}
		const auto before = [&]() -> HistoryItem* {
			for (const auto &block : ranges::views::reverse(blocks)) {
				const auto &messages = block->messages;
				for (const auto &item : ranges::views::reverse(messages)) {
					if (item->data() != last) {
						return item->data();
					}
				}
			}
			return nullptr;
		}();
		if (before) {
			// We found a message that is not the migration one.
			return before;
		} else if (loadedAtTop()) {
			// No other messages in this history.
			return _lastMessage;
		}
		return std::nullopt;
	} else if (const auto from = migrateFrom()) {
		// In megagroups we just try to use
		// the message from the original group.
		return from->chatListMessageKnown()
			? std::make_optional(from->chatListMessage())
			: std::nullopt;
	}
	return _lastMessage;
}

void History::setChatListMessageFromLast() {
	if (const auto good = computeChatListMessageFromLast()) {
		setChatListMessage(*good);
	} else {
		setChatListMessageUnknown();
	}
}

void History::setChatListMessageUnknown() {
	if (!_chatListMessage.has_value()) {
		return;
	}
	const auto was = *_chatListMessage;
	_chatListMessage = std::nullopt;
	if (const auto folder = this->folder()) {
		folder->oneListMessageChanged(was, nullptr);
	}
}

void History::requestChatListMessage() {
	if (!lastMessageKnown()) {
		owner().histories().requestDialogEntry(this, [=] {
			requestChatListMessage();
		});
		return;
	} else if (chatListMessageKnown()) {
		return;
	}
	setChatListMessageFromLast();
	if (!chatListMessageKnown()) {
		setFakeChatListMessage();
	}
}

void History::setFakeChatListMessage() {
	if (!(_flags & Flag::ResolveChatListMessage)) {
		if (!chatListTimeId()) {
			if (const auto last = lastMessage()) {
				setChatListTimeId(last->date());
			}
		}
		return;
	} else if (const auto chat = peer->asChat()) {
		// In chats we try to take the item before the 'last', which
		// is the empty-displayed migration message.
		owner().histories().requestFakeChatListMessage(this);
	} else if (const auto from = migrateFrom()) {
		// In megagroups we just try to use
		// the message from the original group.
		from->allowChatListMessageResolve();
		from->requestChatListMessage();
	}
}

void History::setFakeChatListMessageFrom(const MTPmessages_Messages &data) {
	if (!lastMessageKnown()) {
		requestChatListMessage();
		return;
	}
	const auto finalize = gsl::finally([&] {
		// Make sure that we have chatListMessage when we get out of here.
		if (!chatListMessageKnown()) {
			setChatListMessage(lastMessage());
		}
	});
	const auto last = lastMessage();
	if (!last || !last->isGroupMigrate()) {
		// Last message is good enough.
		return;
	}
	const auto other = data.match([&](
			const MTPDmessages_messagesNotModified &) {
		return static_cast<const MTPMessage*>(nullptr);
	}, [&](const auto &data) {
		for (const auto &message : data.vmessages().v) {
			const auto id = message.match([](const auto &data) {
				return data.vid().v;
			});
			if (id != last->id) {
				return &message;
			}
		}
		return static_cast<const MTPMessage*>(nullptr);
	});
	if (!other) {
		// Other (non equal to the last one) message not found.
		return;
	}
	const auto item = owner().addNewMessage(
		*other,
		MessageFlags(),
		NewMessageType::Existing);
	if (!item || item->isGroupMigrate()) {
		// Not better than the last one.
		return;
	}
	setChatListMessage(item);
}

void History::applyChatListGroup(
		PeerId dataPeerId,
		const MTPmessages_Messages &data) {
	if (!isEmpty()
		|| !_chatListMessage
		|| !*_chatListMessage
		|| (*_chatListMessage)->history() != this
		|| !_lastMessage
		|| !*_lastMessage
		|| dataPeerId != peer->id) {
		return;
	}
	// Apply loaded album as a last slice.
	const auto processMessages = [&](const MTPVector<MTPMessage> &messages) {
		auto items = std::vector<not_null<HistoryItem*>>();
		items.reserve(messages.v.size());
		for (const auto &message : messages.v) {
			const auto id = IdFromMessage(message);
			if (const auto message = owner().message(dataPeerId, id)) {
				items.push_back(message);
			}
		}
		if (!ranges::contains(items, not_null(*_lastMessage))
			|| !ranges::contains(items, not_null(*_chatListMessage))) {
			return;
		}
		_loadedAtBottom = true;
		ranges::sort(items, ranges::less{}, &HistoryItem::id);
		addCreatedOlderSlice(items);
		checkLocalMessages();
		checkLastMessage();
	};
	data.match([&](const MTPDmessages_messagesNotModified &) {
	}, [&](const auto &data) {
		processMessages(data.vmessages());
	});
}

HistoryItem *History::lastMessage() const {
	return _lastMessage.value_or(nullptr);
}

bool History::lastMessageKnown() const {
	return _lastMessage.has_value();
}

HistoryItem *History::lastServerMessage() const {
	return _lastServerMessage.value_or(nullptr);
}

bool History::lastServerMessageKnown() const {
	return _lastServerMessage.has_value();
}

void History::updateChatListExistence() {
	Entry::updateChatListExistence();
}

bool History::useTopPromotion() const {
	if (!isTopPromoted()) {
		return false;
	} else if (const auto channel = peer->asChannel()) {
		return !isPinnedDialog(FilterId()) && !channel->amIn();
	} else if (const auto user = peer->asUser()) {
		return !isPinnedDialog(FilterId()) && user->isBot() && isEmpty();
	}
	return false;
}

int History::fixedOnTopIndex() const {
	return useTopPromotion() ? kTopPromotionFixOnTopIndex : 0;
}

bool History::trackUnreadMessages() const {
	if (const auto channel = peer->asChannel()) {
		return channel->amIn();
	}
	return true;
}

bool History::shouldBeInChatList() const {
	if (peer->migrateTo() || !folderKnown()) {
		return false;
	} else if (isPinnedDialog(FilterId())) {
		return true;
	} else if (const auto channel = peer->asChannel()) {
		if (!channel->amIn()) {
			return isTopPromoted();
		}
	} else if (const auto chat = peer->asChat()) {
		return chat->amIn()
			|| !lastMessageKnown()
			|| (lastMessage() != nullptr);
	} else if (const auto user = peer->asUser()) {
		if (user->isBot() && isTopPromoted()) {
			return true;
		}
	}
	return !lastMessageKnown()
		|| (lastMessage() != nullptr);
}

void History::unknownMessageDeleted(MsgId messageId) {
	if (_inboxReadBefore && messageId >= *_inboxReadBefore) {
		owner().histories().requestDialogEntry(this);
	}
}

bool History::isServerSideUnread(not_null<const HistoryItem*> item) const {
	Expects(item->isRegular());

	return item->out()
		? (!_outboxReadBefore || (item->id >= *_outboxReadBefore))
		: (!_inboxReadBefore || (item->id >= *_inboxReadBefore));
}

void History::applyDialog(
		Data::Folder *requestFolder,
		const MTPDdialog &data) {
	const auto folderId = data.vfolder_id();
	const auto folder = !folderId
		? requestFolder
		: folderId->v
		? owner().folder(folderId->v).get()
		: nullptr;
	applyDialogFields(
		folder,
		data.vunread_count().v,
		data.vread_inbox_max_id().v,
		data.vread_outbox_max_id().v);
	applyDialogTopMessage(data.vtop_message().v);
	setUnreadMark(data.is_unread_mark());
	unreadMentions().setCount(data.vunread_mentions_count().v);
	unreadReactions().setCount(data.vunread_reactions_count().v);
	if (const auto channel = peer->asChannel()) {
		if (const auto pts = data.vpts()) {
			channel->ptsReceived(pts->v);
		}
		if (!channel->amCreator()) {
			const auto topMessageId = FullMsgId(
				channel->id,
				data.vtop_message().v);
			if (const auto item = owner().message(topMessageId)) {
				if (item->date() <= channel->date) {
					session().api().chatParticipants().requestSelf(channel);
				}
			}
		}
	}
	owner().notifySettings().apply(
		MTP_notifyPeer(data.vpeer()),
		data.vnotify_settings());

	const auto draft = data.vdraft();
	if (draft && draft->type() == mtpc_draftMessage) {
		Data::ApplyPeerCloudDraft(
			&session(),
			peer->id,
			MsgId(0), // topicRootId
			draft->c_draftMessage());
	}
	if (const auto ttl = data.vttl_period()) {
		peer->setMessagesTTL(ttl->v);
	}
	owner().histories().dialogEntryApplied(this);
}

void History::dialogEntryApplied() {
	if (!lastServerMessageKnown()) {
		setLastServerMessage(nullptr);
	} else if (!lastMessageKnown()) {
		setLastMessage(nullptr);
	}
	if (peer->migrateTo()) {
		return;
	} else if (!chatListMessageKnown()) {
		requestChatListMessage();
		return;
	}
	if (!chatListMessage()) {
		clear(ClearType::Unload);
		addNewerSlice(QVector<MTPMessage>());
		addOlderSlice(QVector<MTPMessage>());
		if (const auto channel = peer->asChannel()) {
			const auto inviter = channel->inviter;
			if (inviter && channel->amIn()) {
				if (const auto from = owner().userLoaded(inviter)) {
					insertJoinedMessage();
				}
			}
		}
		return;
	}

	if (chatListTimeId() != 0 && loadedAtBottom()) {
		if (const auto channel = peer->asChannel()) {
			const auto inviter = channel->inviter;
			if (inviter
				&& chatListTimeId() <= channel->inviteDate
				&& channel->amIn()) {
				if (const auto from = owner().userLoaded(inviter)) {
					insertJoinedMessage();
				}
			}
		}
	}
}

void History::cacheTopPromotion(
		bool promoted,
		const QString &type,
		const QString &message) {
	const auto changed = (isTopPromoted() != promoted);
	cacheTopPromoted(promoted);
	if (topPromotionType() != type || _topPromotedMessage != message) {
		_topPromotedType = type;
		_topPromotedMessage = message;
		cloudDraftTextCache().clear();
	} else if (changed) {
		cloudDraftTextCache().clear();
	}
}

QStringView History::topPromotionType() const {
	return topPromotionAboutShown()
		? base::StringViewMid(_topPromotedType, 5)
		: QStringView(_topPromotedType);
}

bool History::topPromotionAboutShown() const {
	return _topPromotedType.startsWith("seen^");
}

void History::markTopPromotionAboutShown() {
	if (!topPromotionAboutShown()) {
		_topPromotedType = "seen^" + _topPromotedType;
	}
}

QString History::topPromotionMessage() const {
	return _topPromotedMessage;
}

bool History::clearUnreadOnClientSide() const {
	if (!session().supportMode()) {
		return false;
	}
	if (const auto user = peer->asUser()) {
		if (user->isInaccessible()) {
			return true;
		}
	}
	return false;
}

bool History::skipUnreadUpdate() const {
	return clearUnreadOnClientSide();
}

void History::applyDialogFields(
		Data::Folder *folder,
		int unreadCount,
		MsgId maxInboxRead,
		MsgId maxOutboxRead) {
	if (folder) {
		setFolder(folder);
	} else {
		clearFolder();
	}
	if (!skipUnreadUpdate()
		&& maxInboxRead + 1 >= _inboxReadBefore.value_or(1)) {
		setUnreadCount(unreadCount);
		setInboxReadTill(maxInboxRead);
	}
	setOutboxReadTill(maxOutboxRead);
}

void History::applyDialogTopMessage(MsgId topMessageId) {
	if (topMessageId) {
		const auto itemId = FullMsgId(peer->id, topMessageId);
		if (const auto item = owner().message(itemId)) {
			setLastServerMessage(item);
		} else {
			setLastServerMessage(nullptr);
		}
	} else {
		setLastServerMessage(nullptr);
	}
	if (clearUnreadOnClientSide()) {
		setUnreadCount(0);
		if (const auto last = lastMessage()) {
			setInboxReadTill(last->id);
		}
	}
}

void History::setInboxReadTill(MsgId upTo) {
	if (_inboxReadBefore) {
		accumulate_max(*_inboxReadBefore, upTo + 1);
	} else {
		_inboxReadBefore = upTo + 1;
	}
}

void History::setOutboxReadTill(MsgId upTo) {
	if (_outboxReadBefore) {
		accumulate_max(*_outboxReadBefore, upTo + 1);
	} else {
		_outboxReadBefore = upTo + 1;
	}
}

MsgId History::minMsgId() const {
	for (const auto &block : blocks) {
		for (const auto &message : block->messages) {
			const auto item = message->data();
			if (item->isRegular()) {
				return item->id;
			}
		}
	}
	return 0;
}

MsgId History::maxMsgId() const {
	for (const auto &block : ranges::views::reverse(blocks)) {
		for (const auto &message : ranges::views::reverse(block->messages)) {
			const auto item = message->data();
			if (item->isRegular()) {
				return item->id;
			}
		}
	}
	return 0;
}

MsgId History::msgIdForRead() const {
	const auto last = lastMessage();
	const auto result = (last && last->isRegular())
		? last->id
		: MsgId(0);
	return loadedAtBottom()
		? std::max(result, maxMsgId())
		: result;
}

HistoryItem *History::lastEditableMessage() const {
	if (!loadedAtBottom()) {
		return nullptr;
	}
	const auto now = base::unixtime::now();
	for (const auto &block : ranges::views::reverse(blocks)) {
		for (const auto &message : ranges::views::reverse(block->messages)) {
			const auto item = message->data();
			if (item->allowsEdit(now)) {
				return owner().groups().findItemToEdit(item);
			}
		}
	}
	return nullptr;
}

void History::resizeToWidth(int newWidth) {
	using Request = HistoryBlock::ResizeRequest;
	const auto request = (_flags & Flag::PendingAllItemsResize)
		? Request::ReinitAll
		: (_width != newWidth)
		? Request::ResizeAll
		: Request::ResizePending;
	if (request == Request::ResizePending && !hasPendingResizedItems()) {
		return;
	}
	_flags &= ~(Flag::HasPendingResizedItems | Flag::PendingAllItemsResize);

	_width = newWidth;
	int y = 0;
	for (const auto &block : blocks) {
		block->setY(y);
		y += block->resizeGetHeight(newWidth, request);
	}
	_height = y;
}

void History::forceFullResize() {
	_width = 0;
	_flags |= Flag::HasPendingResizedItems;
}

Data::Thread *History::threadFor(MsgId topicRootId) {
	return topicRootId
		? peer->forumTopicFor(topicRootId)
		: static_cast<Data::Thread*>(this);
}

const Data::Thread *History::threadFor(MsgId topicRootId) const {
	return const_cast<History*>(this)->threadFor(topicRootId);
}

void History::forumChanged(Data::Forum *old) {
	if (inChatList()) {
		notifyUnreadStateChange(old
			? old->topicsList()->unreadState()
			: computeUnreadState());
	}

	if (const auto forum = peer->forum()) {
		_flags |= Flag::IsForum;

		forum->topicsList()->unreadStateChanges(
		) | rpl::filter([=] {
			return (_flags & Flag::IsForum) && inChatList();
		}) | rpl::start_with_next([=](const Dialogs::UnreadState &old) {
			notifyUnreadStateChange(old);
		}, forum->lifetime());

		forum->chatsListChanges(
		) | rpl::start_with_next([=] {
			updateChatListEntry();
		}, forum->lifetime());
	} else {
		_flags &= ~Flag::IsForum;
	}
	if (cloudDraft(MsgId(0))) {
		updateChatListSortPosition();
	}
	_flags |= Flag::PendingAllItemsResize;
}

bool History::isForum() const {
	return (_flags & Flag::IsForum);
}

not_null<History*> History::migrateToOrMe() const {
	if (const auto to = peer->migrateTo()) {
		return owner().history(to);
	}
	// We could get it by owner().history(peer), but we optimize.
	return const_cast<History*>(this);
}

History *History::migrateFrom() const {
	if (const auto from = peer->migrateFrom()) {
		return owner().history(from);
	}
	return nullptr;
}

MsgRange History::rangeForDifferenceRequest() const {
	auto fromId = MsgId(0);
	auto toId = MsgId(0);
	for (const auto &block : blocks) {
		for (const auto &item : block->messages) {
			const auto id = item->data()->id;
			if (id > 0) {
				fromId = id;
				break;
			}
		}
		if (fromId) break;
	}
	if (fromId) {
		for (auto blockIndex = blocks.size(); blockIndex > 0;) {
			const auto &block = blocks[--blockIndex];
			for (auto itemIndex = block->messages.size(); itemIndex > 0;) {
				const auto id = block->messages[--itemIndex]->data()->id;
				if (id > 0) {
					toId = id;
					break;
				}
			}
			if (toId) break;
		}
		return { fromId, toId + 1 };
	}
	return MsgRange();
}

HistoryItem *History::insertJoinedMessage() {
	const auto channel = peer->asChannel();
	if (!channel
		|| _joinedMessage
		|| !channel->amIn()
		|| (peer->isMegagroup()
			&& channel->mgInfo->joinedMessageFound)) {
		return _joinedMessage;
	}

	const auto inviter = (channel->inviter.bare > 0)
		? owner().userLoaded(channel->inviter)
		: nullptr;
	if (!inviter) {
		return nullptr;
	}

	if (peer->isMegagroup()
		&& peer->migrateFrom()
		&& !blocks.empty()
		&& blocks.front()->messages.front()->data()->id == 1) {
		channel->mgInfo->joinedMessageFound = true;
		return nullptr;
	}

	_joinedMessage = GenerateJoinedMessage(
		this,
		channel->inviteDate,
		inviter,
		channel->inviteViaRequest);
	insertMessageToBlocks(_joinedMessage);
	return _joinedMessage;
}

void History::insertMessageToBlocks(not_null<HistoryItem*> item) {
	Expects(item->mainView() == nullptr);

	if (isEmpty()) {
		addNewToBack(item, false);
		return;
	}

	const auto itemDate = item->date();
	for (auto blockIndex = blocks.size(); blockIndex > 0;) {
		const auto &block = blocks[--blockIndex];
		for (auto itemIndex = block->messages.size(); itemIndex > 0;) {
			if (block->messages[--itemIndex]->data()->date() <= itemDate) {
				++itemIndex;
				addNewInTheMiddle(item, blockIndex, itemIndex);
				const auto lastDate = chatListTimeId();
				if (!lastDate || itemDate >= lastDate) {
					setLastMessage(item);
				}
				return;
			}
		}
	}

	startBuildingFrontBlock();
	addItemToBlock(item);
	finishBuildingFrontBlock();
}

void History::checkLocalMessages() {
	if (isEmpty() && (!loadedAtTop() || !loadedAtBottom())) {
		return;
	}
	const auto firstDate = loadedAtTop()
		? 0
		: blocks.front()->messages.front()->data()->date();
	const auto lastDate = loadedAtBottom()
		? std::numeric_limits<TimeId>::max()
		: blocks.back()->messages.back()->data()->date();
	const auto goodDate = [&](TimeId date) {
		return (date >= firstDate && date < lastDate);
	};
	for (const auto &item : _clientSideMessages) {
		if (!item->mainView() && goodDate(item->date())) {
			insertMessageToBlocks(item);
		}
	}
	if (peer->isChannel()
		&& !_joinedMessage
		&& peer->asChannel()->inviter
		&& goodDate(peer->asChannel()->inviteDate)) {
		insertJoinedMessage();
	}
}

void History::removeJoinedMessage() {
	if (_joinedMessage) {
		_joinedMessage->destroy();
	}
}

void History::reactionsEnabledChanged(bool enabled) {
	if (!enabled) {
		for (const auto &item : _messages) {
			item->updateReactions(nullptr);
		}
	} else {
		for (const auto &item : _messages) {
			item->updateReactionsUnknown();
		}
	}
}

bool History::isEmpty() const {
	return blocks.empty();
}

bool History::isDisplayedEmpty() const {
	if (!loadedAtTop() || !loadedAtBottom()) {
		return false;
	}
	const auto first = findFirstNonEmpty();
	if (!first) {
		return true;
	}
	const auto chat = peer->asChat();
	if (!chat || !chat->amCreator()) {
		return false;
	}

	// For legacy chats we want to show the chat with only
	// messages about you creating the group and maybe about you
	// changing the group photo as an empty chat with
	// a nice information about the group features.
	if (nonEmptyCountMoreThan(2)) {
		return false;
	}
	const auto isChangePhoto = [](not_null<HistoryItem*> item) {
		if (const auto media = item->media()) {
			return (media->photo() != nullptr) && item->isService();
		}
		return false;
	};
	const auto last = findLastNonEmpty();
	if (first == last) {
		return first->data()->isGroupEssential()
			|| isChangePhoto(first->data());
	}
	return first->data()->isGroupEssential() && isChangePhoto(last->data());
}

auto History::findFirstNonEmpty() const -> Element* {
	for (const auto &block : blocks) {
		for (const auto &element : block->messages) {
			if (!element->data()->isEmpty()) {
				return element.get();
			}
		}
	}
	return nullptr;
}

auto History::findFirstDisplayed() const -> Element* {
	for (const auto &block : blocks) {
		for (const auto &element : block->messages) {
			if (!element->data()->isEmpty() && !element->isHidden()) {
				return element.get();
			}
		}
	}
	return nullptr;
}

auto History::findLastNonEmpty() const -> Element* {
	for (const auto &block : ranges::views::reverse(blocks)) {
		for (const auto &element : ranges::views::reverse(block->messages)) {
			if (!element->data()->isEmpty()) {
				return element.get();
			}
		}
	}
	return nullptr;
}

auto History::findLastDisplayed() const -> Element* {
	for (const auto &block : ranges::views::reverse(blocks)) {
		for (const auto &element : ranges::views::reverse(block->messages)) {
			if (!element->data()->isEmpty() && !element->isHidden()) {
				return element.get();
			}
		}
	}
	return nullptr;
}

bool History::nonEmptyCountMoreThan(int count) const {
	Expects(count >= 0);

	for (const auto &block : blocks) {
		for (const auto &element : block->messages) {
			if (!element->data()->isEmpty()) {
				if (!count--) {
					return true;
				}
			}
		}
	}
	return false;
}

bool History::hasOrphanMediaGroupPart() const {
	if (loadedAtTop() || !loadedAtBottom()) {
		return false;
	} else if (blocks.size() != 1) {
		return false;
	} else if (blocks.front()->messages.size() != 1) {
		return false;
	}
	const auto last = blocks.front()->messages.front()->data();
	return last->groupId() != MessageGroupId();
}

std::vector<MsgId> History::collectMessagesFromParticipantToDelete(
		not_null<PeerData*> participant) const {
	auto result = std::vector<MsgId>();
	for (const auto &block : blocks) {
		for (const auto &message : block->messages) {
			const auto item = message->data();
			if (item->from() == participant && item->canDelete()) {
				result.push_back(item->id);
			}
		}
	}
	return result;
}

void History::clear(ClearType type) {
	_unreadBarView = nullptr;
	_firstUnreadView = nullptr;
	removeJoinedMessage();

	forgetScrollState();
	blocks.clear();
	owner().notifyHistoryUnloaded(this);
	lastKeyboardInited = false;
	if (type == ClearType::Unload) {
		_loadedAtTop = _loadedAtBottom = false;
	} else {
		// Leave the 'sending' messages in local messages.
		auto local = base::flat_set<not_null<HistoryItem*>>();
		for (const auto &item : _clientSideMessages) {
			if (!item->isSending()) {
				local.emplace(item);
			}
		}
		for (const auto &item : local) {
			item->destroy();
		}
		clearNotifications();
		owner().notifyHistoryCleared(this);
		if (unreadCountKnown()) {
			setUnreadCount(0);
		}
		if (type == ClearType::DeleteChat) {
			setLastServerMessage(nullptr);
		} else if (_lastMessage && *_lastMessage) {
			if ((*_lastMessage)->isRegular()) {
				(*_lastMessage)->applyEditionToHistoryCleared();
			} else {
				_lastMessage = std::nullopt;
			}
		}
		const auto tillId = (_lastMessage && *_lastMessage)
			? (*_lastMessage)->id
			: std::numeric_limits<MsgId>::max();
		clearUpTill(tillId);
		if (blocks.empty() && _lastMessage && *_lastMessage) {
			addItemToBlock(*_lastMessage);
		}
		_loadedAtTop = _loadedAtBottom = _lastMessage.has_value();
		clearSharedMedia();
		clearLastKeyboard();
	}

	if (const auto chat = peer->asChat()) {
		chat->lastAuthors.clear();
		chat->markupSenders.clear();
	} else if (const auto channel = peer->asMegagroup()) {
		channel->mgInfo->markupSenders.clear();
	}

	owner().notifyHistoryChangeDelayed(this);
	owner().sendHistoryChangeNotifications();
}

void History::clearUpTill(MsgId availableMinId) {
	auto remove = std::vector<not_null<HistoryItem*>>();
	remove.reserve(_messages.size());
	for (const auto &item : _messages) {
		const auto itemId = item->id;
		if (!item->isRegular()) {
			continue;
		} else if (itemId == availableMinId) {
			item->applyEditionToHistoryCleared();
		} else if (itemId < availableMinId) {
			remove.push_back(item.get());
		}
	}
	for (const auto item : remove) {
		item->destroy();
	}
	requestChatListMessage();
}

void History::applyGroupAdminChanges(const base::flat_set<UserId> &changes) {
	for (const auto &block : blocks) {
		for (const auto &message : block->messages) {
			message->applyGroupAdminChanges(changes);
		}
	}
}

void History::changedChatListPinHook() {
	session().changes().historyUpdated(this, UpdateFlag::IsPinned);
}

void History::removeBlock(not_null<HistoryBlock*> block) {
	Expects(block->messages.empty());

	if (_buildingFrontBlock && block == _buildingFrontBlock->block) {
		_buildingFrontBlock->block = nullptr;
	}

	int index = block->indexInHistory();
	blocks.erase(blocks.begin() + index);
	if (index < blocks.size()) {
		for (int i = index, l = blocks.size(); i < l; ++i) {
			blocks[i]->setIndexInHistory(i);
		}
		blocks[index]->messages.front()->previousInBlocksChanged();
	} else if (!blocks.empty() && !blocks.back()->messages.empty()) {
		blocks.back()->messages.back()->nextInBlocksRemoved();
	}
}

void History::cacheTopPromoted(bool promoted) {
	if (isTopPromoted() == promoted) {
		return;
	} else if (promoted) {
		_flags |= Flag::IsTopPromoted;
	} else {
		_flags &= ~Flag::IsTopPromoted;
	}
	updateChatListSortPosition();
	updateChatListEntry();
	if (!isTopPromoted()) {
		updateChatListExistence();
	}
}

bool History::isTopPromoted() const {
	return (_flags & Flag::IsTopPromoted);
}

void History::translateOfferFrom(LanguageId id) {
	if (!id) {
		if (translatedTo()) {
			_translation->offerFrom(id);
		} else if (_translation) {
			_translation = nullptr;
			session().changes().historyUpdated(
				this,
				UpdateFlag::TranslateFrom);
		}
	} else if (!_translation) {
		_translation = std::make_unique<HistoryTranslation>(this, id);
	} else {
		_translation->offerFrom(id);
	}
}

LanguageId History::translateOfferedFrom() const {
	return _translation ? _translation->offeredFrom() : LanguageId();
}

void History::translateTo(LanguageId id) {
	if (!_translation) {
		return;
	} else if (!id && !translateOfferedFrom()) {
		_translation = nullptr;
		session().changes().historyUpdated(this, UpdateFlag::TranslatedTo);
	} else {
		_translation->translateTo(id);
	}
}

LanguageId History::translatedTo() const {
	return _translation ? _translation->translatedTo() : LanguageId();
}

HistoryTranslation *History::translation() const {
	return _translation.get();
}

HistoryBlock::HistoryBlock(not_null<History*> history)
: _history(history) {
}

int HistoryBlock::resizeGetHeight(int newWidth, ResizeRequest request) {
	auto y = 0;
	if (request == ResizeRequest::ReinitAll) {
		for (const auto &message : messages) {
			message->setY(y);
			message->initDimensions();
			y += message->resizeGetHeight(newWidth);
		}
	} else if (request == ResizeRequest::ResizeAll) {
		for (const auto &message : messages) {
			message->setY(y);
			y += message->resizeGetHeight(newWidth);
		}
	} else {
		for (const auto &message : messages) {
			message->setY(y);
			y += message->pendingResize()
				? message->resizeGetHeight(newWidth)
				: message->height();
		}
	}
	_height = y;
	return _height;
}

void HistoryBlock::remove(not_null<Element*> view) {
	Expects(view->block() == this);

	_history->mainViewRemoved(this, view);

	const auto blockIndex = indexInHistory();
	const auto itemIndex = view->indexInBlock();
	const auto item = view->data();
	item->clearMainView();
	messages.erase(messages.begin() + itemIndex);
	for (auto i = itemIndex, l = int(messages.size()); i < l; ++i) {
		messages[i]->setIndexInBlock(i);
	}
	if (messages.empty()) {
		// Deletes this.
		_history->removeBlock(this);
	} else if (itemIndex < messages.size()) {
		messages[itemIndex]->previousInBlocksChanged();
	} else if (blockIndex + 1 < _history->blocks.size()) {
		_history->blocks[blockIndex + 1]->messages.front()->previousInBlocksChanged();
	} else if (!_history->blocks.empty() && !_history->blocks.back()->messages.empty()) {
		_history->blocks.back()->messages.back()->nextInBlocksRemoved();
	}
}

void HistoryBlock::refreshView(not_null<Element*> view) {
	Expects(view->block() == this);

	const auto item = view->data();
	auto refreshed = item->createView(
		_history->delegateMixin()->delegate(),
		view);

	auto blockIndex = indexInHistory();
	auto itemIndex = view->indexInBlock();
	_history->viewReplaced(view, refreshed.get());

	messages[itemIndex] = std::move(refreshed);
	messages[itemIndex]->attachToBlock(this, itemIndex);
	if (itemIndex + 1 < messages.size()) {
		messages[itemIndex + 1]->previousInBlocksChanged();
	} else if (blockIndex + 1 < _history->blocks.size()) {
		_history->blocks[blockIndex + 1]->messages.front()->previousInBlocksChanged();
	} else if (!_history->blocks.empty() && !_history->blocks.back()->messages.empty()) {
		_history->blocks.back()->messages.back()->nextInBlocksRemoved();
	}
}

HistoryBlock::~HistoryBlock() = default;
