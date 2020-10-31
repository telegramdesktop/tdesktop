/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history.h"

#include "history/view/history_view_element.h"
#include "history/history_message.h"
#include "history/history_service.h"
#include "history/history_item_components.h"
#include "history/history_inner_widget.h"
#include "dialogs/dialogs_indexed_list.h"
#include "data/stickers/data_stickers.h"
#include "data/data_drafts.h"
#include "data/data_session.h"
#include "data/data_media_types.h"
#include "data/data_channel_admins.h"
#include "data/data_changes.h"
#include "data/data_chat_filters.h"
#include "data/data_scheduled_messages.h"
#include "data/data_folder.h"
#include "data/data_photo.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "data/data_document.h"
#include "data/data_histories.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "window/notifications_manager.h"
#include "calls/calls_instance.h"
#include "storage/localstorage.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "storage/storage_account.h"
//#include "storage/storage_feed_messages.h" // #feed
#include "support/support_helper.h"
#include "ui/image/image.h"
#include "ui/text/text_options.h"
#include "core/crash_reports.h"
#include "core/application.h"
#include "base/unixtime.h"
#include "styles/style_dialogs.h"

namespace {

constexpr auto kNewBlockEachMessage = 50;
constexpr auto kSkipCloudDraftsFor = TimeId(3);

using UpdateFlag = Data::HistoryUpdate::Flag;

} // namespace

History::History(not_null<Data::Session*> owner, PeerId peerId)
: Entry(owner, Type::History)
, peer(owner->peer(peerId))
, cloudDraftTextCache(st::dialogsTextWidthMin)
, _mute(owner->notifyIsMuted(peer))
, _sendActionPainter(this) {
	if (const auto user = peer->asUser()) {
		if (user->isBot()) {
			_outboxReadBefore = std::numeric_limits<MsgId>::max();
		}
	}
}

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

void History::removeNotification(not_null<HistoryItem*> item) {
	_notifications.erase(
		ranges::remove(_notifications, item),
		end(_notifications));
}

HistoryItem *History::currentNotification() {
	return empty(_notifications)
		? nullptr
		: _notifications.front().get();
}

bool History::hasNotification() const {
	return !empty(_notifications);
}

void History::skipNotification() {
	if (!empty(_notifications)) {
		_notifications.pop_front();
	}
}

void History::popNotification(HistoryItem *item) {
	if (!empty(_notifications) && (_notifications.back() == item)) {
		_notifications.pop_back();
	}
}

bool History::hasPendingResizedItems() const {
	return _flags & Flag::f_has_pending_resized_items;
}

void History::setHasPendingResizedItems() {
	_flags |= Flag::f_has_pending_resized_items;
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
		unregisterLocalMessage(item);
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
	//if (const auto channel = peer->asChannel()) { // #feed
	//	if (const auto feed = channel->feed()) {
	//		// Must be after history->chatListMessage() is updated.
	//		// Otherwise feed last message will be this value again.
	//		feed->messageRemoved(item);
	//	}
	//}
}

void History::itemVanished(not_null<HistoryItem*> item) {
	removeNotification(item);
	if (lastKeyboardId == item->id) {
		clearLastKeyboard();
	}
	if ((!item->out() || item->isPost())
		&& item->unread()
		&& unreadCount() > 0) {
		setUnreadCount(unreadCount() - 1);
	}
}

void History::setLocalDraft(std::unique_ptr<Data::Draft> &&draft) {
	_localDraft = std::move(draft);
}

void History::takeLocalDraft(History *from) {
	if (auto &draft = from->_localDraft) {
		if (!draft->textWithTags.text.isEmpty() && !_localDraft) {
			_localDraft = std::move(draft);

			// Edit and reply to drafts can't migrate.
			// Cloud drafts do not migrate automatically.
			_localDraft->msgId = 0;
		}
		from->clearLocalDraft();
		session().api().saveDraftToCloudDelayed(from);
	}
}

void History::createLocalDraftFromCloud() {
	const auto draft = cloudDraft();
	if (!draft) {
		clearLocalDraft();
		return;
	} else if (Data::draftIsNull(draft) || !draft->date) {
		return;
	}

	auto existing = localDraft();
	if (Data::draftIsNull(existing)
		|| !existing->date
		|| draft->date >= existing->date) {
		if (!existing) {
			setLocalDraft(std::make_unique<Data::Draft>(
				draft->textWithTags,
				draft->msgId,
				draft->cursor,
				draft->previewCancelled));
			existing = localDraft();
		} else if (existing != draft) {
			existing->textWithTags = draft->textWithTags;
			existing->msgId = draft->msgId;
			existing->cursor = draft->cursor;
			existing->previewCancelled = draft->previewCancelled;
		}
		existing->date = draft->date;
	}
}

void History::setCloudDraft(std::unique_ptr<Data::Draft> &&draft) {
	_cloudDraft = std::move(draft);
	cloudDraftTextCache.clear();
}

Data::Draft *History::createCloudDraft(const Data::Draft *fromDraft) {
	if (Data::draftIsNull(fromDraft)) {
		setCloudDraft(std::make_unique<Data::Draft>(
			TextWithTags(),
			0,
			MessageCursor(),
			false));
		cloudDraft()->date = TimeId(0);
	} else {
		auto existing = cloudDraft();
		if (!existing) {
			setCloudDraft(std::make_unique<Data::Draft>(
				fromDraft->textWithTags,
				fromDraft->msgId,
				fromDraft->cursor,
				fromDraft->previewCancelled));
			existing = cloudDraft();
		} else if (existing != fromDraft) {
			existing->textWithTags = fromDraft->textWithTags;
			existing->msgId = fromDraft->msgId;
			existing->cursor = fromDraft->cursor;
			existing->previewCancelled = fromDraft->previewCancelled;
		}
		existing->date = base::unixtime::now();
	}

	cloudDraftTextCache.clear();
	updateChatListSortPosition();

	return cloudDraft();
}

bool History::skipCloudDraft(const QString &text, MsgId replyTo, TimeId date) const {
	if (Data::draftStringIsEmpty(text)
		&& !replyTo
		&& date > 0
		&& date <= _lastSentDraftTime + kSkipCloudDraftsFor) {
		return true;
	} else if (_lastSentDraftText && *_lastSentDraftText == text) {
		return true;
	}
	return false;
}

void History::setSentDraftText(const QString &text) {
	_lastSentDraftText = text;
}

void History::clearSentDraftText(const QString &text) {
	if (_lastSentDraftText && *_lastSentDraftText == text) {
		_lastSentDraftText = std::nullopt;
	}
	accumulate_max(_lastSentDraftTime, base::unixtime::now());
}

void History::setEditDraft(std::unique_ptr<Data::Draft> &&draft) {
	_editDraft = std::move(draft);
}

void History::clearLocalDraft() {
	_localDraft = nullptr;
}

void History::clearCloudDraft() {
	if (_cloudDraft) {
		_cloudDraft = nullptr;
		cloudDraftTextCache.clear();
		updateChatListSortPosition();
	}
}

void History::applyCloudDraft() {
	if (session().supportMode()) {
		updateChatListEntry();
		session().supportHelper().cloudDraftChanged(this);
	} else {
		createLocalDraftFromCloud();
		updateChatListSortPosition();
		session().changes().historyUpdated(this, UpdateFlag::CloudDraft);
	}
}

void History::clearEditDraft() {
	_editDraft = nullptr;
}

void History::draftSavedToCloud() {
	updateChatListEntry();
	session().local().writeDrafts(this);
}

HistoryItemsList History::validateForwardDraft() {
	auto result = owner().idsToItems(_forwardDraft);
	if (result.size() != _forwardDraft.size()) {
		setForwardDraft(owner().itemsToIds(result));
	}
	return result;
}

void History::setForwardDraft(MessageIdsList &&items) {
	_forwardDraft = std::move(items);
}

HistoryItem *History::createItem(
		const MTPMessage &message,
		MTPDmessage_ClientFlags clientFlags,
		bool detachExistingItem) {
	const auto messageId = IdFromMessage(message);
	if (!messageId) {
		return nullptr;
	}

	if (const auto result = owner().message(channelId(), messageId)) {
		if (detachExistingItem) {
			result->removeMainView();
		}
		return result;
	}
	return HistoryItem::Create(this, message, clientFlags);
}

std::vector<not_null<HistoryItem*>> History::createItems(
		const QVector<MTPMessage> &data) {
	auto result = std::vector<not_null<HistoryItem*>>();
	result.reserve(data.size());
	const auto clientFlags = MTPDmessage_ClientFlags();
	for (auto i = data.cend(), e = data.cbegin(); i != e;) {
		const auto detachExistingItem = true;
		const auto item = createItem(*--i, clientFlags, detachExistingItem);
		if (item) {
			result.emplace_back(item);
		}
	}
	return result;
}

HistoryItem *History::addNewMessage(
		const MTPMessage &msg,
		MTPDmessage_ClientFlags clientFlags,
		NewMessageType type) {
	const auto detachExistingItem = (type == NewMessageType::Unread);
	const auto item = createItem(msg, clientFlags, detachExistingItem);
	if (!item) {
		return nullptr;
	}
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
		if (IsServerMsgId(item->id)) {
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

	const auto document = [&] {
		const auto media = item->media();
		return media ? media->document() : nullptr;
	}();

	owner().unregisterMessage(item);
	Core::App().notifications().clearFromItem(item);

	auto hack = std::unique_ptr<HistoryItem>(item.get());
	const auto i = _messages.find(hack);
	hack.release();

	Assert(i != end(_messages));
	_messages.erase(i);

	if (document) {
		session().data().documentMessageRemoved(document);
	}
}

void History::unpinAllMessages() {
	session().storage().remove(
		Storage::SharedMediaRemoveAll(
			peer->id,
			Storage::SharedMediaType::Pinned));
	peer->setHasPinnedMessages(false);
	for (const auto &message : _messages) {
		if (message->isPinned()) {
			message->setIsPinned(false);
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
	if (!loadedAtBottom() || peer->migrateTo()) {
		setLastMessage(item);
		if (unread) {
			newItemAdded(item);
		}
	} else {
		addNewToBack(item, unread);
		checkForLoadedAtTop(item);
		if (!unread) {
			// When we add just one last item, like we do while loading dialogs,
			// we want to remove a single added grouped media, otherwise it will
			// jump once we open the message history (first we show only that
			// media, then we load the rest of the group and show the group).
			//
			// That way when we open the message history we show nothing until a
			// whole history part is loaded, it certainly will contain the group.
			removeOrphanMediaGroupPart();
		}
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
		MTPDmessage::Flags flags,
		MTPDmessage_ClientFlags clientFlags,
		TimeId date,
		PeerId from,
		const QString &postAuthor,
		not_null<HistoryMessage*> forwardOriginal) {
	return addNewItem(
		makeMessage(
			id,
			flags,
			clientFlags,
			date,
			from,
			postAuthor,
			forwardOriginal),
		true);
}

not_null<HistoryItem*> History::addNewLocalMessage(
		MsgId id,
		MTPDmessage::Flags flags,
		MTPDmessage_ClientFlags clientFlags,
		UserId viaBotId,
		MsgId replyTo,
		TimeId date,
		PeerId from,
		const QString &postAuthor,
		not_null<DocumentData*> document,
		const TextWithEntities &caption,
		const MTPReplyMarkup &markup) {
	return addNewItem(
		makeMessage(
			id,
			flags,
			clientFlags,
			replyTo,
			viaBotId,
			date,
			from,
			postAuthor,
			document,
			caption,
			markup),
		true);
}

not_null<HistoryItem*> History::addNewLocalMessage(
		MsgId id,
		MTPDmessage::Flags flags,
		MTPDmessage_ClientFlags clientFlags,
		UserId viaBotId,
		MsgId replyTo,
		TimeId date,
		PeerId from,
		const QString &postAuthor,
		not_null<PhotoData*> photo,
		const TextWithEntities &caption,
		const MTPReplyMarkup &markup) {
	return addNewItem(
		makeMessage(
			id,
			flags,
			clientFlags,
			replyTo,
			viaBotId,
			date,
			from,
			postAuthor,
			photo,
			caption,
			markup),
		true);
}

not_null<HistoryItem*> History::addNewLocalMessage(
		MsgId id,
		MTPDmessage::Flags flags,
		MTPDmessage_ClientFlags clientFlags,
		UserId viaBotId,
		MsgId replyTo,
		TimeId date,
		PeerId from,
		const QString &postAuthor,
		not_null<GameData*> game,
		const MTPReplyMarkup &markup) {
	return addNewItem(
		makeMessage(
			id,
			flags,
			clientFlags,
			replyTo,
			viaBotId,
			date,
			from,
			postAuthor,
			game,
			markup),
		true);
}

void History::setUnreadMentionsCount(int count) {
	const auto had = _unreadMentionsCount && (*_unreadMentionsCount > 0);
	if (_unreadMentions.size() > count) {
		LOG(("API Warning: real mentions count is greater than received mentions count"));
		count = _unreadMentions.size();
	}
	_unreadMentionsCount = count;
	const auto has = (count > 0);
	if (has != had) {
		owner().chatsFilters().refreshHistory(this);
		updateChatListEntry();
	}
}

bool History::addToUnreadMentions(
		MsgId msgId,
		UnreadMentionType type) {
	if (peer->isChannel() && !peer->isMegagroup()) {
		return false;
	}
	auto allLoaded = _unreadMentionsCount
		? (_unreadMentions.size() >= *_unreadMentionsCount)
		: false;
	if (allLoaded) {
		if (type == UnreadMentionType::New) {
			_unreadMentions.insert(msgId);
			setUnreadMentionsCount(*_unreadMentionsCount + 1);
			return true;
		}
	} else if (!_unreadMentions.empty() && type != UnreadMentionType::New) {
		_unreadMentions.insert(msgId);
		return true;
	}
	return false;
}

void History::eraseFromUnreadMentions(MsgId msgId) {
	_unreadMentions.remove(msgId);
	if (_unreadMentionsCount && *_unreadMentionsCount > 0) {
		setUnreadMentionsCount(*_unreadMentionsCount - 1);
	}
	session().changes().historyUpdated(this, UpdateFlag::UnreadMentions);
}

void History::addUnreadMentionsSlice(const MTPmessages_Messages &result) {
	auto count = 0;
	auto messages = (const QVector<MTPMessage>*)nullptr;
	auto getMessages = [&](auto &list) {
		owner().processUsers(list.vusers());
		owner().processChats(list.vchats());
		return &list.vmessages().v;
	};
	switch (result.type()) {
	case mtpc_messages_messages: {
		auto &d = result.c_messages_messages();
		messages = getMessages(d);
		count = messages->size();
	} break;

	case mtpc_messages_messagesSlice: {
		auto &d = result.c_messages_messagesSlice();
		messages = getMessages(d);
		count = d.vcount().v;
	} break;

	case mtpc_messages_channelMessages: {
		LOG(("API Error: unexpected messages.channelMessages! (History::addUnreadMentionsSlice)"));
		auto &d = result.c_messages_channelMessages();
		messages = getMessages(d);
		count = d.vcount().v;
	} break;

	case mtpc_messages_messagesNotModified: {
		LOG(("API Error: received messages.messagesNotModified! (History::addUnreadMentionsSlice)"));
	} break;

	default: Unexpected("type in History::addUnreadMentionsSlice");
	}

	auto added = false;
	if (messages) {
		const auto clientFlags = MTPDmessage_ClientFlags();
		const auto type = NewMessageType::Existing;
		for (const auto &message : *messages) {
			if (const auto item = addNewMessage(message, clientFlags, type)) {
				if (item->isUnreadMention()) {
					_unreadMentions.insert(item->id);
					added = true;
				}
			}
		}
	}
	if (!added) {
		count = _unreadMentions.size();
	}
	setUnreadMentionsCount(count);
	session().changes().historyUpdated(this, UpdateFlag::UnreadMentions);
}

not_null<HistoryItem*> History::addNewToBack(
		not_null<HistoryItem*> item,
		bool unread) {
	Expects(!isBuildingFrontBlock());

	addItemToBlock(item);

	if (!unread && IsServerMsgId(item->id)) {
		if (const auto sharedMediaTypes = item->sharedMediaTypes()) {
			auto from = loadedAtTop() ? 0 : minMsgId();
			auto till = loadedAtBottom() ? ServerMaxMsgId : maxMsgId();
			session().storage().add(Storage::SharedMediaAddExisting(
				peer->id,
				sharedMediaTypes,
				item->id,
				{ from, till }));
			if (sharedMediaTypes.test(Storage::SharedMediaType::Pinned)) {
				peer->setHasPinnedMessages(true);
			}
		}
	}
	if (item->from()->id) {
		if (auto user = item->from()->asUser()) {
			auto getLastAuthors = [this]() -> std::deque<not_null<UserData*>>* {
				if (auto chat = peer->asChat()) {
					return &chat->lastAuthors;
				} else if (auto channel = peer->asMegagroup()) {
					return &channel->mgInfo->lastParticipants;
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
			if (!(markupFlags & MTPDreplyKeyboardMarkup::Flag::f_selective)
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
				if (markupFlags & MTPDreplyKeyboardMarkup_ClientFlag::f_zero) { // zero markup means replyKeyboardHide
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
								|| !peer->canWrite())
							&& !peer->asChat()->participants.contains(
								item->from()->asUser());
					} else if (peer->isMegagroup()) {
						botNotInChat = item->from()->isUser()
							&& (peer->asChannel()->mgInfo->botStatus != 0
								|| !peer->canWrite())
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
	auto &action = data.vaction();
	switch (action.type()) {
	case mtpc_messageActionChatAddUser: {
		auto &d = action.c_messageActionChatAddUser();
		if (const auto megagroup = peer->asMegagroup()) {
			const auto mgInfo = megagroup->mgInfo.get();
			Assert(mgInfo != nullptr);
			for (const auto &userId : d.vusers().v) {
				if (const auto user = owner().userLoaded(userId.v)) {
					if (!base::contains(mgInfo->lastParticipants, user)) {
						mgInfo->lastParticipants.push_front(user);
						session().changes().peerUpdated(
							peer,
							Data::PeerUpdate::Flag::Members);
						owner().addNewMegagroupParticipant(megagroup, user);
					}
					if (user->isBot()) {
						peer->asChannel()->mgInfo->bots.insert(user);
						if (peer->asChannel()->mgInfo->botStatus != 0 && peer->asChannel()->mgInfo->botStatus < 2) {
							peer->asChannel()->mgInfo->botStatus = 2;
						}
					}
				}
			}
		}
	} break;

	case mtpc_messageActionChatJoinedByLink: {
		auto &d = action.c_messageActionChatJoinedByLink();
		if (auto megagroup = peer->asMegagroup()) {
			auto mgInfo = megagroup->mgInfo.get();
			Assert(mgInfo != nullptr);
			if (auto user = item->from()->asUser()) {
				if (!base::contains(mgInfo->lastParticipants, user)) {
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
			}
		}
	} break;

	case mtpc_messageActionChatDeletePhoto: {
		if (const auto chat = peer->asChat()) {
			chat->setPhoto(MTP_chatPhotoEmpty());
		}
	} break;

	case mtpc_messageActionChatDeleteUser: {
		auto &d = action.c_messageActionChatDeleteUser();
		auto uid = d.vuser_id().v;
		if (lastKeyboardFrom == peerFromUser(uid)) {
			clearLastKeyboard();
		}
		if (auto megagroup = peer->asMegagroup()) {
			if (auto user = owner().userLoaded(uid)) {
				auto mgInfo = megagroup->mgInfo.get();
				Assert(mgInfo != nullptr);
				auto i = ranges::find(
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
					megagroup->setMembersCount(megagroup->membersCount() - 1);
				} else {
					mgInfo->lastParticipantsStatus |= MegagroupInfo::LastParticipantsCountOutdated;
					mgInfo->lastParticipantsCount = 0;
				}
				if (mgInfo->lastAdmins.contains(user)) {
					mgInfo->lastAdmins.remove(user);
					if (megagroup->adminsCount() > 1) {
						megagroup->setAdminsCount(megagroup->adminsCount() - 1);
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
	} break;

	case mtpc_messageActionChatEditPhoto: {
		auto &d = action.c_messageActionChatEditPhoto();
		d.vphoto().match([&](const MTPDphoto &data) {
			const auto &sizes = data.vsizes().v;
			if (!sizes.isEmpty()) {
				auto photo = owner().processPhoto(data);
				photo->peer = peer;
				auto &smallSize = sizes.front();
				auto &bigSize = sizes.back();
				const MTPFileLocation *smallLoc = nullptr;
				const MTPFileLocation *bigLoc = nullptr;
				switch (smallSize.type()) {
				case mtpc_photoSize: smallLoc = &smallSize.c_photoSize().vlocation(); break;
				case mtpc_photoCachedSize: smallLoc = &smallSize.c_photoCachedSize().vlocation(); break;
				}
				switch (bigSize.type()) {
				case mtpc_photoSize: bigLoc = &bigSize.c_photoSize().vlocation(); break;
				case mtpc_photoCachedSize: bigLoc = &bigSize.c_photoCachedSize().vlocation(); break;
				}
				if (smallLoc && bigLoc) {
					const auto chatPhoto = MTP_chatPhoto(
						MTP_flags(photo->hasVideo()
							? MTPDchatPhoto::Flag::f_has_video
							: MTPDchatPhoto::Flag(0)),
						*smallLoc,
						*bigLoc,
						data.vdc_id());
					if (const auto chat = peer->asChat()) {
						chat->setPhoto(photo->id, chatPhoto);
					} else if (const auto channel = peer->asChannel()) {
						channel->setPhoto(photo->id, chatPhoto);
					}
					peer->loadUserpic();
				}
			}
		}, [&](const MTPDphotoEmpty &data) {
			if (const auto chat = peer->asChat()) {
				chat->setPhoto(MTP_chatPhotoEmpty());
			} else if (const auto channel = peer->asChannel()) {
				channel->setPhoto(MTP_chatPhotoEmpty());
			}
		});
	} break;

	case mtpc_messageActionChatEditTitle: {
		auto &d = action.c_messageActionChatEditTitle();
		if (auto chat = peer->asChat()) {
			chat->setName(qs(d.vtitle()));
		}
	} break;

	case mtpc_messageActionChatMigrateTo: {
		if (const auto chat = peer->asChat()) {
			chat->addFlags(MTPDchat::Flag::f_deactivated);
			const auto &d = action.c_messageActionChatMigrateTo();
			if (const auto channel = owner().channelLoaded(d.vchannel_id().v)) {
				Data::ApplyMigration(chat, channel);
			}
		}
	} break;

	case mtpc_messageActionChannelMigrateFrom: {
		if (const auto channel = peer->asChannel()) {
			channel->addFlags(MTPDchannel::Flag::f_megagroup);
			const auto &d = action.c_messageActionChannelMigrateFrom();
			if (const auto chat = owner().chatLoaded(d.vchat_id().v)) {
				Data::ApplyMigration(chat, channel);
			}
		}
	} break;

	case mtpc_messageActionPinMessage: {
		if (const auto replyTo = data.vreply_to()) {
			replyTo->match([&](const MTPDmessageReplyHeader &data) {
				const auto id = data.vreply_to_msg_id().v;
				if (item) {
					session().storage().add(Storage::SharedMediaAddSlice(
						peer->id,
						Storage::SharedMediaType::Pinned,
						{ id },
						{ id, ServerMaxMsgId }));
					peer->setHasPinnedMessages(true);
				}
			});
		}
	} break;
	}
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
			owner().repliesSendActionPaintersClear(this, from);
		}
		from->madeAction(item->date());
	}
	item->contributeToSlowmode();
	if (item->showNotification()) {
		_notifications.push_back(item);
		owner().notifyUnreadItemAdded(item);
		const auto stillShow = item->showNotification();
		if (stillShow) {
			Core::App().notifications().schedule(item);
			if (!item->out() && item->unread()) {
				if (unreadCountKnown()) {
					setUnreadCount(unreadCount() + 1);
				} else {
					owner().histories().requestDialogEntry(this);
				}
			}
		}
	} else if (item->out()) {
		destroyUnreadBar();
	} else if (!item->unread()) {
		inboxRead(item);
	}
	if (item->out() && !item->unread()) {
		outboxRead(item);
	}
	item->incrementReplyToTopCounter();
	if (!folderKnown()) {
		owner().histories().requestDialogEntry(this);
	}
}

void History::registerLocalMessage(not_null<HistoryItem*> item) {
	Expects(item->isHistoryEntry());
	Expects(IsClientMsgId(item->id));

	_localMessages.emplace(item);
	session().changes().historyUpdated(this, UpdateFlag::LocalMessages);
}

void History::unregisterLocalMessage(not_null<HistoryItem*> item) {
	const auto removed = _localMessages.remove(item);
	Assert(removed);

	session().changes().historyUpdated(this, UpdateFlag::LocalMessages);
}

const base::flat_set<not_null<HistoryItem*>> &History::localMessages() {
	return _localMessages;
}

HistoryItem *History::latestSendingMessage() const {
	auto sending = ranges::view::all(
		_localMessages
	) | ranges::view::filter([](not_null<HistoryItem*> item) {
		return item->isSending();
	});
	const auto i = ranges::max_element(sending, ranges::less(), [](
			not_null<HistoryItem*> item) {
		return uint64(item->date()) << 32 | uint32(item->id);
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

	block->messages.push_back(item->createView(
		HistoryInner::ElementDelegate()));
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
		startBuildingFrontBlock(added.size());
		for (const auto item : added) {
			addItemToBlock(item);
		}
		finishBuildingFrontBlock();

		if (loadedAtBottom()) {
			// Add photos to overview and authors to lastAuthors.
			addItemsToLists(added);
		}
		addToSharedMedia(added);
	} else {
		// If no items were added it means we've loaded everything old.
		_loadedAtTop = true;
		addEdgesToSharedMedia();
	}

	checkLocalMessages();
	checkLastMessage();
}

void History::addNewerSlice(const QVector<MTPMessage> &slice) {
	bool wasEmpty = isEmpty(), wasLoadedAtBottom = loadedAtBottom();

	if (slice.isEmpty()) {
		_loadedAtBottom = true;
		if (!lastMessage()) {
			setLastMessage(lastAvailableMessage());
		}
	}

	if (const auto added = createItems(slice); !added.empty()) {
		Assert(!isBuildingFrontBlock());

		for (const auto item : added) {
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
	for (const auto item : ranges::view::reverse(items)) {
		item->addToUnreadMentions(UnreadMentionType::Existing);
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
					auto markupFlags = item->replyKeyboardFlags();
					if (!(markupFlags & MTPDreplyKeyboardMarkup::Flag::f_selective) || item->mentionsMe()) {
						bool wasKeyboardHide = markupSenders->contains(item->author());
						if (!wasKeyboardHide) {
							markupSenders->insert(item->author());
						}
						if (!(markupFlags & MTPDreplyKeyboardMarkup_ClientFlag::f_zero)) {
							if (!lastKeyboardInited) {
								bool botNotInChat = false;
								if (peer->isChat()) {
									botNotInChat = (!peer->canWrite() || !peer->asChat()->participants.empty()) && item->author()->isUser() && !peer->asChat()->participants.contains(item->author()->asUser());
								} else if (peer->isMegagroup()) {
									botNotInChat = (!peer->canWrite() || peer->asChannel()->mgInfo->botStatus != 0) && item->author()->isUser() && !peer->asChannel()->mgInfo->bots.contains(item->author()->asUser());
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
				MTPDreplyKeyboardMarkup::Flags markupFlags = item->replyKeyboardFlags();
				if (!(markupFlags & MTPDreplyKeyboardMarkup::Flag::f_selective) || item->mentionsMe()) {
					if (markupFlags & MTPDreplyKeyboardMarkup_ClientFlag::f_zero) {
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
			item->addToUnreadMentions(UnreadMentionType::Existing);
		}
	}
}

void History::addToSharedMedia(
		const std::vector<not_null<HistoryItem*>> &items) {
	std::vector<MsgId> medias[Storage::kSharedMediaTypeCount];
	for (const auto item : items) {
		if (const auto types = item->sharedMediaTypes()) {
			for (auto i = 0; i != Storage::kSharedMediaTypeCount; ++i) {
				const auto type = static_cast<Storage::SharedMediaType>(i);
				if (types.test(type)) {
					if (medias[i].empty()) {
						medias[i].reserve(items.size());
					}
					medias[i].push_back(item->id);
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
				type,
				std::move(medias[i]),
				{ from, till }));
			if (type == Storage::SharedMediaType::Pinned) {
				peer->setHasPinnedMessages(true);
			}
		}
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
	for (const auto &block : ranges::view::reverse(blocks)) {
		for (const auto &message : ranges::view::reverse(block->messages)) {
			const auto item = message->data();
			if (!IsServerMsgId(item->id)) {
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
		).arg(_inboxReadBefore.value_or(-666)));
	return IsServerMsgId(tillId) && (_inboxReadBefore.value_or(1) <= tillId);
}

void History::readClientSideMessages() {
	auto &histories = owner().histories();
	for (const auto item : _localMessages) {
		histories.readClientSideMessage(item);
	}
}

bool History::unreadCountRefreshNeeded(MsgId readTillId) const {
	return !unreadCountKnown()
		|| ((readTillId + 1) > _inboxReadBefore.value_or(0));
}

std::optional<int> History::countStillUnreadLocal(MsgId readTillId) const {
	if (isEmpty() || !folderKnown()) {
		DEBUG_LOG(("Reading: countStillUnreadLocal unknown %1 and %2."
			).arg(Logs::b(isEmpty())
			).arg(Logs::b(folderKnown())));
		return std::nullopt;
	}
	if (_inboxReadBefore) {
		const auto before = *_inboxReadBefore;
		DEBUG_LOG(("Reading: check before %1 with min %2 and max %3."
			).arg(before
			).arg(minMsgId()
			).arg(maxMsgId()));
		if (minMsgId() <= before && maxMsgId() >= readTillId) {
			auto result = 0;
			for (const auto &block : blocks) {
				for (const auto &message : block->messages) {
					const auto item = message->data();
					if (!IsServerMsgId(item->id)
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
	DEBUG_LOG(("Reading: check at end loaded from %1 loaded %2 - %3"
		).arg(minimalServerId
		).arg(Logs::b(loadedAtBottom())
		).arg(Logs::b(loadedAtTop())));
	if (!loadedAtBottom()
		|| (!loadedAtTop() && !minimalServerId)
		|| minimalServerId > readTillId) {
		return std::nullopt;
	}
	auto result = 0;
	for (const auto &block : ranges::view::reverse(blocks)) {
		for (const auto &message : ranges::view::reverse(block->messages)) {
			const auto item = message->data();
			if (IsServerMsgId(item->id)) {
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
	if (IsServerMsgId(wasRead->id)) {
		inboxRead(wasRead->id);
	}
}

void History::outboxRead(MsgId upTo) {
	setOutboxReadTill(upTo);
	if (const auto last = chatListMessage()) {
		if (last->out() && IsServerMsgId(last->id) && last->id <= upTo) {
			session().changes().messageUpdated(
				last,
				Data::MessageUpdate::Flag::DialogRowRepaint);
		}
	}
	updateChatListEntry();
	session().changes().historyUpdated(this, UpdateFlag::OutboxRead);
}

void History::outboxRead(not_null<const HistoryItem*> wasRead) {
	if (IsServerMsgId(wasRead->id)) {
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

int History::unreadCountForBadge() const {
	const auto result = unreadCount();
	return (!result && unreadMark()) ? 1 : result;
}

bool History::unreadCountKnown() const {
	return _unreadCount.has_value();
}

void History::setUnreadCount(int newUnreadCount) {
	Expects(folderKnown());

	if (_unreadCount == newUnreadCount) {
		return;
	}
	const auto wasForBadge = (unreadCountForBadge() > 0);
	const auto refresher = gsl::finally([&] {
		if (wasForBadge != (unreadCountForBadge() > 0)) {
			owner().chatsFilters().refreshHistory(this);
		}
		session().changes().historyUpdated(this, UpdateFlag::UnreadView);
	});
	const auto notifier = unreadStateChangeNotifier(true);
	_unreadCount = newUnreadCount;

	if (newUnreadCount == 1) {
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
	if (_unreadMark == unread) {
		return;
	}
	const auto noUnreadMessages = !unreadCount();
	const auto refresher = gsl::finally([&] {
		if (inChatList() && noUnreadMessages) {
			owner().chatsFilters().refreshHistory(this);
			updateChatListEntry();
		}
		session().changes().historyUpdated(this, UpdateFlag::UnreadView);
	});
	const auto notifier = unreadStateChangeNotifier(noUnreadMessages);
	_unreadMark = unread;
}

bool History::unreadMark() const {
	return _unreadMark;
}

void History::setFakeUnreadWhileOpened(bool enabled) {
	if (_fakeUnreadWhileOpened == enabled
		|| (enabled
			&& (!inChatList()
				|| (!unreadCount()
					&& !unreadMark()
					&& !hasUnreadMentions())))) {
		return;
	}
	_fakeUnreadWhileOpened = enabled;
	owner().chatsFilters().refreshHistory(this);
}

[[nodiscard]] bool History::fakeUnreadWhileOpened() const {
	return _fakeUnreadWhileOpened;
}

bool History::mute() const {
	return _mute;
}

bool History::changeMute(bool newMute) {
	if (_mute == newMute) {
		return false;
	}
	const auto refresher = gsl::finally([&] {
		if (inChatList()) {
			owner().chatsFilters().refreshHistory(this);
			updateChatListEntry();
		}
		session().changes().peerUpdated(
			peer,
			Data::PeerUpdate::Flag::Notifications);
	});
	const auto notify = (unreadCountForBadge() > 0);
	const auto notifier = unreadStateChangeNotifier(notify);
	_mute = newMute;
	return true;
}

void History::getNextFirstUnreadMessage() {
	Expects(_firstUnreadView != nullptr);

	const auto block = _firstUnreadView->block();
	const auto index = _firstUnreadView->indexInBlock();
	const auto setFromMessage = [&](const auto &view) {
		if (IsServerMsgId(view->data()->id)) {
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
	auto &filters = owner().chatsFilters();
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
	if (const auto draft = cloudDraft()) {
		if (!Data::draftIsNull(draft) && !session().supportMode()) {
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
	if (_unreadBarView || !_firstUnreadView || !unreadCount()) {
		return;
	}
	if (const auto count = chatListUnreadCount()) {
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
		item->createView(
			HistoryInner::ElementDelegate()));
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

int History::chatListUnreadCount() const {
	const auto result = unreadCount();
	if (const auto migrated = migrateSibling()) {
		return result + migrated->unreadCount();
	}
	return result;
}

bool History::chatListUnreadMark() const {
	if (unreadMark()) {
		return true;
	} else if (const auto migrated = migrateSibling()) {
		return migrated->unreadMark();
	}
	return false;
}

bool History::chatListMutedBadge() const {
	return mute();
}

Dialogs::UnreadState History::chatListUnreadState() const {
	auto result = Dialogs::UnreadState();
	const auto count = _unreadCount.value_or(0);
	const auto mark = !count && _unreadMark;
	result.messages = count;
	result.messagesMuted = mute() ? count : 0;
	result.chats = count ? 1 : 0;
	result.chatsMuted = (count && mute()) ? 1 : 0;
	result.marks = mark ? 1 : 0;
	result.marksMuted = (mark && mute()) ? 1 : 0;
	result.known = _unreadCount.has_value();
	return result;
}

HistoryItem *History::chatListMessage() const {
	return _chatListMessage.value_or(nullptr);
}

bool History::chatListMessageKnown() const {
	return _chatListMessage.has_value();
}

const QString &History::chatListName() const {
	return peer->name;
}

const base::flat_set<QString> &History::chatListNameWords() const {
	return peer->nameWords();
}

const base::flat_set<QChar> &History::chatListFirstLetters() const {
	return peer->nameFirstLetters();
}

void History::loadUserpic() {
	peer->loadUserpic();
}

void History::paintUserpic(
		Painter &p,
		std::shared_ptr<Data::CloudImageView> &view,
		int x,
		int y,
		int size) const {
	peer->paintUserpic(p, view, x, y, size);
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
			const auto last = block->messages.back().get();
			const auto first = blocks[1]->messages.front().get();

			// we've added a new front block, so previous item for
			// the old first item of a first block was changed
			first->previousInBlocksChanged();
		} else {
			block->messages.back()->nextInBlocksRemoved();
		}
	}
}

void History::clearNotifications() {
	_notifications.clear();
}

void History::clearIncomingNotifications() {
	if (!peer->isSelf()) {
		_notifications.erase(
			ranges::remove(_notifications, false, &HistoryItem::out),
			end(_notifications));
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
	const auto item = owner().message(channelId(), msgId);
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
	//if (const auto channel = peer->asChannel()) { // #feed
	//	if (const auto feed = channel->feed()) {
	//		session().storage().invalidate(
	//			Storage::FeedMessagesInvalidateBottom(
	//				feed->id()));
	//	}
	//}
}

void History::clearSharedMedia() {
	session().storage().remove(
		Storage::SharedMediaRemoveAll(peer->id));
	//if (const auto channel = peer->asChannel()) { // #feed
	//	if (const auto feed = channel->feed()) {
	//		session().storage().remove(
	//			Storage::FeedMessagesRemoveAll(
	//				feed->id(),
	//				channel->bareId()));
	//	}
	//}
}

void History::setLastServerMessage(HistoryItem *item) {
	_lastServerMessage = item;
	if (_lastMessage
		&& *_lastMessage
		&& !IsServerMsgId((*_lastMessage)->id)
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
	if (!item || IsServerMsgId(item->id)) {
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
		if (_chatListMessage
			&& *_chatListMessage
			&& !IsServerMsgId((*_chatListMessage)->id)
			&& (*_chatListMessage)->date() > item->date()) {
			return;
		}
		_chatListMessage = item;
		setChatListTimeId(item->date());
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
			for (const auto &block : ranges::view::reverse(blocks)) {
				const auto &messages = block->messages;
				for (const auto &item : ranges::view::reverse(messages)) {
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
	if (const auto chat = peer->asChat()) {
		// In chats we try to take the item before the 'last', which
		// is the empty-displayed migration message.
		owner().histories().requestFakeChatListMessage(this);
	} else if (const auto from = migrateFrom()) {
		// In megagroups we just try to use
		// the message from the original group.
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
		MTPDmessage_ClientFlags(),
		NewMessageType::Existing);
	if (!item || item->isGroupMigrate()) {
		// Not better than the last one.
		return;
	}
	setChatListMessage(item);
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
	//if (const auto channel = peer->asChannel()) { // #feed
	//	if (!channel->feed()) {
	//		// After ungrouping from a feed we need to load dialog.
	//		requestChatListMessage();
	//		if (!unreadCountKnown()) {
	//			owner().histories().requestDialogEntry(this);
	//		}
	//	}
	//}
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
		//} else if (const auto feed = channel->feed()) { // #feed
		//	return !feed->needUpdateInChatList();
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
	Expects(IsServerMsgId(item->id));

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
	setUnreadMentionsCount(data.vunread_mentions_count().v);
	if (const auto channel = peer->asChannel()) {
		if (const auto pts = data.vpts()) {
			channel->ptsReceived(pts->v);
		}
		if (!channel->amCreator()) {
			const auto topMessageId = FullMsgId(
				peerToChannel(channel->id),
				data.vtop_message().v);
			if (const auto item = owner().message(topMessageId)) {
				if (item->date() <= channel->date) {
					session().api().requestSelfParticipant(channel);
				}
			}
		}
	}
	owner().applyNotifySetting(
		MTP_notifyPeer(data.vpeer()),
		data.vnotify_settings());

	const auto draft = data.vdraft();
	if (draft && draft->type() == mtpc_draftMessage) {
		Data::ApplyPeerCloudDraft(
			&session(),
			peer->id,
			draft->c_draftMessage());
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
			if (inviter > 0 && channel->amIn()) {
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
			if (inviter > 0
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
		cloudDraftTextCache.clear();
	} else if (changed) {
		cloudDraftTextCache.clear();
	}
}

QStringRef History::topPromotionType() const {
	return topPromotionAboutShown()
		? _topPromotedType.midRef(5)
		: _topPromotedType.midRef(0);
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
		if (user->flags() & MTPDuser::Flag::f_deleted) {
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
		const auto itemId = FullMsgId(
			channelId(),
			topMessageId);
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
			if (IsServerMsgId(item->id)) {
				return item->id;
			}
		}
	}
	return 0;
}

MsgId History::maxMsgId() const {
	for (const auto &block : ranges::view::reverse(blocks)) {
		for (const auto &message : ranges::view::reverse(block->messages)) {
			const auto item = message->data();
			if (IsServerMsgId(item->id)) {
				return item->id;
			}
		}
	}
	return 0;
}

MsgId History::msgIdForRead() const {
	const auto last = lastMessage();
	const auto result = (last && IsServerMsgId(last->id))
		? last->id
		: MsgId(0);
	return loadedAtBottom()
		? std::max(result, maxMsgId())
		: result;
}

HistoryItem *History::lastSentMessage() const {
	if (!loadedAtBottom()) {
		return nullptr;
	}
	for (const auto &block : ranges::view::reverse(blocks)) {
		for (const auto &message : ranges::view::reverse(block->messages)) {
			const auto item = message->data();
			if (item->canBeEditedFromHistory()) {
				return item;
			}
		}
	}
	return nullptr;
}

void History::resizeToWidth(int newWidth) {
	const auto resizeAllItems = (_width != newWidth);

	if (!resizeAllItems && !hasPendingResizedItems()) {
		return;
	}
	_flags &= ~(Flag::f_has_pending_resized_items);

	_width = newWidth;
	int y = 0;
	for (const auto &block : blocks) {
		block->setY(y);
		y += block->resizeGetHeight(newWidth, resizeAllItems);
	}
	_height = y;
}

void History::forceFullResize() {
	_width = 0;
	_flags |= Flag::f_has_pending_resized_items;
}

ChannelId History::channelId() const {
	return peerToChannel(peer->id);
}

bool History::isChannel() const {
	return peerIsChannel(peer->id);
}

bool History::isMegagroup() const {
	return peer->isMegagroup();
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

HistoryService *History::insertJoinedMessage() {
	if (!isChannel()
		|| _joinedMessage
		|| !peer->asChannel()->amIn()
		|| (peer->isMegagroup()
			&& peer->asChannel()->mgInfo->joinedMessageFound)) {
		return _joinedMessage;
	}

	const auto inviter = (peer->asChannel()->inviter > 0)
		? owner().userLoaded(peer->asChannel()->inviter)
		: nullptr;
	if (!inviter) {
		return nullptr;
	}

	if (peer->isMegagroup()
		&& peer->migrateFrom()
		&& !blocks.empty()
		&& blocks.front()->messages.front()->data()->id == 1) {
		peer->asChannel()->mgInfo->joinedMessageFound = true;
		return nullptr;
	}

	const auto flags = MTPDmessage::Flags();
	const auto inviteDate = peer->asChannel()->inviteDate;
	_joinedMessage = GenerateJoinedMessage(this, inviteDate, inviter, flags);
	insertLocalMessage(_joinedMessage);
	return _joinedMessage;
}

void History::insertLocalMessage(not_null<HistoryItem*> item) {
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
	for (const auto &item : _localMessages) {
		if (!item->mainView() && goodDate(item->date())) {
			insertLocalMessage(item);
		}
	}
	if (isChannel()
		&& !_joinedMessage
		&& (peer->asChannel()->inviter > 0)
		&& goodDate(peer->asChannel()->inviteDate)) {
		insertJoinedMessage();
	}
}

void History::removeJoinedMessage() {
	if (_joinedMessage) {
		_joinedMessage->destroy();
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
			return (media->photo() != nullptr) && !item->toHistoryMessage();
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
	for (const auto &block : ranges::view::reverse(blocks)) {
		for (const auto &element : ranges::view::reverse(block->messages)) {
			if (!element->data()->isEmpty()) {
				return element.get();
			}
		}
	}
	return nullptr;
}

auto History::findLastDisplayed() const -> Element* {
	for (const auto &block : ranges::view::reverse(blocks)) {
		for (const auto &element : ranges::view::reverse(block->messages)) {
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

bool History::removeOrphanMediaGroupPart() {
	if (hasOrphanMediaGroupPart()) {
		clear(ClearType::Unload);
		return true;
	}
	return false;
}

QVector<MsgId> History::collectMessagesFromUserToDelete(
		not_null<UserData*> user) const {
	auto result = QVector<MsgId>();
	for (const auto &block : blocks) {
		for (const auto &message : block->messages) {
			const auto item = message->data();
			if (item->from() == user && item->canDelete()) {
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
		for (const auto item : _localMessages) {
			if (!item->isSending()) {
				local.emplace(item);
			}
		}
		for (const auto item : local) {
			item->destroy();
		}
		_notifications.clear();
		owner().notifyHistoryCleared(this);
		if (unreadCountKnown()) {
			setUnreadCount(0);
		}
		if (type == ClearType::DeleteChat) {
			setLastServerMessage(nullptr);
		} else if (_lastMessage && *_lastMessage) {
			if (IsServerMsgId((*_lastMessage)->id)) {
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
		if (const auto channel = peer->asChannel()) {
			//if (const auto feed = channel->feed()) { // #feed
			//	// Should be after resetting the _lastMessage.
			//	feed->historyCleared(this);
			//}
		}
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
		if (!IsServerMsgId(itemId)) {
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

History::~History() = default;

HistoryBlock::HistoryBlock(not_null<History*> history)
: _history(history) {
}

int HistoryBlock::resizeGetHeight(int newWidth, bool resizeAllItems) {
	auto y = 0;
	for (const auto &message : messages) {
		message->setY(y);
		if (resizeAllItems || message->pendingResize()) {
			y += message->resizeGetHeight(newWidth);
		} else {
			y += message->height();
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
		HistoryInner::ElementDelegate(),
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
