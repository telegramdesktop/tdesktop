/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "history.h"

#include "core/click_handler_types.h"
#include "dialogs/dialogs_indexed_list.h"
#include "styles/style_dialogs.h"
#include "history/history_service_layout.h"
#include "data/data_drafts.h"
#include "lang.h"
#include "mainwidget.h"
#include "application.h"
#include "fileuploader.h"
#include "mainwindow.h"
#include "ui/filedialog.h"
#include "boxes/addcontactbox.h"
#include "boxes/confirmbox.h"
#include "audio.h"
#include "localstorage.h"
#include "apiwrap.h"
#include "window/top_bar_widget.h"
#include "playerwidget.h"
#include "observer_peer.h"

namespace {

TextParseOptions _historySrvOptions = {
	TextParseLinks | TextParseMentions | TextParseHashtags/* | TextParseMultiline*/ | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // lang-dependent
};
TextParseOptions _webpageTitleOptions = {
	TextParseMultiline | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};
TextParseOptions _webpageDescriptionOptions = {
	TextParseLinks | TextParseMultiline | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};
TextParseOptions _twitterDescriptionOptions = {
	TextParseLinks | TextParseMentions | TextTwitterMentions | TextParseHashtags | TextTwitterHashtags | TextParseMultiline | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};
TextParseOptions _instagramDescriptionOptions = {
	TextParseLinks | TextParseMentions | TextInstagramMentions | TextParseHashtags | TextInstagramHashtags | TextParseMultiline | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

inline void _initTextOptions() {
	_historySrvOptions.dir = _textNameOptions.dir = _textDlgOptions.dir = cLangDir();
	_textDlgOptions.maxw = st::dialogsWidthMax * 2;
	_webpageTitleOptions.maxw = st::msgMaxWidth - st::msgPadding.left() - st::msgPadding.right() - st::webPageLeft;
	_webpageTitleOptions.maxh = st::webPageTitleFont->height * 2;
	_webpageDescriptionOptions.maxw = st::msgMaxWidth - st::msgPadding.left() - st::msgPadding.right() - st::webPageLeft;
	_webpageDescriptionOptions.maxh = st::webPageDescriptionFont->height * 3;
}

inline const TextParseOptions &itemTextOptions(HistoryItem *item) {
	return itemTextOptions(item->history(), item->author());
}
inline const TextParseOptions &itemTextNoMonoOptions(const HistoryItem *item) {
	return itemTextNoMonoOptions(item->history(), item->author());
}

bool needReSetInlineResultDocument(const MTPMessageMedia &media, DocumentData *existing) {
	if (media.type() == mtpc_messageMediaDocument) {
		if (DocumentData *document = App::feedDocument(media.c_messageMediaDocument().vdocument)) {
			if (document == existing) {
				return false;
			} else {
				document->collectLocalData(existing);
			}
		}
	}
	return true;
}

MediaOverviewType messageMediaToOverviewType(HistoryMedia *media) {
	switch (media->type()) {
	case MediaTypePhoto: return OverviewPhotos;
	case MediaTypeVideo: return OverviewVideos;
	case MediaTypeFile: return OverviewFiles;
	case MediaTypeMusicFile: return media->getDocument()->isMusic() ? OverviewMusicFiles : OverviewFiles;
	case MediaTypeVoiceFile: return OverviewVoiceFiles;
	case MediaTypeGif: return media->getDocument()->isGifv() ? OverviewCount : OverviewFiles;
	default: break;
	}
	return OverviewCount;
}

MediaOverviewType serviceMediaToOverviewType(HistoryMedia *media) {
	switch (media->type()) {
	case MediaTypePhoto: return OverviewChatPhotos;
	default: break;
	}
	return OverviewCount;
}

} // namespace

void historyInit() {
	_initTextOptions();
}

History::History(const PeerId &peerId)
: peer(App::peer(peerId))
, lastItemTextCache(st::dialogsTextWidthMin)
, typingText(st::dialogsTextWidthMin)
, cloudDraftTextCache(st::dialogsTextWidthMin)
, _mute(isNotifyMuted(peer->notify)) {
	if (peer->isUser() && peer->asUser()->botInfo) {
		outboxReadBefore = INT_MAX;
	}
	for (auto &countData : overviewCountData) {
		countData = -1; // not loaded yet
	}
}

void History::clearLastKeyboard() {
	if (lastKeyboardId) {
		if (lastKeyboardId == lastKeyboardHiddenId) {
			lastKeyboardHiddenId = 0;
		}
		lastKeyboardId = 0;
	}
	lastKeyboardInited = true;
	lastKeyboardFrom = 0;
}

bool History::canHaveFromPhotos() const {
	if (peer->isUser() && !Adaptive::Wide()) {
		return false;
	} else if (isChannel() && !peer->isMegagroup()) {
		return false;
	}
	return true;
}

void History::setHasPendingResizedItems() {
	_flags |= Flag::f_has_pending_resized_items;
	Global::RefHandleHistoryUpdate().call();
}

void History::setLocalDraft(std_::unique_ptr<Data::Draft> &&draft) {
	_localDraft = std_::move(draft);
}

void History::takeLocalDraft(History *from) {
	if (auto &draft = from->_localDraft) {
		if (!draft->textWithTags.text.isEmpty() && !_localDraft) {
			_localDraft = std_::move(draft);

			// Edit and reply to drafts can't migrate.
			// Cloud drafts do not migrate automatically.
			_localDraft->msgId = 0;
		}
		from->clearLocalDraft();
	}
}

void History::createLocalDraftFromCloud() {
	auto draft = cloudDraft();
	if (Data::draftIsNull(draft) || !draft->date.isValid()) return;

	auto existing = localDraft();
	if (Data::draftIsNull(existing) || !existing->date.isValid() || draft->date >= existing->date) {
		if (!existing) {
			setLocalDraft(std_::make_unique<Data::Draft>(draft->textWithTags, draft->msgId, draft->cursor, draft->previewCancelled));
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

void History::setCloudDraft(std_::unique_ptr<Data::Draft> &&draft) {
	_cloudDraft = std_::move(draft);
	cloudDraftTextCache.clear();
}

Data::Draft *History::createCloudDraft(Data::Draft *fromDraft) {
	if (Data::draftIsNull(fromDraft)) {
		setCloudDraft(std_::make_unique<Data::Draft>(TextWithTags(), 0, MessageCursor(), false));
		cloudDraft()->date = QDateTime();
	} else {
		auto existing = cloudDraft();
		if (!existing) {
			setCloudDraft(std_::make_unique<Data::Draft>(fromDraft->textWithTags, fromDraft->msgId, fromDraft->cursor, fromDraft->previewCancelled));
			existing = cloudDraft();
		} else if (existing != fromDraft) {
			existing->textWithTags = fromDraft->textWithTags;
			existing->msgId = fromDraft->msgId;
			existing->cursor = fromDraft->cursor;
			existing->previewCancelled = fromDraft->previewCancelled;
		}
		existing->date = ::date(myunixtime());
	}

	cloudDraftTextCache.clear();
	updateChatListSortPosition();
	updateChatListEntry();

	return cloudDraft();
}

void History::setEditDraft(std_::unique_ptr<Data::Draft> &&draft) {
	_editDraft = std_::move(draft);
}

void History::clearLocalDraft() {
	_localDraft = nullptr;
}

void History::clearCloudDraft() {
	if (_cloudDraft) {
		_cloudDraft = nullptr;
		cloudDraftTextCache.clear();
		updateChatListSortPosition();
		updateChatListEntry();
	}
}

void History::clearEditDraft() {
	_editDraft = nullptr;
}

void History::draftSavedToCloud() {
	updateChatListEntry();
	if (App::main()) App::main()->writeDrafts(this);
}

bool History::updateTyping(uint64 ms, bool force) {
	bool changed = force;
	for (TypingUsers::iterator i = typing.begin(), e = typing.end(); i != e;) {
		if (ms >= i.value()) {
			i = typing.erase(i);
			changed = true;
		} else {
			++i;
		}
	}
	for (SendActionUsers::iterator i = sendActions.begin(); i != sendActions.cend();) {
		if (ms >= i.value().until) {
			i = sendActions.erase(i);
			changed = true;
		} else {
			++i;
		}
	}
	if (changed) {
		QString newTypingStr;
		int32 cnt = typing.size();
		if (cnt > 2) {
			newTypingStr = lng_many_typing(lt_count, cnt);
		} else if (cnt > 1) {
			newTypingStr = lng_users_typing(lt_user, typing.begin().key()->firstName, lt_second_user, (typing.end() - 1).key()->firstName);
		} else if (cnt) {
			newTypingStr = peer->isUser() ? lang(lng_typing) : lng_user_typing(lt_user, typing.begin().key()->firstName);
		} else if (!sendActions.isEmpty()) {
			switch (sendActions.begin().value().type) {
			case SendActionRecordVideo: newTypingStr = peer->isUser() ? lang(lng_send_action_record_video) : lng_user_action_record_video(lt_user, sendActions.begin().key()->firstName); break;
			case SendActionUploadVideo: newTypingStr = peer->isUser() ? lang(lng_send_action_upload_video) : lng_user_action_upload_video(lt_user, sendActions.begin().key()->firstName); break;
			case SendActionRecordVoice: newTypingStr = peer->isUser() ? lang(lng_send_action_record_audio) : lng_user_action_record_audio(lt_user, sendActions.begin().key()->firstName); break;
			case SendActionUploadVoice: newTypingStr = peer->isUser() ? lang(lng_send_action_upload_audio) : lng_user_action_upload_audio(lt_user, sendActions.begin().key()->firstName); break;
			case SendActionUploadPhoto: newTypingStr = peer->isUser() ? lang(lng_send_action_upload_photo) : lng_user_action_upload_photo(lt_user, sendActions.begin().key()->firstName); break;
			case SendActionUploadFile: newTypingStr = peer->isUser() ? lang(lng_send_action_upload_file) : lng_user_action_upload_file(lt_user, sendActions.begin().key()->firstName); break;
			case SendActionChooseLocation: newTypingStr = peer->isUser() ? lang(lng_send_action_geo_location) : lng_user_action_geo_location(lt_user, sendActions.begin().key()->firstName); break;
			case SendActionChooseContact: newTypingStr = peer->isUser() ? lang(lng_send_action_choose_contact) : lng_user_action_choose_contact(lt_user, sendActions.begin().key()->firstName); break;
			}
		}
		if (!newTypingStr.isEmpty()) {
			newTypingStr += qsl("...");
		}
		if (typingStr != newTypingStr) {
			typingText.setText(st::dialogsTextFont, (typingStr = newTypingStr), _textNameOptions);
		}
	}
	if (!typingStr.isEmpty()) {
		if (typingText.lastDots(typingDots % 4)) {
			changed = true;
		}
	}
	if (changed && App::main()) {
		updateChatListEntry();
		if (App::main()->historyPeer() == peer) {
			App::main()->topBar()->update();
		}
	}
	return changed;
}

ChannelHistory::ChannelHistory(const PeerId &peer) : History(peer)
, _joinedMessage(nullptr) {
}

void ChannelHistory::getRangeDifference() {
	MsgId fromId = 0, toId = 0;
	for (int32 blockIndex = 0, blocksCount = blocks.size(); blockIndex < blocksCount; ++blockIndex) {
		HistoryBlock *block = blocks.at(blockIndex);
		for (int32 itemIndex = 0, itemsCount = block->items.size(); itemIndex < itemsCount; ++itemIndex) {
			HistoryItem *item = block->items.at(itemIndex);
			if (item->type() == HistoryItemMsg && item->id > 0) {
				fromId = item->id;
				break;
			}
		}
		if (fromId) break;
	}
	if (!fromId) return;
	for (int32 blockIndex = blocks.size(); blockIndex > 0;) {
		HistoryBlock *block = blocks.at(--blockIndex);
		for (int32 itemIndex = block->items.size(); itemIndex > 0;) {
			HistoryItem *item = block->items.at(--itemIndex);
			if (item->type() == HistoryItemMsg && item->id > 0) {
				toId = item->id;
				break;
			}
		}
		if (toId) break;
	}
	if (fromId > 0 && peer->asChannel()->pts() > 0) {
		if (_rangeDifferenceRequestId) {
			MTP::cancel(_rangeDifferenceRequestId);
		}
		_rangeDifferenceFromId = fromId;
		_rangeDifferenceToId = toId;

		MTP_LOG(0, ("getChannelDifference { good - after channelDifferenceTooLong was received, validating history part }%1").arg(cTestMode() ? " TESTMODE" : ""));
		getRangeDifferenceNext(peer->asChannel()->pts());
	}
}

void ChannelHistory::getRangeDifferenceNext(int32 pts) {
	if (!App::main() || _rangeDifferenceToId < _rangeDifferenceFromId) return;

	int32 limit = _rangeDifferenceToId + 1 - _rangeDifferenceFromId;
	_rangeDifferenceRequestId = MTP::send(MTPupdates_GetChannelDifference(peer->asChannel()->inputChannel, MTP_channelMessagesFilter(MTP_flags(MTPDchannelMessagesFilter::Flags(0)), MTP_vector<MTPMessageRange>(1, MTP_messageRange(MTP_int(_rangeDifferenceFromId), MTP_int(_rangeDifferenceToId)))), MTP_int(pts), MTP_int(limit)), App::main()->rpcDone(&MainWidget::gotRangeDifference, peer->asChannel()));
}

HistoryJoined *ChannelHistory::insertJoinedMessage(bool unread) {
	if (_joinedMessage || !peer->asChannel()->amIn() || (peer->isMegagroup() && peer->asChannel()->mgInfo->joinedMessageFound)) {
		return _joinedMessage;
	}

	UserData *inviter = (peer->asChannel()->inviter > 0) ? App::userLoaded(peer->asChannel()->inviter) : nullptr;
	if (!inviter) return nullptr;

	MTPDmessage::Flags flags = 0;
	if (peerToUser(inviter->id) == MTP::authedId()) {
		unread = false;
	//} else if (unread) {
	//	flags |= MTPDmessage::Flag::f_unread;
	}

	QDateTime inviteDate = peer->asChannel()->inviteDate;
	if (unread) _maxReadMessageDate = inviteDate;
	if (isEmpty()) {
		_joinedMessage = HistoryJoined::create(this, inviteDate, inviter, flags);
		addNewItem(_joinedMessage, unread);
		return _joinedMessage;
	}

	for (int32 blockIndex = blocks.size(); blockIndex > 0;) {
		HistoryBlock *block = blocks.at(--blockIndex);
		for (int32 itemIndex = block->items.size(); itemIndex > 0;) {
			HistoryItem *item = block->items.at(--itemIndex);
			HistoryItemType type = item->type();
			if (type == HistoryItemMsg) {
				// Due to a server bug sometimes inviteDate is less (before) than the
				// first message in the megagroup (message about migration), let us
				// ignore that and think, that the inviteDate is always greater-or-equal.
				if (item->isGroupMigrate() && peer->isMegagroup() && peer->migrateFrom()) {
					peer->asChannel()->mgInfo->joinedMessageFound = true;
					return nullptr;
				}
				if (item->date <= inviteDate) {
					++itemIndex;
					_joinedMessage = HistoryJoined::create(this, inviteDate, inviter, flags);
					addNewInTheMiddle(_joinedMessage, blockIndex, itemIndex);
					if (lastMsgDate.isNull() || inviteDate >= lastMsgDate) {
						setLastMessage(_joinedMessage);
						if (unread) {
							newItemAdded(_joinedMessage);
						}
					}
					return _joinedMessage;
				}
			}
		}
	}

	startBuildingFrontBlock();

	_joinedMessage = HistoryJoined::create(this, inviteDate, inviter, flags);
	addItemToBlock(_joinedMessage);

	finishBuildingFrontBlock();

	return _joinedMessage;
}

void ChannelHistory::checkJoinedMessage(bool createUnread) {
	if (_joinedMessage || peer->asChannel()->inviter <= 0) {
		return;
	}
	if (isEmpty()) {
		if (loadedAtTop() && loadedAtBottom()) {
			if (insertJoinedMessage(createUnread)) {
				if (!_joinedMessage->detached()) {
					setLastMessage(_joinedMessage);
				}
			}
			return;
		}
	}

	QDateTime inviteDate = peer->asChannel()->inviteDate;
	QDateTime firstDate, lastDate;
	for (int blockIndex = 0, blocksCount = blocks.size(); blockIndex < blocksCount; ++blockIndex) {
		HistoryBlock *block = blocks.at(blockIndex);
		int itemIndex = 0, itemsCount = block->items.size();
		for (; itemIndex < itemsCount; ++itemIndex) {
			HistoryItem *item = block->items.at(itemIndex);
			HistoryItemType type = item->type();
			if (type == HistoryItemMsg) {
				firstDate = item->date;
				break;
			}
		}
		if (itemIndex < itemsCount) break;
	}
	for (int blockIndex = blocks.size(); blockIndex > 0;) {
		HistoryBlock *block = blocks.at(--blockIndex);
		int itemIndex = block->items.size();
		for (; itemIndex > 0;) {
			HistoryItem *item = block->items.at(--itemIndex);
			HistoryItemType type = item->type();
			if (type == HistoryItemMsg) {
				lastDate = item->date;
				++itemIndex;
				break;
			}
		}
		if (itemIndex) break;
	}

	if (!firstDate.isNull() && !lastDate.isNull() && (firstDate <= inviteDate || loadedAtTop()) && (lastDate > inviteDate || loadedAtBottom())) {
		bool willBeLastMsg = (inviteDate >= lastDate);
		if (insertJoinedMessage(createUnread && willBeLastMsg) && willBeLastMsg) {
			if (!_joinedMessage->detached()) {
				setLastMessage(_joinedMessage);
			}
		}
	}
}

void ChannelHistory::checkMaxReadMessageDate() {
	if (_maxReadMessageDate.isValid()) return;

	for (int blockIndex = blocks.size(); blockIndex > 0;) {
		HistoryBlock *block = blocks.at(--blockIndex);
		for (int itemIndex = block->items.size(); itemIndex > 0;) {
			HistoryItem *item = block->items.at(--itemIndex);
			if (!item->unread()) {
				_maxReadMessageDate = item->date;
				if (item->isGroupMigrate() && isMegagroup() && peer->migrateFrom()) {
					_maxReadMessageDate = date(MTP_int(peer->asChannel()->date + 1)); // no report spam panel
				}
				return;
			}
		}
	}
	if (loadedAtTop() && (!isMegagroup() || !isEmpty())) {
		_maxReadMessageDate = date(MTP_int(peer->asChannel()->date));
	}
}

const QDateTime &ChannelHistory::maxReadMessageDate() {
	return _maxReadMessageDate;
}

HistoryItem *ChannelHistory::addNewChannelMessage(const MTPMessage &msg, NewMessageType type) {
	if (type == NewMessageExisting) return addToHistory(msg);

	return addNewToBlocks(msg, type);
}

HistoryItem *ChannelHistory::addNewToBlocks(const MTPMessage &msg, NewMessageType type) {
	if (!loadedAtBottom()) {
		HistoryItem *item = addToHistory(msg);
		if (item) {
			setLastMessage(item);
			if (type == NewMessageUnread) {
				newItemAdded(item);
			}
		}
		return item;
	}

	return addNewToLastBlock(msg, type);
}

void ChannelHistory::cleared(bool leaveItems) {
	_joinedMessage = nullptr;
}

HistoryItem *ChannelHistory::findPrevItem(HistoryItem *item) const {
	if (item->detached()) return nullptr;

	int itemIndex = item->indexInBlock();
	int blockIndex = item->block()->indexInHistory();
	for (++blockIndex, ++itemIndex; blockIndex > 0;) {
		--blockIndex;
		HistoryBlock *block = blocks.at(blockIndex);
		if (!itemIndex) itemIndex = block->items.size();
		for (; itemIndex > 0;) {
			--itemIndex;
			if (block->items.at(itemIndex)->type() == HistoryItemMsg) {
				return block->items.at(itemIndex);
			}
		}
	}
	return nullptr;
}

void ChannelHistory::messageDetached(HistoryItem *msg) {
	if (_joinedMessage == msg) {
		_joinedMessage = nullptr;
	}
}

ChannelHistory::~ChannelHistory() {
	// all items must be destroyed before ChannelHistory is destroyed
	// or they will call history()->asChannelHistory() -> undefined behaviour
	clearOnDestroy();
}

History *Histories::find(const PeerId &peerId) {
	Map::const_iterator i = map.constFind(peerId);
	return (i == map.cend()) ? 0 : i.value();
}

History *Histories::findOrInsert(const PeerId &peerId, int32 unreadCount, int32 maxInboxRead, int32 maxOutboxRead) {
	auto i = map.constFind(peerId);
	if (i == map.cend()) {
		auto history = peerIsChannel(peerId) ? static_cast<History*>(new ChannelHistory(peerId)) : (new History(peerId));
		i = map.insert(peerId, history);
		history->setUnreadCount(unreadCount);
		history->inboxReadBefore = maxInboxRead + 1;
		history->outboxReadBefore = maxOutboxRead + 1;
	} else {
		auto history = i.value();
		if (unreadCount >= history->unreadCount()) {
			history->setUnreadCount(unreadCount);
			history->inboxReadBefore = maxInboxRead + 1;
		}
		accumulate_max(history->outboxReadBefore, maxOutboxRead + 1);
	}
	return i.value();
}

void Histories::clear() {
	App::historyClearMsgs();

	Map temp;
	std::swap(temp, map);
	for_const (auto history, temp) {
		delete history;
	}

	_unreadFull = _unreadMuted = 0;
	Notify::unreadCounterUpdated();
	App::historyClearItems();
	typing.clear();
}

void Histories::regSendAction(History *history, UserData *user, const MTPSendMessageAction &action) {
	if (action.type() == mtpc_sendMessageCancelAction) {
		history->unregTyping(user);
		return;
	}

	uint64 ms = getms();
	switch (action.type()) {
	case mtpc_sendMessageTypingAction: history->typing[user] = ms + 6000; break;
	case mtpc_sendMessageRecordVideoAction: history->sendActions.insert(user, SendAction(SendActionRecordVideo, ms + 6000)); break;
	case mtpc_sendMessageUploadVideoAction: history->sendActions.insert(user, SendAction(SendActionUploadVideo, ms + 6000, action.c_sendMessageUploadVideoAction().vprogress.v)); break;
	case mtpc_sendMessageRecordAudioAction: history->sendActions.insert(user, SendAction(SendActionRecordVoice, ms + 6000)); break;
	case mtpc_sendMessageUploadAudioAction: history->sendActions.insert(user, SendAction(SendActionUploadVoice, ms + 6000, action.c_sendMessageUploadAudioAction().vprogress.v)); break;
	case mtpc_sendMessageUploadPhotoAction: history->sendActions.insert(user, SendAction(SendActionUploadPhoto, ms + 6000, action.c_sendMessageUploadPhotoAction().vprogress.v)); break;
	case mtpc_sendMessageUploadDocumentAction: history->sendActions.insert(user, SendAction(SendActionUploadFile, ms + 6000, action.c_sendMessageUploadDocumentAction().vprogress.v)); break;
	case mtpc_sendMessageGeoLocationAction: history->sendActions.insert(user, SendAction(SendActionChooseLocation, ms + 6000)); break;
	case mtpc_sendMessageChooseContactAction: history->sendActions.insert(user, SendAction(SendActionChooseContact, ms + 6000)); break;
	default: return;
	}

	user->madeAction();

	TypingHistories::const_iterator i = typing.find(history);
	if (i == typing.cend()) {
		typing.insert(history, ms);
		history->typingDots = 0;
		_a_typings.start();
	}
	history->updateTyping(ms, true);
}

void Histories::step_typings(uint64 ms, bool timer) {
	for (TypingHistories::iterator i = typing.begin(), e = typing.end(); i != e;) {
		i.key()->typingDots = (ms - i.value()) / 150;
		i.key()->updateTyping(ms);
		if (i.key()->typing.isEmpty() && i.key()->sendActions.isEmpty()) {
			i = typing.erase(i);
		} else {
			++i;
		}
	}
	if (typing.isEmpty()) {
		_a_typings.stop();
	}
}

void Histories::remove(const PeerId &peer) {
	Map::iterator i = map.find(peer);
	if (i != map.cend()) {
		typing.remove(i.value());
		delete i.value();
		map.erase(i);
	}
}

namespace {

void checkForSwitchInlineButton(HistoryItem *item) {
	if (item->out() || !item->hasSwitchInlineButton()) {
		return;
	}
	if (UserData *user = item->history()->peer->asUser()) {
		if (!user->botInfo || !user->botInfo->inlineReturnPeerId) {
			return;
		}
		if (auto markup = item->Get<HistoryMessageReplyMarkup>()) {
			for_const (const auto &row, markup->rows) {
				for_const (const auto &button, row) {
					if (button.type == HistoryMessageReplyMarkup::Button::SwitchInline) {
						Notify::switchInlineBotButtonReceived(QString::fromUtf8(button.data));
						return;
					}
				}
			}
		}
	}
}

} // namespace

HistoryItem *Histories::addNewMessage(const MTPMessage &msg, NewMessageType type) {
	PeerId peer = peerFromMessage(msg);
	if (!peer) return nullptr;

	HistoryItem *result = App::history(peer)->addNewMessage(msg, type);
	if (result && type == NewMessageUnread) {
		checkForSwitchInlineButton(result);
	}
	return result;
}

HistoryItem *History::createItem(const MTPMessage &msg, bool applyServiceAction, bool detachExistingItem) {
	MsgId msgId = 0;
	switch (msg.type()) {
	case mtpc_messageEmpty: msgId = msg.c_messageEmpty().vid.v; break;
	case mtpc_message: msgId = msg.c_message().vid.v; break;
	case mtpc_messageService: msgId = msg.c_messageService().vid.v; break;
	}
	if (!msgId) return nullptr;

	HistoryItem *result = App::histItemById(channelId(), msgId);
	if (result) {
		if (!result->detached() && detachExistingItem) {
			result->detach();
		}
		if (msg.type() == mtpc_message) {
			result->updateMedia(msg.c_message().has_media() ? (&msg.c_message().vmedia) : 0);
			if (applyServiceAction) {
				App::checkSavedGif(result);
			}
		}
		return result;
	}

	switch (msg.type()) {
	case mtpc_messageEmpty:
		result = HistoryService::create(this, msg.c_messageEmpty().vid.v, date(), lang(lng_message_empty));
	break;

	case mtpc_message: {
		const auto &m(msg.c_message());
		int badMedia = 0; // 1 - unsupported, 2 - empty
		if (m.has_media()) switch (m.vmedia.type()) {
		case mtpc_messageMediaEmpty:
		case mtpc_messageMediaContact: break;
		case mtpc_messageMediaGeo:
			switch (m.vmedia.c_messageMediaGeo().vgeo.type()) {
			case mtpc_geoPoint: break;
			case mtpc_geoPointEmpty: badMedia = 2; break;
			default: badMedia = 1; break;
			}
			break;
		case mtpc_messageMediaVenue:
			switch (m.vmedia.c_messageMediaVenue().vgeo.type()) {
			case mtpc_geoPoint: break;
			case mtpc_geoPointEmpty: badMedia = 2; break;
			default: badMedia = 1; break;
			}
			break;
		case mtpc_messageMediaPhoto:
			switch (m.vmedia.c_messageMediaPhoto().vphoto.type()) {
			case mtpc_photo: break;
			case mtpc_photoEmpty: badMedia = 2; break;
			default: badMedia = 1; break;
			}
			break;
		case mtpc_messageMediaDocument:
			switch (m.vmedia.c_messageMediaDocument().vdocument.type()) {
			case mtpc_document: break;
			case mtpc_documentEmpty: badMedia = 2; break;
			default: badMedia = 1; break;
			}
			break;
		case mtpc_messageMediaWebPage:
			switch (m.vmedia.c_messageMediaWebPage().vwebpage.type()) {
			case mtpc_webPage:
			case mtpc_webPageEmpty:
			case mtpc_webPagePending: break;
			default: badMedia = 1; break;
			}
			break;
		case mtpc_messageMediaUnsupported:
		default: badMedia = 1; break;
		}
		if (badMedia == 1) {
			QString text(lng_message_unsupported(lt_link, qsl("https://desktop.telegram.org")));
			EntitiesInText entities;
			textParseEntities(text, _historyTextNoMonoOptions.flags, &entities);
			entities.push_front(EntityInText(EntityInTextItalic, 0, text.size()));
			result = HistoryMessage::create(this, m.vid.v, m.vflags.v, m.vreply_to_msg_id.v, m.vvia_bot_id.v, date(m.vdate), m.vfrom_id.v, { text, entities });
		} else if (badMedia) {
			result = HistoryService::create(this, m.vid.v, date(m.vdate), lang(lng_message_empty), m.vflags.v, m.has_from_id() ? m.vfrom_id.v : 0);
		} else {
			result = HistoryMessage::create(this, m);
		}
	} break;

	case mtpc_messageService: {
		const auto &d(msg.c_messageService());
		result = HistoryService::create(this, d);

		if (applyServiceAction) {
			const auto &action(d.vaction);
			switch (d.vaction.type()) {
			case mtpc_messageActionChatAddUser: {
				const auto &d(action.c_messageActionChatAddUser());
				if (peer->isMegagroup()) {
					const auto &v(d.vusers.c_vector().v);
					for (int32 i = 0, l = v.size(); i < l; ++i) {
						if (UserData *user = App::userLoaded(peerFromUser(v.at(i)))) {
							if (peer->asChannel()->mgInfo->lastParticipants.indexOf(user) < 0) {
								peer->asChannel()->mgInfo->lastParticipants.push_front(user);
								peer->asChannel()->mgInfo->lastParticipantsStatus |= MegagroupInfo::LastParticipantsAdminsOutdated;
								Notify::peerUpdatedDelayed(peer, Notify::PeerUpdate::Flag::MembersChanged);
							}
							if (user->botInfo) {
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
				const auto &d(action.c_messageActionChatJoinedByLink());
				if (peer->isMegagroup()) {
					if (result->from()->isUser()) {
						if (peer->asChannel()->mgInfo->lastParticipants.indexOf(result->from()->asUser()) < 0) {
							peer->asChannel()->mgInfo->lastParticipants.push_front(result->from()->asUser());
							Notify::peerUpdatedDelayed(peer, Notify::PeerUpdate::Flag::MembersChanged);
						}
						if (result->from()->asUser()->botInfo) {
							peer->asChannel()->mgInfo->bots.insert(result->from()->asUser());
							if (peer->asChannel()->mgInfo->botStatus != 0 && peer->asChannel()->mgInfo->botStatus < 2) {
								peer->asChannel()->mgInfo->botStatus = 2;
							}
						}
					}
				}
			} break;

			case mtpc_messageActionChatDeletePhoto: {
				ChatData *chat = peer->asChat();
				if (chat) chat->setPhoto(MTP_chatPhotoEmpty());
			} break;

			case mtpc_messageActionChatDeleteUser: {
				const auto &d(action.c_messageActionChatDeleteUser());
				PeerId uid = peerFromUser(d.vuser_id);
				if (lastKeyboardFrom == uid) {
					clearLastKeyboard();
					if (App::main()) App::main()->updateBotKeyboard(this);
				}
				if (peer->isMegagroup()) {
					if (auto user = App::userLoaded(uid)) {
						auto channel = peer->asChannel();
						auto megagroupInfo = channel->mgInfo;

						int32 index = megagroupInfo->lastParticipants.indexOf(user);
						if (index >= 0) {
							megagroupInfo->lastParticipants.removeAt(index);
							Notify::peerUpdatedDelayed(peer, Notify::PeerUpdate::Flag::MembersChanged);
						}
						if (peer->asChannel()->membersCount() > 1) {
							peer->asChannel()->setMembersCount(channel->membersCount() - 1);
						} else {
							megagroupInfo->lastParticipantsStatus |= MegagroupInfo::LastParticipantsCountOutdated;
							megagroupInfo->lastParticipantsCount = 0;
						}
						if (megagroupInfo->lastAdmins.contains(user)) {
							megagroupInfo->lastAdmins.remove(user);
							if (channel->adminsCount() > 1) {
								channel->setAdminsCount(channel->adminsCount() - 1);
							}
							Notify::peerUpdatedDelayed(peer, Notify::PeerUpdate::Flag::AdminsChanged);
						}
						megagroupInfo->bots.remove(user);
						if (megagroupInfo->bots.isEmpty() && megagroupInfo->botStatus > 0) {
							megagroupInfo->botStatus = -1;
						}
					}
				}
			} break;

			case mtpc_messageActionChatEditPhoto: {
				const auto &d(action.c_messageActionChatEditPhoto());
				if (d.vphoto.type() == mtpc_photo) {
					const auto &sizes(d.vphoto.c_photo().vsizes.c_vector().v);
					if (!sizes.isEmpty()) {
						PhotoData *photo = App::feedPhoto(d.vphoto.c_photo());
						if (photo) photo->peer = peer;
						const auto &smallSize(sizes.front()), &bigSize(sizes.back());
						const MTPFileLocation *smallLoc = 0, *bigLoc = 0;
						switch (smallSize.type()) {
						case mtpc_photoSize: smallLoc = &smallSize.c_photoSize().vlocation; break;
						case mtpc_photoCachedSize: smallLoc = &smallSize.c_photoCachedSize().vlocation; break;
						}
						switch (bigSize.type()) {
						case mtpc_photoSize: bigLoc = &bigSize.c_photoSize().vlocation; break;
						case mtpc_photoCachedSize: bigLoc = &bigSize.c_photoCachedSize().vlocation; break;
						}
						if (smallLoc && bigLoc) {
							if (peer->isChat()) {
								peer->asChat()->setPhoto(MTP_chatPhoto(*smallLoc, *bigLoc), photo ? photo->id : 0);
							} else if (peer->isChannel()) {
								peer->asChannel()->setPhoto(MTP_chatPhoto(*smallLoc, *bigLoc), photo ? photo->id : 0);
							}
							peer->loadUserpic();
						}
					}
				}
			} break;

			case mtpc_messageActionChatEditTitle: {
				auto &d(action.c_messageActionChatEditTitle());
				if (auto chat = peer->asChat()) {
					chat->setName(qs(d.vtitle));
				}
			} break;

			case mtpc_messageActionChatMigrateTo: {
				peer->asChat()->flags |= MTPDchat::Flag::f_deactivated;

				//const auto &d(action.c_messageActionChatMigrateTo());
				//PeerData *channel = App::channelLoaded(d.vchannel_id.v);
			} break;

			case mtpc_messageActionChannelMigrateFrom: {
				//const auto &d(action.c_messageActionChannelMigrateFrom());
				//PeerData *chat = App::chatLoaded(d.vchat_id.v);
			} break;

			case mtpc_messageActionPinMessage: {
				if (d.has_reply_to_msg_id() && result && result->history()->peer->isMegagroup()) {
					result->history()->peer->asChannel()->mgInfo->pinnedMsgId = d.vreply_to_msg_id.v;
					if (App::main()) emit App::main()->peerUpdated(result->history()->peer);
				}
			} break;
			}
		}
	} break;
	}

	if (applyServiceAction) {
		App::checkSavedGif(result);
	}

	return result;
}

HistoryItem *History::createItemForwarded(MsgId id, MTPDmessage::Flags flags, QDateTime date, int32 from, HistoryMessage *msg) {
	return HistoryMessage::create(this, id, flags, date, from, msg);
}

HistoryItem *History::createItemDocument(MsgId id, MTPDmessage::Flags flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, DocumentData *doc, const QString &caption, const MTPReplyMarkup &markup) {
	return HistoryMessage::create(this, id, flags, replyTo, viaBotId, date, from, doc, caption, markup);
}

HistoryItem *History::createItemPhoto(MsgId id, MTPDmessage::Flags flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, PhotoData *photo, const QString &caption, const MTPReplyMarkup &markup) {
	return HistoryMessage::create(this, id, flags, replyTo, viaBotId, date, from, photo, caption, markup);
}

HistoryItem *History::addNewService(MsgId msgId, QDateTime date, const QString &text, MTPDmessage::Flags flags, bool newMsg) {
	return addNewItem(HistoryService::create(this, msgId, date, text, flags), newMsg);
}

HistoryItem *History::addNewMessage(const MTPMessage &msg, NewMessageType type) {
	if (isChannel()) return asChannelHistory()->addNewChannelMessage(msg, type);

	if (type == NewMessageExisting) return addToHistory(msg);
	if (!loadedAtBottom() || peer->migrateTo()) {
		HistoryItem *item = addToHistory(msg);
		if (item) {
			setLastMessage(item);
			if (type == NewMessageUnread) {
				newItemAdded(item);
			}
		}
		return item;
	}

	return addNewToLastBlock(msg, type);
}

HistoryItem *History::addNewToLastBlock(const MTPMessage &msg, NewMessageType type) {
	bool applyServiceAction = (type == NewMessageUnread), detachExistingItem = (type != NewMessageLast);
	HistoryItem *item = createItem(msg, applyServiceAction, detachExistingItem);
	if (!item || !item->detached()) {
		return item;
	}
	return addNewItem(item, (type == NewMessageUnread));
}

HistoryItem *History::addToHistory(const MTPMessage &msg) {
	return createItem(msg, false, false);
}

HistoryItem *History::addNewForwarded(MsgId id, MTPDmessage::Flags flags, QDateTime date, int32 from, HistoryMessage *item) {
	return addNewItem(createItemForwarded(id, flags, date, from, item), true);
}

HistoryItem *History::addNewDocument(MsgId id, MTPDmessage::Flags flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, DocumentData *doc, const QString &caption, const MTPReplyMarkup &markup) {
	return addNewItem(createItemDocument(id, flags, viaBotId, replyTo, date, from, doc, caption, markup), true);
}

HistoryItem *History::addNewPhoto(MsgId id, MTPDmessage::Flags flags, int32 viaBotId, MsgId replyTo, QDateTime date, int32 from, PhotoData *photo, const QString &caption, const MTPReplyMarkup &markup) {
	return addNewItem(createItemPhoto(id, flags, viaBotId, replyTo, date, from, photo, caption, markup), true);
}

bool History::addToOverview(MediaOverviewType type, MsgId msgId, AddToOverviewMethod method) {
	bool adding = false;
	switch (method) {
	case AddToOverviewNew:
	case AddToOverviewFront: adding = (overviewIds[type].constFind(msgId) == overviewIds[type].cend()); break;
	case AddToOverviewBack: adding = (overviewCountData[type] != 0); break;
	}
	if (!adding) return false;

	overviewIds[type].insert(msgId, NullType());
	switch (method) {
	case AddToOverviewNew:
	case AddToOverviewBack: overview[type].push_back(msgId); break;
	case AddToOverviewFront: overview[type].push_front(msgId); break;
	}
	if (method == AddToOverviewNew) {
		if (overviewCountData[type] > 0) {
			++overviewCountData[type];
		}
		if (App::wnd()) App::wnd()->mediaOverviewUpdated(peer, type);
	}
	return true;
}

void History::eraseFromOverview(MediaOverviewType type, MsgId msgId) {
	if (overviewIds[type].isEmpty()) return;

	History::MediaOverviewIds::iterator i = overviewIds[type].find(msgId);
	if (i == overviewIds[type].cend()) return;

	overviewIds[type].erase(i);
	for (History::MediaOverview::iterator i = overview[type].begin(), e = overview[type].end(); i != e; ++i) {
		if ((*i) == msgId) {
			overview[type].erase(i);
			if (overviewCountData[type] > 0) {
				--overviewCountData[type];
			}
			break;
		}
	}
	if (App::wnd()) App::wnd()->mediaOverviewUpdated(peer, type);
}

HistoryItem *History::addNewItem(HistoryItem *adding, bool newMsg) {
	t_assert(!isBuildingFrontBlock());
	addItemToBlock(adding);

	setLastMessage(adding);
	if (newMsg) {
		newItemAdded(adding);
	}

	adding->addToOverview(AddToOverviewNew);
	if (adding->from()->id) {
		if (adding->from()->isUser()) {
			QList<UserData*> *lastAuthors = 0;
			if (peer->isChat()) {
				lastAuthors = &peer->asChat()->lastAuthors;
			} else if (peer->isMegagroup()) {
				lastAuthors = &peer->asChannel()->mgInfo->lastParticipants;
				if (adding->from()->asUser()->botInfo) {
					peer->asChannel()->mgInfo->bots.insert(adding->from()->asUser());
					if (peer->asChannel()->mgInfo->botStatus != 0 && peer->asChannel()->mgInfo->botStatus < 2) {
						peer->asChannel()->mgInfo->botStatus = 2;
					}
				}
			}
			if (lastAuthors) {
				int prev = lastAuthors->indexOf(adding->from()->asUser());
				if (prev > 0) {
					lastAuthors->removeAt(prev);
				} else if (prev < 0 && peer->isMegagroup()) { // nothing is outdated if just reordering
					peer->asChannel()->mgInfo->lastParticipantsStatus |= MegagroupInfo::LastParticipantsAdminsOutdated;
				}
				if (prev) {
					lastAuthors->push_front(adding->from()->asUser());
				}
				if (peer->isMegagroup()) {
					Notify::peerUpdatedDelayed(peer, Notify::PeerUpdate::Flag::MembersChanged);
				}
			}
		}
		if (adding->definesReplyKeyboard()) {
			MTPDreplyKeyboardMarkup::Flags markupFlags = adding->replyKeyboardFlags();
			if (!(markupFlags & MTPDreplyKeyboardMarkup::Flag::f_selective) || adding->mentionsMe()) {
				OrderedSet<PeerData*> *markupSenders = 0;
				if (peer->isChat()) {
					markupSenders = &peer->asChat()->markupSenders;
				} else if (peer->isMegagroup()) {
					markupSenders = &peer->asChannel()->mgInfo->markupSenders;
				}
				if (markupSenders) {
					markupSenders->insert(adding->from());
				}
				if (markupFlags & MTPDreplyKeyboardMarkup_ClientFlag::f_zero) { // zero markup means replyKeyboardHide
					if (lastKeyboardFrom == adding->from()->id || (!lastKeyboardInited && !peer->isChat() && !peer->isMegagroup() && !adding->out())) {
						clearLastKeyboard();
					}
				} else {
					bool botNotInChat = false;
					if (peer->isChat()) {
						botNotInChat = adding->from()->isUser() && (!peer->canWrite() || !peer->asChat()->participants.isEmpty()) && !peer->asChat()->participants.contains(adding->from()->asUser());
					} else if (peer->isMegagroup()) {
						botNotInChat = adding->from()->isUser() && (!peer->canWrite() || peer->asChannel()->mgInfo->botStatus != 0) && !peer->asChannel()->mgInfo->bots.contains(adding->from()->asUser());
					}
					if (botNotInChat) {
						clearLastKeyboard();
					} else {
						lastKeyboardInited = true;
						lastKeyboardId = adding->id;
						lastKeyboardFrom = adding->from()->id;
						lastKeyboardUsed = false;
					}
				}
			}
		}
	}

	return adding;
}

void History::unregTyping(UserData *from) {
	uint64 updateAtMs = 0;
	TypingUsers::iterator i = typing.find(from);
	if (i != typing.end()) {
		updateAtMs = getms();
		i.value() = updateAtMs;
	}
	SendActionUsers::iterator j = sendActions.find(from);
	if (j != sendActions.end()) {
		if (!updateAtMs) updateAtMs = getms();
		j.value().until = updateAtMs;
	}
	if (updateAtMs) {
		updateTyping(updateAtMs, true);
	}
}

void History::newItemAdded(HistoryItem *item) {
	App::checkImageCacheSize();
	if (item->from() && item->from()->isUser()) {
		if (item->from() == item->author()) {
			unregTyping(item->from()->asUser());
		}
		item->from()->asUser()->madeAction();
	}
	if (item->out()) {
		if (unreadBar) unreadBar->destroyUnreadBar();
		if (!item->unread()) {
			outboxRead(item);
		}
	} else if (item->unread()) {
		bool skip = false;
		if (!isChannel() || peer->asChannel()->amIn()) {
			notifies.push_back(item);
			App::main()->newUnreadMsg(this, item);
		}
	} else if (!item->isGroupMigrate() || !peer->isMegagroup()) {
		inboxRead(item);
	}
}

HistoryBlock *History::prepareBlockForAddingItem() {
	if (isBuildingFrontBlock()) {
		if (_buildingFrontBlock->block) {
			return _buildingFrontBlock->block;
		}

		HistoryBlock *result = _buildingFrontBlock->block = new HistoryBlock(this);
		if (_buildingFrontBlock->expectedItemsCount > 0) {
			result->items.reserve(_buildingFrontBlock->expectedItemsCount + 1);
		}
		result->setIndexInHistory(0);
		blocks.push_front(result);
		for (int i = 1, l = blocks.size(); i < l; ++i) {
			blocks.at(i)->setIndexInHistory(i);
		}
		return result;
	}

	bool addNewBlock = blocks.isEmpty() || (blocks.back()->items.size() >= MessagesPerPage);
	if (!addNewBlock) {
		return blocks.back();
	}

	HistoryBlock *result = new HistoryBlock(this);
	result->setIndexInHistory(blocks.size());
	blocks.push_back(result);

	result->items.reserve(MessagesPerPage);
	return result;
};

void History::addItemToBlock(HistoryItem *item) {
	t_assert(item != nullptr);
	t_assert(item->detached());

	HistoryBlock *block = prepareBlockForAddingItem();

	item->attachToBlock(block, block->items.size());
	block->items.push_back(item);
	item->previousItemChanged();

	if (isBuildingFrontBlock() && _buildingFrontBlock->expectedItemsCount > 0) {
		--_buildingFrontBlock->expectedItemsCount;
	}
}

void History::addOlderSlice(const QVector<MTPMessage> &slice) {
	if (slice.isEmpty()) {
		oldLoaded = true;
		if (isChannel()) {
			asChannelHistory()->checkJoinedMessage();
			asChannelHistory()->checkMaxReadMessageDate();
		}
		return;
	}

	startBuildingFrontBlock(slice.size());

	for (auto i = slice.cend(), e = slice.cbegin(); i != e;) {
		--i;
		HistoryItem *adding = createItem(*i, false, true);
		if (!adding) continue;

		addItemToBlock(adding);
	}

	HistoryBlock *block = finishBuildingFrontBlock();
	if (!block) {
		// If no items were added it means we've loaded everything old.
		oldLoaded = true;
	} else if (loadedAtBottom()) { // add photos to overview and authors to lastAuthors / lastParticipants
		bool channel = isChannel();
		int32 mask = 0;
		QList<UserData*> *lastAuthors = 0;
		OrderedSet<PeerData*> *markupSenders = 0;
		if (peer->isChat()) {
			lastAuthors = &peer->asChat()->lastAuthors;
			markupSenders = &peer->asChat()->markupSenders;
		} else if (peer->isMegagroup()) {
			lastAuthors = &peer->asChannel()->mgInfo->lastParticipants;
			markupSenders = &peer->asChannel()->mgInfo->markupSenders;
		}
		for (int32 i = block->items.size(); i > 0; --i) {
			HistoryItem *item = block->items[i - 1];
			mask |= item->addToOverview(AddToOverviewFront);
			if (item->from()->id) {
				if (lastAuthors) { // chats
					if (item->from()->isUser()) {
						if (!lastAuthors->contains(item->from()->asUser())) {
							lastAuthors->push_back(item->from()->asUser());
							if (peer->isMegagroup()) {
								peer->asChannel()->mgInfo->lastParticipantsStatus |= MegagroupInfo::LastParticipantsAdminsOutdated;
								Notify::peerUpdatedDelayed(peer, Notify::PeerUpdate::Flag::MembersChanged);
							}
						}
					}
				}
			}
			if (item->author()->id) {
				if (markupSenders) { // chats with bots
					if (!lastKeyboardInited && item->definesReplyKeyboard() && !item->out()) {
						MTPDreplyKeyboardMarkup::Flags markupFlags = item->replyKeyboardFlags();
						if (!(markupFlags & MTPDreplyKeyboardMarkup::Flag::f_selective) || item->mentionsMe()) {
							bool wasKeyboardHide = markupSenders->contains(item->author());
							if (!wasKeyboardHide) {
								markupSenders->insert(item->author());
							}
							if (!(markupFlags & MTPDreplyKeyboardMarkup_ClientFlag::f_zero)) {
								if (!lastKeyboardInited) {
									bool botNotInChat = false;
									if (peer->isChat()) {
										botNotInChat = (!peer->canWrite() || !peer->asChat()->participants.isEmpty()) && item->author()->isUser() && !peer->asChat()->participants.contains(item->author()->asUser());
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
		for (int32 t = 0; t < OverviewCount; ++t) {
			if ((mask & (1 << t)) && App::wnd()) App::wnd()->mediaOverviewUpdated(peer, MediaOverviewType(t));
		}
	}

	if (isChannel()) {
		asChannelHistory()->checkJoinedMessage();
		asChannelHistory()->checkMaxReadMessageDate();
	}
	checkLastMsg();
}

void History::addNewerSlice(const QVector<MTPMessage> &slice) {
	bool wasEmpty = isEmpty(), wasLoadedAtBottom = loadedAtBottom();

	if (slice.isEmpty()) {
		newLoaded = true;
		if (!lastMsg) {
			setLastMessage(lastImportantMessage());
		}
	}

	t_assert(!isBuildingFrontBlock());
	if (!slice.isEmpty()) {
		bool atLeastOneAdded = false;
		for (auto i = slice.cend(), e = slice.cbegin(); i != e;) {
			--i;
			HistoryItem *adding = createItem(*i, false, true);
			if (!adding) continue;

			addItemToBlock(adding);
			atLeastOneAdded = true;
		}

		if (!atLeastOneAdded) {
			newLoaded = true;
			setLastMessage(lastImportantMessage());
		}
	}

	if (!wasLoadedAtBottom && loadedAtBottom()) { // add all loaded photos to overview
		int32 mask = 0;
		for (int32 i = 0; i < OverviewCount; ++i) {
			if (overviewCountData[i] == 0) continue; // all loaded
			if (!overview[i].isEmpty() || !overviewIds[i].isEmpty()) {
				overview[i].clear();
				overviewIds[i].clear();
				mask |= (1 << i);
			}
		}
		for_const (HistoryBlock *block, blocks) {
			for_const (HistoryItem *item, block->items) {
				mask |= item->addToOverview(AddToOverviewBack);
			}
		}
		for (int32 t = 0; t < OverviewCount; ++t) {
			if ((mask & (1 << t)) && App::wnd()) App::wnd()->mediaOverviewUpdated(peer, MediaOverviewType(t));
		}
	}

	if (isChannel()) asChannelHistory()->checkJoinedMessage();
	checkLastMsg();
}

void History::checkLastMsg() {
	if (lastMsg) {
		if (!newLoaded && !lastMsg->detached()) {
			newLoaded = true;
		}
	} else if (newLoaded) {
		setLastMessage(lastImportantMessage());
	}
}

int History::countUnread(MsgId upTo) {
	int result = 0;
	for (auto i = blocks.cend(), e = blocks.cbegin(); i != e;) {
		--i;
		for (auto j = (*i)->items.cend(), en = (*i)->items.cbegin(); j != en;) {
			--j;
			if ((*j)->id > 0 && (*j)->id <= upTo) {
				break;
			} else if (!(*j)->out() && (*j)->unread() && (*j)->id > upTo) {
				++result;
			}
		}
	}
	return result;
}

void History::updateShowFrom() {
	if (showFrom) return;

	for (auto i = blocks.cend(); i != blocks.cbegin();) {
		--i;
		for (auto j = (*i)->items.cend(); j != (*i)->items.cbegin();) {
			--j;
			if ((*j)->type() == HistoryItemMsg && (*j)->id > 0 && (!(*j)->out() || !showFrom)) {
				if ((*j)->id >= inboxReadBefore) {
					showFrom = *j;
				} else {
					return;
				}
			}
		}
	}
}

MsgId History::inboxRead(MsgId upTo) {
	if (upTo < 0) return upTo;
	if (unreadCount()) {
		if (upTo && loadedAtBottom()) App::main()->historyToDown(this);
		setUnreadCount(upTo ? countUnread(upTo) : 0);
	}

	if (!upTo) upTo = msgIdForRead();
	accumulate_max(inboxReadBefore, upTo + 1);

	updateChatListEntry();
	if (peer->migrateTo()) {
		if (History *h = App::historyLoaded(peer->migrateTo()->id)) {
			h->updateChatListEntry();
		}
	}

	showFrom = nullptr;
	App::wnd()->notifyClear(this);
	clearNotifications();

	return upTo;
}

MsgId History::inboxRead(HistoryItem *wasRead) {
	return inboxRead(wasRead ? wasRead->id : 0);
}

MsgId History::outboxRead(int32 upTo) {
	if (upTo < 0) return upTo;
	if (!upTo) upTo = msgIdForRead();
	accumulate_max(outboxReadBefore, upTo + 1);

	return upTo;
}

MsgId History::outboxRead(HistoryItem *wasRead) {
	return outboxRead(wasRead ? wasRead->id : 0);
}

HistoryItem *History::lastImportantMessage() const {
	if (isEmpty()) {
		return nullptr;
	}
	bool importantOnly = isChannel() && !isMegagroup();
	for (int blockIndex = blocks.size(); blockIndex > 0;) {
		HistoryBlock *block = blocks.at(--blockIndex);
		for (int itemIndex = block->items.size(); itemIndex > 0;) {
			HistoryItem *item = block->items.at(--itemIndex);
			if (item->type() == HistoryItemMsg) {
				return item;
			}
		}
	}
	return nullptr;
}

void History::setUnreadCount(int newUnreadCount) {
	if (_unreadCount != newUnreadCount) {
		if (newUnreadCount == 1) {
			if (loadedAtBottom()) showFrom = lastImportantMessage();
			inboxReadBefore = qMax(inboxReadBefore, msgIdForRead());
		} else if (!newUnreadCount) {
			showFrom = nullptr;
			inboxReadBefore = qMax(inboxReadBefore, msgIdForRead() + 1);
		} else {
			if (!showFrom && !unreadBar && loadedAtBottom()) updateShowFrom();
		}
		if (inChatList(Dialogs::Mode::All)) {
			App::histories().unreadIncrement(newUnreadCount - _unreadCount, mute());
			if (!mute() || cIncludeMuted()) {
				Notify::unreadCounterUpdated();
			}
		}
		_unreadCount = newUnreadCount;
		if (auto main = App::main()) {
			main->unreadCountChanged(this);
		}
		if (unreadBar) {
			int32 count = _unreadCount;
			if (peer->migrateTo()) {
				if (History *h = App::historyLoaded(peer->migrateTo()->id)) {
					count += h->unreadCount();
				}
			}
			if (count > 0) {
				unreadBar->setUnreadBarCount(count);
			} else {
				unreadBar->setUnreadBarFreezed();
			}
		}
	}
}

 void History::setMute(bool newMute) {
	if (_mute != newMute) {
		_mute = newMute;
		if (inChatList(Dialogs::Mode::All)) {
			if (_unreadCount) {
				App::histories().unreadMuteChanged(_unreadCount, newMute);
				Notify::unreadCounterUpdated();
			}
			Notify::historyMuteUpdated(this);
		}
		updateChatListEntry();
	}
}

void History::getNextShowFrom(HistoryBlock *block, int i) {
	if (i >= 0) {
		int l = block->items.size();
		for (++i; i < l; ++i) {
			if (block->items.at(i)->type() == HistoryItemMsg) {
				showFrom = block->items.at(i);
				return;
			}
		}
	}

	for (int j = block->indexInHistory() + 1, s = blocks.size(); j < s; ++j) {
		block = blocks.at(j);
		for_const (HistoryItem *item, block->items) {
			if (item->type() == HistoryItemMsg) {
				showFrom = item;
				return;
			}
		}
	}
	showFrom = nullptr;
}

void History::countScrollState(int top) {
	countScrollTopItem(top);
	if (scrollTopItem) {
		scrollTopOffset = (top - scrollTopItem->block()->y - scrollTopItem->y);
	}
}

void History::countScrollTopItem(int top) {
	if (isEmpty()) {
		forgetScrollState();
		return;
	}

	int itemIndex = 0, blockIndex = 0, itemTop = 0;
	if (scrollTopItem && !scrollTopItem->detached()) {
		itemIndex = scrollTopItem->indexInBlock();
		blockIndex = scrollTopItem->block()->indexInHistory();
		itemTop = blocks.at(blockIndex)->y + scrollTopItem->y;
	}
	if (itemTop > top) {
		// go backward through history while we don't find an item that starts above
		do {
			HistoryBlock *block = blocks.at(blockIndex);
			for (--itemIndex; itemIndex >= 0; --itemIndex) {
				HistoryItem *item = block->items.at(itemIndex);
				itemTop = block->y + item->y;
				if (itemTop <= top) {
					scrollTopItem = item;
					return;
				}
			}
			if (--blockIndex >= 0) {
				itemIndex = blocks.at(blockIndex)->items.size();
			} else {
				break;
			}
		} while (true);

		scrollTopItem = blocks.front()->items.front();
	} else {
		// go forward through history while we don't find the last item that starts above
		for (int blocksCount = blocks.size(); blockIndex < blocksCount; ++blockIndex) {
			HistoryBlock *block = blocks.at(blockIndex);
			for (int itemsCount = block->items.size(); itemIndex < itemsCount; ++itemIndex) {
				HistoryItem *item = block->items.at(itemIndex);
				itemTop = block->y + item->y;
				if (itemTop > top) {
					t_assert(itemIndex > 0 || blockIndex > 0);
					if (itemIndex > 0) {
						scrollTopItem = block->items.at(itemIndex - 1);
					} else {
						scrollTopItem = blocks.at(blockIndex - 1)->items.back();
					}
					return;
				}
			}
			itemIndex = 0;
		}
		scrollTopItem = blocks.back()->items.back();
	}
}

void History::getNextScrollTopItem(HistoryBlock *block, int32 i) {
	++i;
	if (i > 0 && i < block->items.size()) {
		scrollTopItem = block->items.at(i);
		return;
	}
	int j = block->indexInHistory() + 1;
	if (j > 0 && j < blocks.size()) {
		scrollTopItem = blocks.at(j)->items.front();
		return;
	}
	scrollTopItem = nullptr;
}

void History::addUnreadBar() {
	if (unreadBar || !showFrom || showFrom->detached() || !unreadCount()) return;

	int32 count = unreadCount();
	if (peer->migrateTo()) {
		if (History *h = App::historyLoaded(peer->migrateTo()->id)) {
			count += h->unreadCount();
		}
	}
	showFrom->setUnreadBarCount(count);
	unreadBar = showFrom;
}

void History::destroyUnreadBar() {
	if (unreadBar) {
		unreadBar->destroyUnreadBar();
	}
}

HistoryItem *History::addNewInTheMiddle(HistoryItem *newItem, int32 blockIndex, int32 itemIndex) {
	t_assert(blockIndex >= 0);
	t_assert(blockIndex < blocks.size());
	t_assert(itemIndex >= 0);
	t_assert(itemIndex <= blocks.at(blockIndex)->items.size());

	HistoryBlock *block = blocks.at(blockIndex);

	newItem->attachToBlock(block, itemIndex);
	block->items.insert(itemIndex, newItem);
	newItem->previousItemChanged();
	for (int i = itemIndex + 1, l = block->items.size(); i < l; ++i) {
		block->items.at(i)->setIndexInBlock(i);
	}
	if (itemIndex + 1 < block->items.size()) {
		block->items.at(itemIndex + 1)->previousItemChanged();
	}

	return newItem;
}

void History::startBuildingFrontBlock(int expectedItemsCount) {
	t_assert(!isBuildingFrontBlock());
	t_assert(expectedItemsCount > 0);

	_buildingFrontBlock.reset(new BuildingBlock());
	_buildingFrontBlock->expectedItemsCount = expectedItemsCount;
}

HistoryBlock *History::finishBuildingFrontBlock() {
	t_assert(isBuildingFrontBlock());

	// Some checks if there was some message history already
	HistoryBlock *block = _buildingFrontBlock->block;
	if (block && blocks.size() > 1) {
		HistoryItem *last = block->items.back(); // ... item, item, item, last ], [ first, item, item ...
		HistoryItem *first = blocks.at(1)->items.front();

		// we've added a new front block, so previous item for
		// the old first item of a first block was changed
		first->previousItemChanged();
	}

	_buildingFrontBlock = nullptr;
	return block;
}

void History::clearNotifications() {
	notifies.clear();
}

bool History::loadedAtBottom() const {
	return newLoaded;
}

bool History::loadedAtTop() const {
	return oldLoaded;
}

bool History::isReadyFor(MsgId msgId) {
	if (msgId < 0 && -msgId < ServerMaxMsgId && peer->migrateFrom()) { // old group history
		return App::history(peer->migrateFrom()->id)->isReadyFor(-msgId);
	}

	if (msgId == ShowAtTheEndMsgId) {
		return loadedAtBottom();
	}
	if (msgId == ShowAtUnreadMsgId) {
		if (peer->migrateFrom()) { // old group history
			if (History *h = App::historyLoaded(peer->migrateFrom()->id)) {
				if (h->unreadCount()) {
					return h->isReadyFor(msgId);
				}
			}
		}
		if (unreadCount()) {
			if (!isEmpty()) {
				return (loadedAtTop() || minMsgId() <= inboxReadBefore) && (loadedAtBottom() || maxMsgId() >= inboxReadBefore);
			}
			return false;
		}
		return loadedAtBottom();
	}
	HistoryItem *item = App::histItemById(channelId(), msgId);
	return item && (item->history() == this) && !item->detached();
}

void History::getReadyFor(MsgId msgId) {
	if (msgId < 0 && -msgId < ServerMaxMsgId && peer->migrateFrom()) {
		History *h = App::history(peer->migrateFrom()->id);
		h->getReadyFor(-msgId);
		if (h->isEmpty()) {
			clear(true);
		}
		return;
	}
	if (msgId == ShowAtUnreadMsgId && peer->migrateFrom()) {
		if (History *h = App::historyLoaded(peer->migrateFrom()->id)) {
			if (h->unreadCount()) {
				clear(true);
				h->getReadyFor(msgId);
				return;
			}
		}
	}
	if (!isReadyFor(msgId)) {
		clear(true);

		if (msgId == ShowAtTheEndMsgId) {
			newLoaded = true;
		}
	}
}

void History::setNotLoadedAtBottom() {
	newLoaded = false;
}

namespace {
	uint32 _dialogsPosToTopShift = 0x80000000UL;
}

inline uint64 dialogPosFromDate(const QDateTime &date) {
	return (uint64(date.toTime_t()) << 32) | (++_dialogsPosToTopShift);
}

void History::setLastMessage(HistoryItem *msg) {
	if (msg) {
		if (!lastMsg) Local::removeSavedPeer(peer);
		lastMsg = msg;
		setChatsListDate(msg->date);
	} else {
		lastMsg = 0;
	}
	updateChatListEntry();
}

bool History::needUpdateInChatList() const {
	if (inChatList(Dialogs::Mode::All)) {
		return true;
	} else if (peer->migrateTo()) {
		return false;
	}
	return (!peer->isChannel() || peer->asChannel()->amIn());
}

void History::setChatsListDate(const QDateTime &date) {
	bool updateDialog = needUpdateInChatList();
	if (!lastMsgDate.isNull() && lastMsgDate >= date) {
		if (!updateDialog || !inChatList(Dialogs::Mode::All)) {
			return;
		}
	}
	lastMsgDate = date;
	updateChatListSortPosition();
}

void History::updateChatListSortPosition() {
	auto chatListDate = [this]() {
		if (auto draft = cloudDraft()) {
			if (draft->date > lastMsgDate) {
				return draft->date;
			}
		}
		return lastMsgDate;
	};

	_sortKeyInChatList = dialogPosFromDate(chatListDate());
	if (App::main() && needUpdateInChatList()) {
		App::main()->createDialog(this);
	}
}

void History::fixLastMessage(bool wasAtBottom) {
	setLastMessage(wasAtBottom ? lastImportantMessage() : 0);
}

MsgId History::minMsgId() const {
	for_const (const HistoryBlock *block, blocks) {
		for_const (const HistoryItem *item, block->items) {
			if (item->id > 0) {
				return item->id;
			}
		}
	}
	return 0;
}

MsgId History::maxMsgId() const {
	for (auto i = blocks.cend(), e = blocks.cbegin(); i != e;) {
		--i;
		for (auto j = (*i)->items.cend(), en = (*i)->items.cbegin(); j != en;) {
			--j;
			if ((*j)->id > 0) {
				return (*j)->id;
			}
		}
	}
	return 0;
}

MsgId History::msgIdForRead() const {
	MsgId result = (lastMsg && lastMsg->id > 0) ? lastMsg->id : 0;
	if (loadedAtBottom()) result = qMax(result, maxMsgId());
	return result;
}

int History::resizeGetHeight(int newWidth) {
	bool resizeAllItems = (_flags & Flag::f_pending_resize) || (width != newWidth);

	if (!resizeAllItems && !hasPendingResizedItems()) {
		return height;
	}
	_flags &= ~(Flag::f_pending_resize | Flag::f_has_pending_resized_items);

	width = newWidth;
	int y = 0;
	for_const (HistoryBlock *block, blocks) {
		block->y = y;
		y += block->resizeGetHeight(newWidth, resizeAllItems);
	}
	height = y;
	return height;
}

ChannelHistory *History::asChannelHistory() {
	return isChannel() ? static_cast<ChannelHistory*>(this) : 0;
}

const ChannelHistory *History::asChannelHistory() const {
	return isChannel() ? static_cast<const ChannelHistory*>(this) : 0;
}

bool History::isDisplayedEmpty() const {
	return isEmpty() || ((blocks.size() == 1) && blocks.front()->items.size() == 1 && blocks.front()->items.front()->isEmpty());
}

void History::clear(bool leaveItems) {
	if (unreadBar) {
		unreadBar = nullptr;
	}
	if (showFrom) {
		showFrom = nullptr;
	}
	if (lastSentMsg) {
		lastSentMsg = nullptr;
	}
	if (scrollTopItem) {
		forgetScrollState();
	}
	if (!leaveItems) {
		setLastMessage(nullptr);
		notifies.clear();
		auto &pending = Global::RefPendingRepaintItems();
		for (auto i = pending.begin(); i != pending.end();) {
			if ((*i)->history() == this) {
				i = pending.erase(i);
			} else {
				++i;
			}
		}
	}
	for (int32 i = 0; i < OverviewCount; ++i) {
		if (!overview[i].isEmpty() || !overviewIds[i].isEmpty()) {
			if (leaveItems) {
				if (overviewCountData[i] == 0) {
					overviewCountData[i] = overview[i].size();
				}
			} else {
				overviewCountData[i] = -1; // not loaded yet
			}
			overview[i].clear();
			overviewIds[i].clear();
			if (App::wnd() && !App::quitting()) App::wnd()->mediaOverviewUpdated(peer, MediaOverviewType(i));
		}
	}
	clearBlocks(leaveItems);
	if (leaveItems) {
		lastKeyboardInited = false;
	} else {
		setUnreadCount(0);
		if (peer->isMegagroup()) {
			peer->asChannel()->mgInfo->pinnedMsgId = 0;
		}
		clearLastKeyboard();
	}
	setPendingResize();

	newLoaded = oldLoaded = false;
	forgetScrollState();

	if (peer->isChat()) {
		peer->asChat()->lastAuthors.clear();
		peer->asChat()->markupSenders.clear();
	} else if (isChannel()) {
		asChannelHistory()->cleared(leaveItems);
		if (isMegagroup()) {
			peer->asChannel()->mgInfo->markupSenders.clear();
		}
	}
	if (leaveItems && App::main()) App::main()->historyCleared(this);
}

void History::clearBlocks(bool leaveItems) {
	Blocks lst;
	std::swap(lst, blocks);
	for_const (HistoryBlock *block, lst) {
		if (leaveItems) {
			block->clear(true);
		}
		delete block;
	}
}

void History::clearOnDestroy() {
	clearBlocks(false);
}

History::PositionInChatListChange History::adjustByPosInChatList(Dialogs::Mode list, Dialogs::IndexedList *indexed) {
	t_assert(indexed != nullptr);
	Dialogs::Row *lnk = mainChatListLink(list);
	int32 movedFrom = lnk->pos();
	indexed->adjustByPos(chatListLinks(list));
	int32 movedTo = lnk->pos();
	return { movedFrom, movedTo };
}

int History::posInChatList(Dialogs::Mode list) const {
	return mainChatListLink(list)->pos();
}

Dialogs::Row *History::addToChatList(Dialogs::Mode list, Dialogs::IndexedList *indexed) {
	t_assert(indexed != nullptr);
	if (!inChatList(list)) {
		chatListLinks(list) = indexed->addToEnd(this);
		if (list == Dialogs::Mode::All && unreadCount()) {
			App::histories().unreadIncrement(unreadCount(), mute());
			Notify::unreadCounterUpdated();
		}
	}
	return mainChatListLink(list);
}

void History::removeFromChatList(Dialogs::Mode list, Dialogs::IndexedList *indexed) {
	t_assert(indexed != nullptr);
	if (inChatList(list)) {
		indexed->del(peer);
		chatListLinks(list).clear();
		if (list == Dialogs::Mode::All && unreadCount()) {
			App::histories().unreadIncrement(-unreadCount(), mute());
			Notify::unreadCounterUpdated();
		}
	}
}

void History::removeChatListEntryByLetter(Dialogs::Mode list, QChar letter) {
	t_assert(letter != 0);
	if (inChatList(list)) {
		chatListLinks(list).remove(letter);
	}
}

void History::addChatListEntryByLetter(Dialogs::Mode list, QChar letter, Dialogs::Row *row) {
	t_assert(letter != 0);
	if (inChatList(list)) {
		chatListLinks(list).insert(letter, row);
	}
}

void History::updateChatListEntry() const {
	if (MainWidget *m = App::main()) {
		if (inChatList(Dialogs::Mode::All)) {
			m->dlgUpdated(Dialogs::Mode::All, mainChatListLink(Dialogs::Mode::All));
			if (inChatList(Dialogs::Mode::Important)) {
				m->dlgUpdated(Dialogs::Mode::Important, mainChatListLink(Dialogs::Mode::Important));
			}
		}
	}
}

void History::overviewSliceDone(int32 overviewIndex, const MTPmessages_Messages &result, bool onlyCounts) {
	const QVector<MTPMessage> *v = 0;
	switch (result.type()) {
	case mtpc_messages_messages: {
		auto &d(result.c_messages_messages());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		v = &d.vmessages.c_vector().v;
		overviewCountData[overviewIndex] = 0;
	} break;

	case mtpc_messages_messagesSlice: {
		auto &d(result.c_messages_messagesSlice());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		overviewCountData[overviewIndex] = d.vcount.v;
		v = &d.vmessages.c_vector().v;
	} break;

	case mtpc_messages_channelMessages: {
		auto &d(result.c_messages_channelMessages());
		if (peer->isChannel()) {
			peer->asChannel()->ptsReceived(d.vpts.v);
		} else {
			LOG(("API Error: received messages.channelMessages when no channel was passed! (History::overviewSliceDone, onlyCounts %1)").arg(Logs::b(onlyCounts)));
		}
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		overviewCountData[overviewIndex] = d.vcount.v;
		v = &d.vmessages.c_vector().v;
	} break;

	default: return;
	}

	if (!onlyCounts && v->isEmpty()) {
		overviewCountData[overviewIndex] = 0;
	} else if (overviewCountData[overviewIndex] > 0) {
		for (History::MediaOverviewIds::const_iterator i = overviewIds[overviewIndex].cbegin(), e = overviewIds[overviewIndex].cend(); i != e; ++i) {
			if (i.key() < 0) {
				++overviewCountData[overviewIndex];
			} else {
				break;
			}
		}
	}

	for (QVector<MTPMessage>::const_iterator i = v->cbegin(), e = v->cend(); i != e; ++i) {
		HistoryItem *item = App::histories().addNewMessage(*i, NewMessageExisting);
		if (item && overviewIds[overviewIndex].constFind(item->id) == overviewIds[overviewIndex].cend()) {
			overviewIds[overviewIndex].insert(item->id, NullType());
			overview[overviewIndex].push_front(item->id);
		}
	}
}

void History::changeMsgId(MsgId oldId, MsgId newId) {
	for (int32 i = 0; i < OverviewCount; ++i) {
		History::MediaOverviewIds::iterator j = overviewIds[i].find(oldId);
		if (j != overviewIds[i].cend()) {
			overviewIds[i].erase(j);
			int32 index = overview[i].indexOf(oldId);
			if (overviewIds[i].constFind(newId) == overviewIds[i].cend()) {
				overviewIds[i].insert(newId, NullType());
				if (index >= 0) {
					overview[i][index] = newId;
				} else {
					overview[i].push_back(newId);
				}
			} else if (index >= 0) {
				overview[i].removeAt(index);
			}
		}
	}
}

void History::removeBlock(HistoryBlock *block) {
	t_assert(block->items.isEmpty());

	if (_buildingFrontBlock && block == _buildingFrontBlock->block) {
		_buildingFrontBlock->block = nullptr;
	}

	int index = block->indexInHistory();
	blocks.removeAt(index);
	for (int i = index, l = blocks.size(); i < l; ++i) {
		blocks.at(i)->setIndexInHistory(i);
	}
	if (index < blocks.size()) {
		blocks.at(index)->items.front()->previousItemChanged();
	}
}

History::~History() {
	clearOnDestroy();
}

int HistoryBlock::resizeGetHeight(int newWidth, bool resizeAllItems) {
	int y = 0;
	for_const (HistoryItem *item, items) {
		item->y = y;
		if (resizeAllItems || item->pendingResize()) {
			y += item->resizeGetHeight(newWidth);
		} else {
			y += item->height();
		}
	}
	height = y;
	return height;
}

void HistoryBlock::clear(bool leaveItems) {
	Items lst;
	std::swap(lst, items);

	if (leaveItems) {
		for_const (HistoryItem *item, lst) {
			item->detachFast();
		}
	} else {
		for_const (HistoryItem *item, lst) {
			delete item;
		}
	}
}

void HistoryBlock::removeItem(HistoryItem *item) {
	t_assert(item->block() == this);

	int blockIndex = indexInHistory();
	int itemIndex = item->indexInBlock();
	if (history->showFrom == item) {
		history->getNextShowFrom(this, itemIndex);
	}
	if (history->lastSentMsg == item) {
		history->lastSentMsg = nullptr;
	}
	if (history->unreadBar == item) {
		history->unreadBar = nullptr;
	}
	if (history->scrollTopItem == item) {
		history->getNextScrollTopItem(this, itemIndex);
	}

	item->detachFast();
	items.remove(itemIndex);
	for (int i = itemIndex, l = items.size(); i < l; ++i) {
		items.at(i)->setIndexInBlock(i);
	}
	if (items.isEmpty()) {
		history->removeBlock(this);
	} else if (itemIndex < items.size()) {
		items.at(itemIndex)->previousItemChanged();
	} else if (blockIndex + 1 < history->blocks.size()) {
		history->blocks.at(blockIndex + 1)->items.front()->previousItemChanged();
	}

	if (items.isEmpty()) {
		delete this;
	}
}

class ReplyMarkupClickHandler : public LeftButtonClickHandler {
public:
	ReplyMarkupClickHandler(const HistoryItem *item, int row, int col) : _itemId(item->fullId()), _row(row), _col(col) {
	}

	QString tooltip() const override {
		return _fullDisplayed ? QString() : buttonText();
	}

	void setFullDisplayed(bool full) {
		_fullDisplayed = full;
	}

	// Copy to clipboard support.
	void copyToClipboard() const override {
		if (auto button = getButton()) {
			if (button->type == HistoryMessageReplyMarkup::Button::Url) {
				auto url = QString::fromUtf8(button->data);
				if (!url.isEmpty()) {
					QApplication::clipboard()->setText(url);
				}
			}
		}
	}
	QString copyToClipboardContextItemText() const override {
		if (auto button = getButton()) {
			if (button->type == HistoryMessageReplyMarkup::Button::Url) {
				return lang(lng_context_copy_link);
			}
		}
		return QString();
	}

	// Finds the corresponding button in the items markup struct.
	// If the button is not found it returns nullptr.
	// Note: it is possible that we will point to the different button
	// than the one was used when constructing the handler, but not a big deal.
	const HistoryMessageReplyMarkup::Button *getButton() const {
		if (auto item = App::histItemById(_itemId)) {
			if (auto markup = item->Get<HistoryMessageReplyMarkup>()) {
				if (_row < markup->rows.size()) {
					const HistoryMessageReplyMarkup::ButtonRow &row(markup->rows.at(_row));
					if (_col < row.size()) {
						return &row.at(_col);
					}
				}
			}
		}
		return nullptr;
	}

	// We hold only FullMsgId, not HistoryItem*, because all click handlers
	// are activated async and the item may be already destroyed.
	void setMessageId(const FullMsgId &msgId) {
		_itemId = msgId;
	}

protected:
	void onClickImpl() const override {
		if (auto item = App::histItemById(_itemId)) {
			App::activateBotCommand(item, _row, _col);
		}
	}

private:
	FullMsgId _itemId;
	int _row, _col;
	bool _fullDisplayed = true;

	// Returns the full text of the corresponding button.
	QString buttonText() const {
		if (auto button = getButton()) {
			return button->text;
		}
		return QString();
	}

};

ReplyKeyboard::ReplyKeyboard(const HistoryItem *item, StylePtr &&s)
: _item(item)
, _a_selected(animation(this, &ReplyKeyboard::step_selected))
, _st(std_::forward<StylePtr>(s)) {
	if (auto markup = item->Get<HistoryMessageReplyMarkup>()) {
		_rows.reserve(markup->rows.size());
		for (int i = 0, l = markup->rows.size(); i != l; ++i) {
			const HistoryMessageReplyMarkup::ButtonRow &row(markup->rows.at(i));
			int s = row.size();
			ButtonRow newRow(s, Button());
			for (int j = 0; j != s; ++j) {
				Button &button(newRow[j]);
				QString str = row.at(j).text;
				button.type = row.at(j).type;
				button.link.reset(new ReplyMarkupClickHandler(item, i, j));
				button.text.setText(_st->textFont(), textOneLine(str), _textPlainOptions);
				button.characters = str.isEmpty() ? 1 : str.size();
			}
			_rows.push_back(newRow);
		}
	}
}

void ReplyKeyboard::updateMessageId() {
	auto msgId = _item->fullId();
	for_const (auto &row, _rows) {
		for_const (auto &button, row) {
			button.link->setMessageId(msgId);
		}
	}

}

void ReplyKeyboard::resize(int width, int height) {
	_width = width;

	auto markup = _item->Get<HistoryMessageReplyMarkup>();
	float64 y = 0, buttonHeight = _rows.isEmpty() ? _st->buttonHeight() : (float64(height + _st->buttonSkip()) / _rows.size());
	for (ButtonRow &row : _rows) {
		int s = row.size();

		int widthForText = _width - ((s - 1) * _st->buttonSkip());
		int widthOfText = 0;
		for_const (const Button &button, row) {
			widthOfText += qMax(button.text.maxWidth(), 1);
			widthForText -= _st->minButtonWidth(button.type);
		}
		bool exact = (widthForText == widthOfText);

		float64 x = 0;
		for (Button &button : row) {
			int buttonw = qMax(button.text.maxWidth(), 1);
			float64 textw = exact ? buttonw : (widthForText / float64(s));
			float64 minw = _st->minButtonWidth(button.type);
			float64 w = minw + textw;
			accumulate_max(w, 2 * float64(_st->buttonPadding()));

			int rectx = static_cast<int>(std::floor(x));
			int rectw = static_cast<int>(std::floor(x + w)) - rectx;
			button.rect = QRect(rectx, qRound(y), rectw, qRound(buttonHeight - _st->buttonSkip()));
			if (rtl()) button.rect.setX(_width - button.rect.x() - button.rect.width());
			x += w + _st->buttonSkip();

			button.link->setFullDisplayed(textw >= buttonw);
		}
		y += buttonHeight;
	}
}

bool ReplyKeyboard::isEnoughSpace(int width, const style::botKeyboardButton &st) const {
	for_const (const auto &row, _rows) {
		int s = row.size();
		int widthLeft = width - ((s - 1) * st.margin + s * 2 * st.padding);
		for_const (const auto &button, row) {
			widthLeft -= qMax(button.text.maxWidth(), 1);
			if (widthLeft < 0) {
				if (row.size() > 3) {
					return false;
				} else {
					break;
				}
			}
		}
	}
	return true;
}

void ReplyKeyboard::setStyle(StylePtr &&st) {
	_st = std_::move(st);
}

int ReplyKeyboard::naturalWidth() const {
	auto result = 0;
	for_const (const auto &row, _rows) {
		auto rowMaxButtonWidth = 0;
		for_const (const auto &button, row) {
			accumulate_max(rowMaxButtonWidth, qMax(button.text.maxWidth(), 1) + _st->minButtonWidth(button.type));
		}

		auto rowSize = row.size();
		accumulate_max(result, rowSize * rowMaxButtonWidth + (rowSize - 1) * _st->buttonSkip());
	}
	return result;
}

int ReplyKeyboard::naturalHeight() const {
	return (_rows.size() - 1) * _st->buttonSkip() + _rows.size() * _st->buttonHeight();
}

void ReplyKeyboard::paint(Painter &p, const QRect &clip) const {
	t_assert(_st != nullptr);
	t_assert(_width > 0);

	_st->startPaint(p);
	for_const (const ButtonRow &row, _rows) {
		for_const (const Button &button, row) {
			QRect rect(button.rect);
			if (rect.y() >= clip.y() + clip.height()) return;
			if (rect.y() + rect.height() < clip.y()) continue;

			// just ignore the buttons that didn't layout well
			if (rect.x() + rect.width() > _width) break;

			_st->paintButton(p, button);
		}
	}
}

ClickHandlerPtr ReplyKeyboard::getState(int x, int y) const {
	t_assert(_width > 0);

	for_const (const ButtonRow &row, _rows) {
		for_const (const Button &button, row) {
			QRect rect(button.rect);

			// just ignore the buttons that didn't layout well
			if (rect.x() + rect.width() > _width) break;

			if (rect.contains(x, y)) {
				return button.link;
			}
		}
	}
	return ClickHandlerPtr();
}

void ReplyKeyboard::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (!p) return;

	bool startAnimation = false;
	for (int i = 0, rows = _rows.size(); i != rows; ++i) {
		const ButtonRow &row(_rows.at(i));
		for (int j = 0, cols = row.size(); j != cols; ++j) {
			if (row.at(j).link == p) {
				bool startAnimation = _animations.isEmpty();

				int indexForAnimation = i * MatrixRowShift + j + 1;
				if (!active) {
					indexForAnimation = -indexForAnimation;
				}

				_animations.remove(-indexForAnimation);
				if (!_animations.contains(indexForAnimation)) {
					_animations.insert(indexForAnimation, getms());
				}

				if (startAnimation && !_a_selected.animating()) {
					_a_selected.start();
				}
				return;
			}
		}
	}
}

void ReplyKeyboard::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	_st->repaint(_item);
}

void ReplyKeyboard::step_selected(uint64 ms, bool timer) {
	for (Animations::iterator i = _animations.begin(); i != _animations.end();) {
		int index = qAbs(i.key()) - 1, row = (index / MatrixRowShift), col = index % MatrixRowShift;
		float64 dt = float64(ms - i.value()) / st::botKbDuration;
		if (dt >= 1) {
			_rows[row][col].howMuchOver = (i.key() > 0) ? 1 : 0;
			i = _animations.erase(i);
		} else {
			_rows[row][col].howMuchOver = (i.key() > 0) ? dt : (1 - dt);
			++i;
		}
	}
	if (timer) _st->repaint(_item);
	if (_animations.isEmpty()) {
		_a_selected.stop();
	}
}

void ReplyKeyboard::clearSelection() {
	for (auto i = _animations.cbegin(), e = _animations.cend(); i != e; ++i) {
		int index = qAbs(i.key()) - 1, row = (index / MatrixRowShift), col = index % MatrixRowShift;
		_rows[row][col].howMuchOver = 0;
	}
	_animations.clear();
	_a_selected.stop();
}

void ReplyKeyboard::Style::paintButton(Painter &p, const ReplyKeyboard::Button &button) const {
	const QRect &rect = button.rect;
	bool pressed = ClickHandler::showAsPressed(button.link);

	paintButtonBg(p, rect, pressed, button.howMuchOver);
	paintButtonIcon(p, rect, button.type);
	if (button.type == HistoryMessageReplyMarkup::Button::Callback) {
		if (const HistoryMessageReplyMarkup::Button *data = button.link->getButton()) {
			if (data->requestId) {
				paintButtonLoading(p, rect);
			}
		}
	}

	int tx = rect.x(), tw = rect.width();
	if (tw >= st::botKbFont->elidew + _st->padding * 2) {
		tx += _st->padding;
		tw -= _st->padding * 2;
	} else if (tw > st::botKbFont->elidew) {
		tx += (tw - st::botKbFont->elidew) / 2;
		tw = st::botKbFont->elidew;
	}
	int textTop = rect.y() + (pressed ? _st->downTextTop : _st->textTop);
	button.text.drawElided(p, tx, textTop + ((rect.height() - _st->height) / 2), tw, 1, style::al_top);
}

void HistoryMessageReplyMarkup::createFromButtonRows(const QVector<MTPKeyboardButtonRow> &v) {
	if (v.isEmpty()) {
		rows.clear();
		return;
	}

	rows.reserve(v.size());
	for_const (const auto &row, v) {
		switch (row.type()) {
		case mtpc_keyboardButtonRow: {
			const auto &r(row.c_keyboardButtonRow());
			const auto &b(r.vbuttons.c_vector().v);
			if (!b.isEmpty()) {
				ButtonRow buttonRow;
				buttonRow.reserve(b.size());
				for_const (const auto &button, b) {
					switch (button.type()) {
					case mtpc_keyboardButton: {
						buttonRow.push_back({ Button::Default, qs(button.c_keyboardButton().vtext), QByteArray(), 0 });
					} break;
					case mtpc_keyboardButtonCallback: {
						const auto &buttonData(button.c_keyboardButtonCallback());
						buttonRow.push_back({ Button::Callback, qs(buttonData.vtext), qba(buttonData.vdata), 0 });
					} break;
					case mtpc_keyboardButtonRequestGeoLocation: {
						buttonRow.push_back({ Button::RequestLocation, qs(button.c_keyboardButtonRequestGeoLocation().vtext), QByteArray(), 0 });
					} break;
					case mtpc_keyboardButtonRequestPhone: {
						buttonRow.push_back({ Button::RequestPhone, qs(button.c_keyboardButtonRequestPhone().vtext), QByteArray(), 0 });
					} break;
					case mtpc_keyboardButtonUrl: {
						const auto &buttonData(button.c_keyboardButtonUrl());
						buttonRow.push_back({ Button::Url, qs(buttonData.vtext), qba(buttonData.vurl), 0 });
					} break;
					case mtpc_keyboardButtonSwitchInline: {
						const auto &buttonData(button.c_keyboardButtonSwitchInline());
						buttonRow.push_back({ Button::SwitchInline, qs(buttonData.vtext), qba(buttonData.vquery), 0 });
						flags |= MTPDreplyKeyboardMarkup_ClientFlag::f_has_switch_inline_button;
					} break;
					}
				}
				if (!buttonRow.isEmpty()) rows.push_back(buttonRow);
			}
		} break;
		}
	}
}

void HistoryMessageReplyMarkup::create(const MTPReplyMarkup &markup) {
	flags = 0;
	rows.clear();
	inlineKeyboard = nullptr;

	switch (markup.type()) {
	case mtpc_replyKeyboardMarkup: {
		const auto &d(markup.c_replyKeyboardMarkup());
		flags = d.vflags.v;

		createFromButtonRows(d.vrows.c_vector().v);
	} break;

	case mtpc_replyInlineMarkup: {
		const auto &d(markup.c_replyInlineMarkup());
		flags = MTPDreplyKeyboardMarkup::Flags(0) | MTPDreplyKeyboardMarkup_ClientFlag::f_inline;

		createFromButtonRows(d.vrows.c_vector().v);
	} break;

	case mtpc_replyKeyboardHide: {
		const auto &d(markup.c_replyKeyboardHide());
		flags = mtpCastFlags(d.vflags) | MTPDreplyKeyboardMarkup_ClientFlag::f_zero;
	} break;

	case mtpc_replyKeyboardForceReply: {
		const auto &d(markup.c_replyKeyboardForceReply());
		flags = mtpCastFlags(d.vflags) | MTPDreplyKeyboardMarkup_ClientFlag::f_force_reply;
	} break;
	}
}

void HistoryDependentItemCallback::call(ChannelData *channel, MsgId msgId) const {
	if (HistoryItem *item = App::histItemById(_dependent)) {
		item->updateDependencyItem();
	}
}

void HistoryMessageUnreadBar::init(int count) {
	if (_freezed) return;
	_text = lng_unread_bar(lt_count, count);
	_width = st::semiboldFont->width(_text);
}

int HistoryMessageUnreadBar::height() {
	return st::unreadBarHeight + st::unreadBarMargin;
}

int HistoryMessageUnreadBar::marginTop() {
	return st::lineWidth + st::unreadBarMargin;
}

void HistoryMessageUnreadBar::paint(Painter &p, int y, int w) const {
	p.fillRect(0, y + marginTop(), w, height() - marginTop() - st::lineWidth, st::unreadBarBG);
	p.fillRect(0, y + height() - st::lineWidth, w, st::lineWidth, st::unreadBarBorder);
	p.setFont(st::unreadBarFont);
	p.setPen(st::unreadBarColor);

	int left = st::msgServiceMargin.left();
	int maxwidth = w;
	if (Adaptive::Wide()) {
		maxwidth = qMin(maxwidth, int32(st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left()));
	}
	w = maxwidth;

	p.drawText((w - _width) / 2, y + marginTop() + (st::unreadBarHeight - 2 * st::lineWidth - st::unreadBarFont->height) / 2 + st::unreadBarFont->ascent, _text);
}

void HistoryMessageDate::init(const QDateTime &date) {
	_text = langDayOfMonthFull(date.date());
	_width = st::msgServiceFont->width(_text);
}

int HistoryMessageDate::height() const {
	return st::msgServiceMargin.top() + st::msgServicePadding.top() + st::msgServiceFont->height + st::msgServicePadding.bottom() + st::msgServiceMargin.bottom();
}

void HistoryMessageDate::paint(Painter &p, int y, int w) const {
	HistoryLayout::ServiceMessagePainter::paintDate(p, _text, _width, y, w);
}

void HistoryMediaPtr::reset(HistoryMedia *p) {
	if (_p) {
		_p->detachFromParent();
		delete _p;
	}
	_p = p;
	if (_p) {
		_p->attachToParent();
	}
}

namespace internal {

TextSelection unshiftSelection(TextSelection selection, const Text &byText) {
	if (selection == FullSelection) {
		return selection;
	}
	return ::unshiftSelection(selection, byText);
}

TextSelection shiftSelection(TextSelection selection, const Text &byText) {
	if (selection == FullSelection) {
		return selection;
	}
	return ::shiftSelection(selection, byText);
}

} // namespace internal

HistoryItem::HistoryItem(History *history, MsgId msgId, MTPDmessage::Flags flags, QDateTime msgDate, int32 from) : HistoryElem()
, y(0)
, id(msgId)
, date(msgDate)
, _from(from ? App::user(from) : history->peer)
, _history(history)
, _flags(flags | MTPDmessage_ClientFlag::f_pending_init_dimensions | MTPDmessage_ClientFlag::f_pending_resize)
, _authorNameVersion(author()->nameVersion) {
}

void HistoryItem::finishCreate() {
	App::historyRegItem(this);
}

void HistoryItem::finishEdition(int oldKeyboardTop) {
	setPendingInitDimensions();
	if (App::main()) {
		App::main()->dlgUpdated(history(), id);
	}

	// invalidate cache for drawInDialog
	if (history()->textCachedFor == this) {
		history()->textCachedFor = nullptr;
	}

	if (oldKeyboardTop >= 0) {
		if (auto keyboard = Get<HistoryMessageReplyMarkup>()) {
			keyboard->oldTop = oldKeyboardTop;
		}
	}

	App::historyUpdateDependent(this);
}

void HistoryItem::finishEditionToEmpty() {
	recountDisplayDate();
	finishEdition(-1);

	_history->removeNotification(this);
	if (history()->isChannel()) {
		if (history()->peer->isMegagroup() && history()->peer->asChannel()->mgInfo->pinnedMsgId == id) {
			history()->peer->asChannel()->mgInfo->pinnedMsgId = 0;
		}
	}
	if (history()->lastKeyboardId == id) {
		history()->clearLastKeyboard();
		if (App::main()) App::main()->updateBotKeyboard(history());
	}
	if ((!out() || isPost()) && unread() && history()->unreadCount() > 0) {
		history()->setUnreadCount(history()->unreadCount() - 1);
	}

	if (auto next = nextItem()) {
		next->previousItemChanged();
	}
}

void HistoryItem::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (auto markup = Get<HistoryMessageReplyMarkup>()) {
		if (markup->inlineKeyboard) {
			markup->inlineKeyboard->clickHandlerActiveChanged(p, active);
		}
	}
	App::hoveredLinkItem(active ? this : nullptr);
	Ui::repaintHistoryItem(this);
}

void HistoryItem::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	if (auto markup = Get<HistoryMessageReplyMarkup>()) {
		if (markup->inlineKeyboard) {
			markup->inlineKeyboard->clickHandlerPressedChanged(p, pressed);
		}
	}
	App::pressedLinkItem(pressed ? this : nullptr);
	Ui::repaintHistoryItem(this);
}

void HistoryItem::destroy() {
	// All this must be done for all items manually in History::clear(false)!
	eraseFromOverview();

	bool wasAtBottom = history()->loadedAtBottom();
	_history->removeNotification(this);
	detach();
	if (history()->isChannel()) {
		if (history()->peer->isMegagroup() && history()->peer->asChannel()->mgInfo->pinnedMsgId == id) {
			history()->peer->asChannel()->mgInfo->pinnedMsgId = 0;
		}
	}
	if (history()->lastMsg == this) {
		history()->fixLastMessage(wasAtBottom);
	}
	if (history()->lastKeyboardId == id) {
		history()->clearLastKeyboard();
		if (App::main()) App::main()->updateBotKeyboard(history());
	}
	if ((!out() || isPost()) && unread() && history()->unreadCount() > 0) {
		history()->setUnreadCount(history()->unreadCount() - 1);
	}
	Global::RefPendingRepaintItems().remove(this);
	delete this;
}

void HistoryItem::detach() {
	if (detached()) return;

	if (_history->isChannel()) {
		_history->asChannelHistory()->messageDetached(this);
	}
	_block->removeItem(this);
	App::historyItemDetached(this);

	_history->setPendingResize();
}

void HistoryItem::detachFast() {
	_block = nullptr;
	_indexInBlock = -1;
}

void HistoryItem::previousItemChanged() {
	recountDisplayDate();
	recountAttachToPrevious();
}

void HistoryItem::recountAttachToPrevious() {
	bool attach = false;
	if (!isPost() && !Has<HistoryMessageDate>() && !Has<HistoryMessageUnreadBar>()) {
		if (auto previos = previousItem()) {
			attach = !previos->isPost()
				&& !previos->serviceMsg()
				&& !previos->isEmpty()
				&& previos->from() == from()
				&& (qAbs(previos->date.secsTo(date)) < AttachMessageToPreviousSecondsDelta);
		}
	}
	if (attach && !(_flags & MTPDmessage_ClientFlag::f_attach_to_previous)) {
		_flags |= MTPDmessage_ClientFlag::f_attach_to_previous;
		setPendingInitDimensions();
	} else if (!attach && (_flags & MTPDmessage_ClientFlag::f_attach_to_previous)) {
		_flags &= ~MTPDmessage_ClientFlag::f_attach_to_previous;
		setPendingInitDimensions();
	}
}

void HistoryItem::setId(MsgId newId) {
	history()->changeMsgId(id, newId);
	id = newId;

	// We don't need to call Notify::replyMarkupUpdated(this) and update keyboard
	// in history widget, because it can't exist for an outgoing message.
	// Only inline keyboards can be in outgoing messages.
	if (auto markup = inlineReplyMarkup()) {
		if (markup->inlineKeyboard) {
			markup->inlineKeyboard->updateMessageId();
		}
	}
}

bool HistoryItem::canEdit(const QDateTime &cur) const {
	if (id < 0 || date.secsTo(cur) >= Global::EditTimeLimit()) return false;

	if (auto msg = toHistoryMessage()) {
		if (msg->Has<HistoryMessageVia>() || msg->Has<HistoryMessageForwarded>()) return false;

		if (HistoryMedia *media = msg->getMedia()) {
			auto type = media->type();
			if (type != MediaTypePhoto &&
				type != MediaTypeVideo &&
				type != MediaTypeFile &&
				type != MediaTypeGif &&
				type != MediaTypeMusicFile &&
				type != MediaTypeVoiceFile &&
				type != MediaTypeWebPage) {
				return false;
			}
		}
		if (isPost()) {
			auto channel = _history->peer->asChannel();
			return (channel->amCreator() || (channel->amEditor() && out()));
		}
		return out() || (peerToUser(_history->peer->id) == MTP::authedId());
	}
	return false;
}

bool HistoryItem::unread() const {
	// Messages from myself are always read.
	if (history()->peer->isSelf()) return false;

	if (out()) {
		// Outgoing messages in converted chats are always read.
		if (history()->peer->migrateTo()) return false;

		if (id > 0) {
			if (id < history()->outboxReadBefore) return false;
			if (auto channel = history()->peer->asChannel()) {
				if (!channel->isMegagroup()) return false;
			}
		}
		return true;
	}

	if (id > 0) {
		if (id < history()->inboxReadBefore) return false;
		return true;
	}
	return (_flags & MTPDmessage_ClientFlag::f_clientside_unread);
}

void HistoryItem::destroyUnreadBar() {
	if (Has<HistoryMessageUnreadBar>()) {
		RemoveComponents(HistoryMessageUnreadBar::Bit());
		setPendingInitDimensions();
		if (_history->unreadBar == this) {
			_history->unreadBar = nullptr;
		}

		recountAttachToPrevious();
	}
}

void HistoryItem::setUnreadBarCount(int count) {
	if (count > 0) {
		HistoryMessageUnreadBar *bar;
		if (!Has<HistoryMessageUnreadBar>()) {
			AddComponents(HistoryMessageUnreadBar::Bit());
			setPendingInitDimensions();

			recountAttachToPrevious();

			bar = Get<HistoryMessageUnreadBar>();
		} else {
			bar = Get<HistoryMessageUnreadBar>();
			if (bar->_freezed) {
				return;
			}
			Global::RefPendingRepaintItems().insert(this);
		}
		bar->init(count);
	} else {
		destroyUnreadBar();
	}
}

void HistoryItem::setUnreadBarFreezed() {
	if (auto bar = Get<HistoryMessageUnreadBar>()) {
		bar->_freezed = true;
	}
}

void HistoryItem::clipCallback(ClipReaderNotification notification) {
	HistoryMedia *media = getMedia();
	if (!media) return;

	ClipReader *reader = media ? media->getClipReader() : 0;
	if (!reader) return;

	switch (notification) {
	case ClipReaderReinit: {
		bool stopped = false;
		if (reader->paused()) {
			if (MainWidget *m = App::main()) {
				if (!m->isItemVisible(this)) { // stop animation if it is not visible
					media->stopInline();
					if (DocumentData *document = media->getDocument()) { // forget data from memory
						document->forget();
					}
					stopped = true;
				}
			}
		}
		if (!stopped) {
			setPendingInitDimensions();
			Notify::historyItemLayoutChanged(this);
		}
	} break;

	case ClipReaderRepaint: {
		if (!reader->currentDisplayed()) {
			Ui::repaintHistoryItem(this);
		}
	} break;
	}
}

void HistoryItem::recountDisplayDate() {
	bool displayingDate = ([this]() {
		if (isEmpty()) return false;

		if (auto previous = previousItem()) {
			return previous->isEmpty() || (previous->date.date() != date.date());
		}
		return true;
	})();

	if (displayingDate && !Has<HistoryMessageDate>()) {
		AddComponents(HistoryMessageDate::Bit());
		Get<HistoryMessageDate>()->init(date);
		setPendingInitDimensions();
	} else if (!displayingDate && Has<HistoryMessageDate>()) {
		RemoveComponents(HistoryMessageDate::Bit());
		setPendingInitDimensions();
	}
}

HistoryItem::~HistoryItem() {
	App::historyUnregItem(this);
	if (id < 0 && App::uploader()) {
		App::uploader()->cancel(fullId());
	}
}

RadialAnimation::RadialAnimation(AnimationCallbacks &&callbacks)
: _firstStart(0)
, _lastStart(0)
, _lastTime(0)
, _opacity(0)
, a_arcEnd(0, 0)
, a_arcStart(0, FullArcLength)
, _animation(std_::move(callbacks)) {

}

void RadialAnimation::start(float64 prg) {
	_firstStart = _lastStart = _lastTime = getms();
	int32 iprg = qRound(qMax(prg, 0.0001) * AlmostFullArcLength), iprgstrict = qRound(prg * AlmostFullArcLength);
	a_arcEnd = anim::ivalue(iprgstrict, iprg);
	_animation.start();
}

void RadialAnimation::update(float64 prg, bool finished, uint64 ms) {
	int32 iprg = qRound(qMax(prg, 0.0001) * AlmostFullArcLength);
	if (iprg != a_arcEnd.to()) {
		a_arcEnd.start(iprg);
		_lastStart = _lastTime;
	}
	_lastTime = ms;

	float64 dt = float64(ms - _lastStart), fulldt = float64(ms - _firstStart);
	_opacity = qMin(fulldt / st::radialDuration, 1.);
	if (!finished) {
		a_arcEnd.update(1. - (st::radialDuration / (st::radialDuration + dt)), anim::linear);
	} else if (dt >= st::radialDuration) {
		a_arcEnd.update(1, anim::linear);
		stop();
	} else {
		float64 r = dt / st::radialDuration;
		a_arcEnd.update(r, anim::linear);
		_opacity *= 1 - r;
	}
	float64 fromstart = fulldt / st::radialPeriod;
	a_arcStart.update(fromstart - std::floor(fromstart), anim::linear);
}

void RadialAnimation::stop() {
	_firstStart = _lastStart = _lastTime = 0;
	a_arcEnd = anim::ivalue(0, 0);
	_animation.stop();
}

void RadialAnimation::step(uint64 ms) {
	_animation.step(ms);
}

void RadialAnimation::draw(Painter &p, const QRect &inner, int32 thickness, const style::color &color) {
	float64 o = p.opacity();
	p.setOpacity(o * _opacity);

	QPen pen(color->p), was(p.pen());
	pen.setWidth(thickness);
	p.setPen(pen);

	int32 len = MinArcLength + a_arcEnd.current();
	int32 from = QuarterArcLength - a_arcStart.current() - len;
	if (rtl()) {
		from = QuarterArcLength - (from - QuarterArcLength) - len;
		if (from < 0) from += FullArcLength;
	}

	p.setRenderHint(QPainter::HighQualityAntialiasing);
	p.drawArc(inner, from, len);
	p.setRenderHint(QPainter::HighQualityAntialiasing, false);

	p.setPen(was);
	p.setOpacity(o);
}

namespace {

int32 documentMaxStatusWidth(DocumentData *document) {
	int32 result = st::normalFont->width(formatDownloadText(document->size, document->size));
	if (SongData *song = document->song()) {
		result = qMax(result, st::normalFont->width(formatPlayedText(song->duration, song->duration)));
		result = qMax(result, st::normalFont->width(formatDurationAndSizeText(song->duration, document->size)));
	} else if (VoiceData *voice = document->voice()) {
		result = qMax(result, st::normalFont->width(formatPlayedText(voice->duration, voice->duration)));
		result = qMax(result, st::normalFont->width(formatDurationAndSizeText(voice->duration, document->size)));
	} else if (document->isVideo()) {
		result = qMax(result, st::normalFont->width(formatDurationAndSizeText(document->duration(), document->size)));
	} else {
		result = qMax(result, st::normalFont->width(formatSizeText(document->size)));
	}
	return result;
}

int32 gifMaxStatusWidth(DocumentData *document) {
	int32 result = st::normalFont->width(formatDownloadText(document->size, document->size));
	result = qMax(result, st::normalFont->width(formatGifAndSizeText(document->size)));
	return result;
}

TextWithEntities captionedSelectedText(const QString &attachType, const Text &caption, TextSelection selection) {
	if (selection != FullSelection) {
		return caption.originalTextWithEntities(selection, ExpandLinksAll);
	}

	TextWithEntities result, original;
	if (!caption.isEmpty()) {
		original = caption.originalTextWithEntities(AllTextSelection, ExpandLinksAll);
	}
	result.text.reserve(5 + attachType.size() + original.text.size());
	result.text.append(qstr("[ ")).append(attachType).append(qstr(" ]"));
	if (!caption.isEmpty()) {
		result.text.append(qstr("\n"));
		appendTextWithEntities(result, std_::move(original));
	}
	return result;
}

} // namespace

void HistoryFileMedia::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (p == _savel || p == _cancell) {
		if (active && !dataLoaded()) {
			ensureAnimation();
			_animation->a_thumbOver.start(1);
			_animation->_a_thumbOver.start();
		} else if (!active && _animation) {
			_animation->a_thumbOver.start(0);
			_animation->_a_thumbOver.start();
		}
	}
}

void HistoryFileMedia::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	Ui::repaintHistoryItem(_parent);
}

void HistoryFileMedia::setLinks(ClickHandlerPtr &&openl, ClickHandlerPtr &&savel, ClickHandlerPtr &&cancell) {
	_openl = std_::move(openl);
	_savel = std_::move(savel);
	_cancell = std_::move(cancell);
}

void HistoryFileMedia::setStatusSize(int32 newSize, int32 fullSize, int32 duration, qint64 realDuration) const {
	_statusSize = newSize;
	if (_statusSize == FileStatusSizeReady) {
		_statusText = (duration >= 0) ? formatDurationAndSizeText(duration, fullSize) : (duration < -1 ? formatGifAndSizeText(fullSize) : formatSizeText(fullSize));
	} else if (_statusSize == FileStatusSizeLoaded) {
		_statusText = (duration >= 0) ? formatDurationText(duration) : (duration < -1 ? qsl("GIF") : formatSizeText(fullSize));
	} else if (_statusSize == FileStatusSizeFailed) {
		_statusText = lang(lng_attach_failed);
	} else if (_statusSize >= 0) {
		_statusText = formatDownloadText(_statusSize, fullSize);
	} else {
		_statusText = formatPlayedText(-_statusSize - 1, realDuration);
	}
}

void HistoryFileMedia::step_thumbOver(float64 ms, bool timer) {
	float64 dt = ms / st::msgFileOverDuration;
	if (dt >= 1) {
		_animation->a_thumbOver.finish();
		_animation->_a_thumbOver.stop();
		checkAnimationFinished();
	} else if (!timer) {
		_animation->a_thumbOver.update(dt, anim::linear);
	}
	if (timer) {
		Ui::repaintHistoryItem(_parent);
	}
}

void HistoryFileMedia::step_radial(uint64 ms, bool timer) {
	if (timer) {
		Ui::repaintHistoryItem(_parent);
	} else {
		_animation->radial.update(dataProgress(), dataFinished(), ms);
		if (!_animation->radial.animating()) {
			checkAnimationFinished();
		}
	}
}

void HistoryFileMedia::ensureAnimation() const {
	if (!_animation) {
		_animation = new AnimationData(
			animation(const_cast<HistoryFileMedia*>(this), &HistoryFileMedia::step_thumbOver),
			animation(const_cast<HistoryFileMedia*>(this), &HistoryFileMedia::step_radial));
	}
}

void HistoryFileMedia::checkAnimationFinished() {
	if (_animation && !_animation->_a_thumbOver.animating() && !_animation->radial.animating()) {
		if (dataLoaded()) {
			delete _animation;
			_animation = 0;
		}
	}
}

HistoryFileMedia::~HistoryFileMedia() {
	deleteAndMark(_animation);
}

HistoryPhoto::HistoryPhoto(HistoryItem *parent, PhotoData *photo, const QString &caption) : HistoryFileMedia(parent)
, _data(photo)
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right()) {
	setLinks(MakeShared<PhotoOpenClickHandler>(_data), MakeShared<PhotoSaveClickHandler>(_data), MakeShared<PhotoCancelClickHandler>(_data));
	if (!caption.isEmpty()) {
		_caption.setText(st::msgFont, caption + _parent->skipBlock(), itemTextNoMonoOptions(_parent));
	}
	init();
}

HistoryPhoto::HistoryPhoto(HistoryItem *parent, PeerData *chat, const MTPDphoto &photo, int32 width) : HistoryFileMedia(parent)
, _data(App::feedPhoto(photo)) {
	setLinks(MakeShared<PhotoOpenClickHandler>(_data, chat), MakeShared<PhotoSaveClickHandler>(_data, chat), MakeShared<PhotoCancelClickHandler>(_data, chat));

	_width = width;
	init();
}

HistoryPhoto::HistoryPhoto(HistoryItem *parent, const HistoryPhoto &other) : HistoryFileMedia(parent)
, _data(other._data)
, _pixw(other._pixw)
, _pixh(other._pixh)
, _caption(other._caption) {
	setLinks(MakeShared<PhotoOpenClickHandler>(_data), MakeShared<PhotoSaveClickHandler>(_data), MakeShared<PhotoCancelClickHandler>(_data));

	init();
}

void HistoryPhoto::init() {
	_data->thumb->load();
}

void HistoryPhoto::initDimensions() {
	if (_caption.hasSkipBlock()) {
		_caption.setSkipBlock(_parent->skipBlockWidth(), _parent->skipBlockHeight());
	}

	int32 tw = convertScale(_data->full->width()), th = convertScale(_data->full->height());
	if (!tw || !th) {
		tw = th = 1;
	}
	if (tw > st::maxMediaSize) {
		th = (st::maxMediaSize * th) / tw;
		tw = st::maxMediaSize;
	}
	if (th > st::maxMediaSize) {
		tw = (st::maxMediaSize * tw) / th;
		th = st::maxMediaSize;
	}

	if (_parent->toHistoryMessage()) {
		bool bubble = _parent->hasBubble();

		int32 minWidth = qMax(st::minPhotoSize, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
		int32 maxActualWidth = qMax(tw, minWidth);
		_maxw = qMax(maxActualWidth, th);
		_minh = qMax(th, int32(st::minPhotoSize));
		if (bubble) {
			maxActualWidth += st::mediaPadding.left() + st::mediaPadding.right();
			_maxw += st::mediaPadding.left() + st::mediaPadding.right();
			_minh += st::mediaPadding.top() + st::mediaPadding.bottom();
			if (!_caption.isEmpty()) {
				_minh += st::mediaCaptionSkip + _caption.countHeight(maxActualWidth - st::msgPadding.left() - st::msgPadding.right()) + st::msgPadding.bottom();
			}
		}
	} else {
		_maxw = _minh = _width;
	}
}

int HistoryPhoto::resizeGetHeight(int width) {
	bool bubble = _parent->hasBubble();

	int tw = convertScale(_data->full->width()), th = convertScale(_data->full->height());
	if (tw > st::maxMediaSize) {
		th = (st::maxMediaSize * th) / tw;
		tw = st::maxMediaSize;
	}
	if (th > st::maxMediaSize) {
		tw = (st::maxMediaSize * tw) / th;
		th = st::maxMediaSize;
	}

	_pixw = qMin(width, _maxw);
	if (bubble) {
		_pixw -= st::mediaPadding.left() + st::mediaPadding.right();
	}
	_pixh = th;
	if (tw > _pixw) {
		_pixh = (_pixw * _pixh / tw);
	} else {
		_pixw = tw;
	}
	if (_pixh > width) {
		_pixw = (_pixw * width) / _pixh;
		_pixh = width;
	}
	if (_pixw < 1) _pixw = 1;
	if (_pixh < 1) _pixh = 1;

	int minWidth = qMax(st::minPhotoSize, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	_width = qMax(_pixw, int16(minWidth));
	_height = qMax(_pixh, int16(st::minPhotoSize));
	if (bubble) {
		_width += st::mediaPadding.left() + st::mediaPadding.right();
		_height += st::mediaPadding.top() + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			int captionw = _width - st::msgPadding.left() - st::msgPadding.right();
			_height += st::mediaCaptionSkip + _caption.countHeight(captionw) + st::msgPadding.bottom();
		}
	}
	return _height;
}

void HistoryPhoto::draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_data->automaticLoad(_parent);
	bool selected = (selection == FullSelection);
	bool loaded = _data->loaded(), displayLoading = _data->displayLoading();

	bool notChild = (_parent->getMedia() == this);
	int skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = _parent->hasBubble();
	bool out = _parent->out(), isPost = _parent->isPost(), outbg = out && !isPost;

	int captionw = width - st::msgPadding.left() - st::msgPadding.right();

	if (displayLoading) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(_data->progress());
		}
	}
	bool radial = isRadialAnimation(ms);

	if (bubble) {
		skipx = st::mediaPadding.left();
		skipy = st::mediaPadding.top();

		width -= st::mediaPadding.left() + st::mediaPadding.right();
		height -= skipy + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			height -= st::mediaCaptionSkip + _caption.countHeight(captionw) + st::msgPadding.bottom();
		}
	} else {
		App::roundShadow(p, 0, 0, width, height, selected ? st::msgInShadowSelected : st::msgInShadow, selected ? InSelectedShadowCorners : InShadowCorners);
	}

	QPixmap pix;
	if (loaded) {
		pix = _data->full->pixSingle(_pixw, _pixh, width, height);
	} else {
		pix = _data->thumb->pixBlurredSingle(_pixw, _pixh, width, height);
	}
	QRect rthumb(rtlrect(skipx, skipy, width, height, _width));
	p.drawPixmap(rthumb.topLeft(), pix);
	if (selected) {
		App::roundRect(p, rthumb, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
	}

	if (notChild && (radial || (!loaded && !_data->loading()))) {
		float64 radialOpacity = (radial && loaded && !_data->uploading()) ? _animation->radial.opacity() : 1;
		QRect inner(rthumb.x() + (rthumb.width() - st::msgFileSize) / 2, rthumb.y() + (rthumb.height() - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
		p.setPen(Qt::NoPen);
		if (selected) {
			p.setBrush(st::msgDateImgBgSelected);
		} else if (isThumbAnimation(ms)) {
			float64 over = _animation->a_thumbOver.current();
			p.setOpacity((st::msgDateImgBg->c.alphaF() * (1 - over)) + (st::msgDateImgBgOver->c.alphaF() * over));
			p.setBrush(st::white);
		} else {
			bool over = ClickHandler::showAsActive(_data->loading() ? _cancell : _savel);
			p.setBrush(over ? st::msgDateImgBgOver : st::msgDateImgBg);
		}

		p.setOpacity(radialOpacity * p.opacity());

		p.setRenderHint(QPainter::HighQualityAntialiasing);
		p.drawEllipse(inner);
		p.setRenderHint(QPainter::HighQualityAntialiasing, false);

		p.setOpacity(radial ? _animation->radial.opacity() : 1);

		p.setOpacity(radialOpacity);
		style::sprite icon;
		if (radial || _data->loading()) {
			DelayedStorageImage *delayed = _data->full->toDelayedStorageImage();
			if (!delayed || !delayed->location().isNull()) {
				icon = (selected ? st::msgFileInCancelSelected : st::msgFileInCancel);
			}
		} else {
			icon = (selected ? st::msgFileInDownloadSelected : st::msgFileInDownload);
		}
		if (!icon.isEmpty()) {
			p.drawSpriteCenter(inner, icon);
		}
		if (radial) {
			p.setOpacity(1);
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			_animation->radial.draw(p, rinner, st::msgFileRadialLine, selected ? st::msgInBgSelected : st::msgInBg);
		}
	}

	// date
	if (_caption.isEmpty()) {
		if (notChild && (_data->uploading() || App::hoveredItem() == _parent)) {
			int32 fullRight = skipx + width, fullBottom = skipy + height;
			_parent->drawInfo(p, fullRight, fullBottom, 2 * skipx + width, selected, InfoDisplayOverImage);
		}
	} else {
		p.setPen(st::black);
		_caption.draw(p, st::msgPadding.left(), skipy + height + st::mediaPadding.bottom() + st::mediaCaptionSkip, captionw, style::al_left, 0, -1, selection);
	}
}

HistoryTextState HistoryPhoto::getState(int x, int y, HistoryStateRequest request) const {
	HistoryTextState result;

	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return result;
	int skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = _parent->hasBubble();

	if (bubble) {
		skipx = st::mediaPadding.left();
		skipy = st::mediaPadding.top();
		if (!_caption.isEmpty()) {
			int captionw = width - st::msgPadding.left() - st::msgPadding.right();
			height -= _caption.countHeight(captionw) + st::msgPadding.bottom();
			if (x >= st::msgPadding.left() && y >= height && x < st::msgPadding.left() + captionw && y < _height) {
				result = _caption.getState(x - st::msgPadding.left(), y - height, captionw, request.forText());
				return result;
			}
			height -= st::mediaCaptionSkip;
		}
		width -= st::mediaPadding.left() + st::mediaPadding.right();
		height -= skipy + st::mediaPadding.bottom();
	}
	if (x >= skipx && y >= skipy && x < skipx + width && y < skipy + height) {
		if (_data->uploading()) {
			result.link = _cancell;
		} else if (_data->loaded()) {
			result.link = _openl;
		} else if (_data->loading()) {
			DelayedStorageImage *delayed = _data->full->toDelayedStorageImage();
			if (!delayed || !delayed->location().isNull()) {
				result.link = _cancell;
			}
		} else {
			result.link = _savel;
		}
		if (_caption.isEmpty() && _parent->getMedia() == this) {
			int32 fullRight = skipx + width, fullBottom = skipy + height;
			bool inDate = _parent->pointInTime(fullRight, fullBottom, x, y, InfoDisplayOverImage);
			if (inDate) {
				result.cursor = HistoryInDateCursorState;
			}
		}
		return result;
	}
	return result;
}

void HistoryPhoto::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaPhoto) {
		const auto &photo(media.c_messageMediaPhoto().vphoto);
		App::feedPhoto(photo, _data);

		if (photo.type() == mtpc_photo) {
			const auto &sizes(photo.c_photo().vsizes.c_vector().v);
			int32 max = 0;
			const MTPDfileLocation *maxLocation = 0;
			for (int32 i = 0, l = sizes.size(); i < l; ++i) {
				char size = 0;
				const MTPFileLocation *loc = 0;
				switch (sizes.at(i).type()) {
				case mtpc_photoSize: {
					const string &s(sizes.at(i).c_photoSize().vtype.c_string().v);
					loc = &sizes.at(i).c_photoSize().vlocation;
					if (s.size()) size = s[0];
				} break;

				case mtpc_photoCachedSize: {
					const string &s(sizes.at(i).c_photoCachedSize().vtype.c_string().v);
					loc = &sizes.at(i).c_photoCachedSize().vlocation;
					if (s.size()) size = s[0];
				} break;
				}
				if (!loc || loc->type() != mtpc_fileLocation) continue;
				if (size == 's') {
					Local::writeImage(storageKey(loc->c_fileLocation()), _data->thumb);
				} else if (size == 'm') {
					Local::writeImage(storageKey(loc->c_fileLocation()), _data->medium);
				} else if (size == 'x' && max < 1) {
					max = 1;
					maxLocation = &loc->c_fileLocation();
				} else if (size == 'y' && max < 2) {
					max = 2;
					maxLocation = &loc->c_fileLocation();
				//} else if (size == 'w' && max < 3) {
				//	max = 3;
				//	maxLocation = &loc->c_fileLocation();
				}
			}
			if (maxLocation) {
				Local::writeImage(storageKey(*maxLocation), _data->full);
			}
		}
	}
}

bool HistoryPhoto::needReSetInlineResultMedia(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaPhoto) {
		if (PhotoData *existing = App::feedPhoto(media.c_messageMediaPhoto().vphoto)) {
			if (existing == _data) {
				return false;
			} else {
				// collect data
			}
		}
	}
	return false;
}

void HistoryPhoto::attachToParent() {
	App::regPhotoItem(_data, _parent);
}

void HistoryPhoto::detachFromParent() {
	App::unregPhotoItem(_data, _parent);
}

QString HistoryPhoto::inDialogsText() const {
	return _caption.isEmpty() ? lang(lng_in_dlg_photo) : _caption.originalText(AllTextSelection, ExpandLinksNone);
}

TextWithEntities HistoryPhoto::selectedText(TextSelection selection) const {
	return captionedSelectedText(lang(lng_in_dlg_photo), _caption, selection);
}

ImagePtr HistoryPhoto::replyPreview() {
	return _data->makeReplyPreview();
}

HistoryVideo::HistoryVideo(HistoryItem *parent, DocumentData *document, const QString &caption) : HistoryFileMedia(parent)
, _data(document)
, _thumbw(1)
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right()) {
	if (!caption.isEmpty()) {
		_caption.setText(st::msgFont, caption + _parent->skipBlock(), itemTextNoMonoOptions(_parent));
	}

	setDocumentLinks(_data);

	setStatusSize(FileStatusSizeReady);

	_data->thumb->load();
}

HistoryVideo::HistoryVideo(HistoryItem *parent, const HistoryVideo &other) : HistoryFileMedia(parent)
, _data(other._data)
, _thumbw(other._thumbw)
, _caption(other._caption) {
	setDocumentLinks(_data);

	setStatusSize(other._statusSize);
}

void HistoryVideo::initDimensions() {
	bool bubble = _parent->hasBubble();

	if (_caption.hasSkipBlock()) {
		_caption.setSkipBlock(_parent->skipBlockWidth(), _parent->skipBlockHeight());
	}

	int32 tw = convertScale(_data->thumb->width()), th = convertScale(_data->thumb->height());
	if (!tw || !th) {
		tw = th = 1;
	}
	if (tw * st::msgVideoSize.height() > th * st::msgVideoSize.width()) {
		th = qRound((st::msgVideoSize.width() / float64(tw)) * th);
		tw = st::msgVideoSize.width();
	} else {
		tw = qRound((st::msgVideoSize.height() / float64(th)) * tw);
		th = st::msgVideoSize.height();
	}

	_thumbw = qMax(tw, 1);
	int32 minWidth = qMax(st::minPhotoSize, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	minWidth = qMax(minWidth, documentMaxStatusWidth(_data) + 2 * int32(st::msgDateImgDelta + st::msgDateImgPadding.x()));
	_maxw = qMax(_thumbw, int32(minWidth));
	_minh = qMax(th, int32(st::minPhotoSize));
	if (bubble) {
		_maxw += st::mediaPadding.left() + st::mediaPadding.right();
		_minh += st::mediaPadding.top() + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			_minh += st::mediaCaptionSkip + _caption.countHeight(_maxw - st::msgPadding.left() - st::msgPadding.right()) + st::msgPadding.bottom();
		}
	}
}

int HistoryVideo::resizeGetHeight(int width) {
	bool bubble = _parent->hasBubble();

	int tw = convertScale(_data->thumb->width()), th = convertScale(_data->thumb->height());
	if (!tw || !th) {
		tw = th = 1;
	}
	if (tw * st::msgVideoSize.height() > th * st::msgVideoSize.width()) {
		th = qRound((st::msgVideoSize.width() / float64(tw)) * th);
		tw = st::msgVideoSize.width();
	} else {
		tw = qRound((st::msgVideoSize.height() / float64(th)) * tw);
		th = st::msgVideoSize.height();
	}

	if (bubble) {
		width -= st::mediaPadding.left() + st::mediaPadding.right();
	}
	if (width < tw) {
		th = qRound((width / float64(tw)) * th);
		tw = width;
	}

	_thumbw = qMax(tw, 1);
	int minWidth = qMax(st::minPhotoSize, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	minWidth = qMax(minWidth, documentMaxStatusWidth(_data) + 2 * int(st::msgDateImgDelta + st::msgDateImgPadding.x()));
	_width = qMax(_thumbw, int(minWidth));
	_height = qMax(th, int(st::minPhotoSize));
	if (bubble) {
		_width += st::mediaPadding.left() + st::mediaPadding.right();
		_height += st::mediaPadding.top() + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			int captionw = _width - st::msgPadding.left() - st::msgPadding.right();
			_height += st::mediaCaptionSkip + _caption.countHeight(captionw) + st::msgPadding.bottom();
		}
	}
	return _height;
}

void HistoryVideo::draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_data->automaticLoad(_parent);
	bool loaded = _data->loaded(), displayLoading = _data->displayLoading();
	bool selected = (selection == FullSelection);

	int skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = _parent->hasBubble();
	bool out = _parent->out(), isPost = _parent->isPost(), outbg = out && !isPost;

	int captionw = width - st::msgPadding.left() - st::msgPadding.right();

	if (displayLoading) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(_data->progress());
		}
	}
	updateStatusText();
	bool radial = isRadialAnimation(ms);

	if (bubble) {
		skipx = st::mediaPadding.left();
		skipy = st::mediaPadding.top();

		width -= st::mediaPadding.left() + st::mediaPadding.right();
		height -= skipy + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			height -= st::mediaCaptionSkip + _caption.countHeight(captionw) + st::msgPadding.bottom();
		}
	} else {
		App::roundShadow(p, 0, 0, width, height, selected ? st::msgInShadowSelected : st::msgInShadow, selected ? InSelectedShadowCorners : InShadowCorners);
	}

	QRect rthumb(rtlrect(skipx, skipy, width, height, _width));
	p.drawPixmap(rthumb.topLeft(), _data->thumb->pixBlurredSingle(_thumbw, 0, width, height));
	if (selected) {
		App::roundRect(p, rthumb, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
	}

	QRect inner(rthumb.x() + (rthumb.width() - st::msgFileSize) / 2, rthumb.y() + (rthumb.height() - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
	p.setPen(Qt::NoPen);
	if (selected) {
		p.setBrush(st::msgDateImgBgSelected);
	} else if (isThumbAnimation(ms)) {
		float64 over = _animation->a_thumbOver.current();
		p.setOpacity((st::msgDateImgBg->c.alphaF() * (1 - over)) + (st::msgDateImgBgOver->c.alphaF() * over));
		p.setBrush(st::white);
	} else {
		bool over = ClickHandler::showAsActive(_data->loading() ? _cancell : _savel);
		p.setBrush(over ? st::msgDateImgBgOver : st::msgDateImgBg);
	}

	p.setRenderHint(QPainter::HighQualityAntialiasing);
	p.drawEllipse(inner);
	p.setRenderHint(QPainter::HighQualityAntialiasing, false);

	if (!selected && _animation) {
		p.setOpacity(1);
	}

	style::sprite icon;
	if (loaded) {
		icon = (selected ? st::msgFileInPlaySelected : st::msgFileInPlay);
	} else if (radial || _data->loading()) {
		icon = (selected ? st::msgFileInCancelSelected : st::msgFileInCancel);
	} else {
		icon = (selected ? st::msgFileInDownloadSelected : st::msgFileInDownload);
	}
	p.drawSpriteCenter(inner, icon);
	if (radial) {
		QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
		_animation->radial.draw(p, rinner, st::msgFileRadialLine, selected ? st::msgInBgSelected : st::msgInBg);
	}

	int32 statusX = skipx + st::msgDateImgDelta + st::msgDateImgPadding.x(), statusY = skipy + st::msgDateImgDelta + st::msgDateImgPadding.y();
	int32 statusW = st::normalFont->width(_statusText) + 2 * st::msgDateImgPadding.x();
	int32 statusH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
	App::roundRect(p, rtlrect(statusX - st::msgDateImgPadding.x(), statusY - st::msgDateImgPadding.y(), statusW, statusH, _width), selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);
	p.setFont(st::normalFont);
	p.setPen(st::black);
	p.drawTextLeft(statusX, statusY, _width, _statusText, statusW - 2 * st::msgDateImgPadding.x());

	// date
	if (_caption.isEmpty()) {
		if (_parent->getMedia() == this) {
			int32 fullRight = skipx + width, fullBottom = skipy + height;
			_parent->drawInfo(p, fullRight, fullBottom, 2 * skipx + width, selected, InfoDisplayOverImage);
		}
	} else {
		p.setPen(st::black);
		_caption.draw(p, st::msgPadding.left(), skipy + height + st::mediaPadding.bottom() + st::mediaCaptionSkip, captionw, style::al_left, 0, -1, selection);
	}
}

HistoryTextState HistoryVideo::getState(int x, int y, HistoryStateRequest request) const {
	HistoryTextState result;

	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return result;

	bool loaded = _data->loaded();

	int32 skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = _parent->hasBubble();

	if (bubble) {
		skipx = st::mediaPadding.left();
		skipy = st::mediaPadding.top();
		if (!_caption.isEmpty()) {
			int32 captionw = width - st::msgPadding.left() - st::msgPadding.right();
			height -= _caption.countHeight(captionw) + st::msgPadding.bottom();
			if (x >= st::msgPadding.left() && y >= height && x < st::msgPadding.left() + captionw && y < _height) {
				result = _caption.getState(x - st::msgPadding.left(), y - height, captionw, request.forText());
			}
			height -= st::mediaCaptionSkip;
		}
		width -= st::mediaPadding.left() + st::mediaPadding.right();
		height -= skipy + st::mediaPadding.bottom();
	}
	if (x >= skipx && y >= skipy && x < skipx + width && y < skipy + height) {
		result.link = loaded ? _openl : (_data->loading() ? _cancell : _savel);
		if (_caption.isEmpty() && _parent->getMedia() == this) {
			int32 fullRight = skipx + width, fullBottom = skipy + height;
			bool inDate = _parent->pointInTime(fullRight, fullBottom, x, y, InfoDisplayOverImage);
			if (inDate) {
				result.cursor = HistoryInDateCursorState;
			}
		}
		return result;
	}
	return result;
}

void HistoryVideo::setStatusSize(int32 newSize) const {
	HistoryFileMedia::setStatusSize(newSize, _data->size, _data->duration(), 0);
}

QString HistoryVideo::inDialogsText() const {
	return _caption.isEmpty() ? lang(lng_in_dlg_video) : _caption.originalText(AllTextSelection, ExpandLinksNone);
}

TextWithEntities HistoryVideo::selectedText(TextSelection selection) const {
	return captionedSelectedText(lang(lng_in_dlg_video), _caption, selection);
}

void HistoryVideo::updateStatusText() const {
	bool showPause = false;
	int32 statusSize = 0, realDuration = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_data->status == FileUploading) {
		statusSize = _data->uploadOffset;
	} else if (_data->loading()) {
		statusSize = _data->loadOffset();
	} else if (_data->loaded()) {
		statusSize = FileStatusSizeLoaded;
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _statusSize) {
		setStatusSize(statusSize);
	}
}

void HistoryVideo::attachToParent() {
	App::regDocumentItem(_data, _parent);
}

void HistoryVideo::detachFromParent() {
	App::unregDocumentItem(_data, _parent);
}

bool HistoryVideo::needReSetInlineResultMedia(const MTPMessageMedia &media) {
	return needReSetInlineResultDocument(media, _data);
}

ImagePtr HistoryVideo::replyPreview() {
	if (_data->replyPreview->isNull() && !_data->thumb->isNull()) {
		if (_data->thumb->loaded()) {
			int w = convertScale(_data->thumb->width()), h = convertScale(_data->thumb->height());
			if (w <= 0) w = 1;
			if (h <= 0) h = 1;
			_data->replyPreview = ImagePtr(w > h ? _data->thumb->pix(w * st::msgReplyBarSize.height() / h, st::msgReplyBarSize.height()) : _data->thumb->pix(st::msgReplyBarSize.height()), "PNG");
		} else {
			_data->thumb->load();
		}
	}
	return _data->replyPreview;
}

HistoryDocumentVoicePlayback::HistoryDocumentVoicePlayback(const HistoryDocument *that)
: _position(0)
, a_progress(0., 0.)
, _a_progress(animation(const_cast<HistoryDocument*>(that), &HistoryDocument::step_voiceProgress)) {
}

void HistoryDocumentVoice::ensurePlayback(const HistoryDocument *that) const {
	if (!_playback) {
		_playback = new HistoryDocumentVoicePlayback(that);
	}
}

void HistoryDocumentVoice::checkPlaybackFinished() const {
	if (_playback && !_playback->_a_progress.animating()) {
		delete _playback;
		_playback = nullptr;
	}
}

HistoryDocument::HistoryDocument(HistoryItem *parent, DocumentData *document, const QString &caption) : HistoryFileMedia(parent)
, _data(document) {
	createComponents(!caption.isEmpty());
	if (auto named = Get<HistoryDocumentNamed>()) {
		named->_name = documentName(_data);
		named->_namew = st::semiboldFont->width(named->_name);
	}

	setDocumentLinks(_data);

	setStatusSize(FileStatusSizeReady);

	if (auto captioned = Get<HistoryDocumentCaptioned>()) {
		captioned->_caption.setText(st::msgFont, caption + _parent->skipBlock(), itemTextNoMonoOptions(_parent));
	}
}

HistoryDocument::HistoryDocument(HistoryItem *parent, const HistoryDocument &other) : HistoryFileMedia(parent)
, Composer()
, _data(other._data) {
	auto captioned = other.Get<HistoryDocumentCaptioned>();
	createComponents(captioned != 0);
	if (auto named = Get<HistoryDocumentNamed>()) {
		if (auto othernamed = other.Get<HistoryDocumentNamed>()) {
			named->_name = othernamed->_name;
			named->_namew = othernamed->_namew;
		} else {
			named->_name = documentName(_data);
			named->_namew = st::semiboldFont->width(named->_name);
		}
	}

	setDocumentLinks(_data);

	setStatusSize(other._statusSize);

	if (captioned) {
		Get<HistoryDocumentCaptioned>()->_caption = captioned->_caption;
	}
}

void HistoryDocument::createComponents(bool caption) {
	uint64 mask = 0;
	if (_data->voice()) {
		mask |= HistoryDocumentVoice::Bit();
	} else {
		mask |= HistoryDocumentNamed::Bit();
		if (!_data->song() && !_data->thumb->isNull() && _data->thumb->width() && _data->thumb->height()) {
			mask |= HistoryDocumentThumbed::Bit();
		}
	}
	if (caption) {
		mask |= HistoryDocumentCaptioned::Bit();
	}
	UpdateComponents(mask);
	if (auto thumbed = Get<HistoryDocumentThumbed>()) {
		thumbed->_linksavel.reset(new DocumentSaveClickHandler(_data));
		thumbed->_linkcancell.reset(new DocumentCancelClickHandler(_data));
	}
}

void HistoryDocument::initDimensions() {
	auto captioned = Get<HistoryDocumentCaptioned>();
	if (captioned && captioned->_caption.hasSkipBlock()) {
		captioned->_caption.setSkipBlock(_parent->skipBlockWidth(), _parent->skipBlockHeight());
	}

	auto thumbed = Get<HistoryDocumentThumbed>();
	if (thumbed) {
		_data->thumb->load();
		int32 tw = convertScale(_data->thumb->width()), th = convertScale(_data->thumb->height());
		if (tw > th) {
			thumbed->_thumbw = (tw * st::msgFileThumbSize) / th;
		} else {
			thumbed->_thumbw = st::msgFileThumbSize;
		}
	}

	_maxw = st::msgFileMinWidth;

	int32 tleft = 0, tright = 0;
	if (thumbed) {
		tleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		tright = st::msgFileThumbPadding.left();
		_maxw = qMax(_maxw, tleft + documentMaxStatusWidth(_data) + tright);
	} else {
		tleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
		tright = st::msgFileThumbPadding.left();
		int32 unread = _data->voice() ? (st::mediaUnreadSkip + st::mediaUnreadSize) : 0;
		_maxw = qMax(_maxw, tleft + documentMaxStatusWidth(_data) + unread + _parent->skipBlockWidth() + st::msgPadding.right());
	}

	if (auto named = Get<HistoryDocumentNamed>()) {
		_maxw = qMax(tleft + named->_namew + tright, _maxw);
		_maxw = qMin(_maxw, int(st::msgMaxWidth));
	}

	if (thumbed) {
		_minh = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom();
		if (!captioned && _parent->Has<HistoryMessageSigned>()) {
			_minh += st::msgDateFont->height - st::msgDateDelta.y();
		}
	} else {
		_minh = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom();
	}

	if (captioned) {
		_minh += captioned->_caption.countHeight(_maxw - st::msgPadding.left() - st::msgPadding.right()) + st::msgPadding.bottom();
	} else {
		_height = _minh;
	}
}

int HistoryDocument::resizeGetHeight(int width) {
	auto captioned = Get<HistoryDocumentCaptioned>();
	if (!captioned) {
		return HistoryFileMedia::resizeGetHeight(width);
	}

	_width = qMin(width, _maxw);
	if (Get<HistoryDocumentThumbed>()) {
		_height = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom();
	} else {
		_height = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom();
	}
	_height += captioned->_caption.countHeight(_width - st::msgPadding.left() - st::msgPadding.right()) + st::msgPadding.bottom();

	return _height;
}

void HistoryDocument::draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_data->automaticLoad(_parent);
	bool loaded = _data->loaded(), displayLoading = _data->displayLoading();
	bool selected = (selection == FullSelection);

	int captionw = _width - st::msgPadding.left() - st::msgPadding.right();

	bool out = _parent->out(), isPost = _parent->isPost(), outbg = out && !isPost;

	if (displayLoading) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(_data->progress());
		}
	}
	bool showPause = updateStatusText();
	bool radial = isRadialAnimation(ms);

	int nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0, bottom = 0;
	if (auto thumbed = Get<HistoryDocumentThumbed>()) {
		nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		nametop = st::msgFileThumbNameTop;
		nameright = st::msgFileThumbPadding.left();
		statustop = st::msgFileThumbStatusTop;
		linktop = st::msgFileThumbLinkTop;
		bottom = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom();

		QRect rthumb(rtlrect(st::msgFileThumbPadding.left(), st::msgFileThumbPadding.top(), st::msgFileThumbSize, st::msgFileThumbSize, _width));
		QPixmap thumb = loaded ? _data->thumb->pixSingle(thumbed->_thumbw, 0, st::msgFileThumbSize, st::msgFileThumbSize) : _data->thumb->pixBlurredSingle(thumbed->_thumbw, 0, st::msgFileThumbSize, st::msgFileThumbSize);
		p.drawPixmap(rthumb.topLeft(), thumb);
		if (selected) {
			App::roundRect(p, rthumb, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
		}

		if (radial || (!loaded && !_data->loading())) {
            float64 radialOpacity = (radial && loaded && !_data->uploading()) ? _animation->radial.opacity() : 1;
			QRect inner(rthumb.x() + (rthumb.width() - st::msgFileSize) / 2, rthumb.y() + (rthumb.height() - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
			p.setPen(Qt::NoPen);
			if (selected) {
				p.setBrush(st::msgDateImgBgSelected);
			} else if (isThumbAnimation(ms)) {
				float64 over = _animation->a_thumbOver.current();
				p.setOpacity((st::msgDateImgBg->c.alphaF() * (1 - over)) + (st::msgDateImgBgOver->c.alphaF() * over));
				p.setBrush(st::white);
			} else {
				bool over = ClickHandler::showAsActive(_data->loading() ? _cancell : _savel);
				p.setBrush(over ? st::msgDateImgBgOver : st::msgDateImgBg);
			}
			p.setOpacity(radialOpacity * p.opacity());

			p.setRenderHint(QPainter::HighQualityAntialiasing);
			p.drawEllipse(inner);
			p.setRenderHint(QPainter::HighQualityAntialiasing, false);

			p.setOpacity(radialOpacity);
			style::sprite icon;
			if (radial || _data->loading()) {
				icon = (selected ? st::msgFileInCancelSelected : st::msgFileInCancel);
			} else {
				icon = (selected ? st::msgFileInDownloadSelected : st::msgFileInDownload);
			}
			p.setOpacity((radial && loaded) ? _animation->radial.opacity() : 1);
			p.drawSpriteCenter(inner, icon);
			if (radial) {
				p.setOpacity(1);

				QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
				_animation->radial.draw(p, rinner, st::msgFileRadialLine, selected ? st::msgInBgSelected : st::msgInBg);
			}
		}

		if (_data->status != FileUploadFailed) {
			const ClickHandlerPtr &lnk((_data->loading() || _data->status == FileUploading) ? thumbed->_linkcancell : thumbed->_linksavel);
			bool over = ClickHandler::showAsActive(lnk);
			p.setFont(over ? st::semiboldFont->underline() : st::semiboldFont);
			p.setPen(outbg ? (selected ? st::msgFileThumbLinkOutFgSelected : st::msgFileThumbLinkOutFg) : (selected ? st::msgFileThumbLinkInFgSelected : st::msgFileThumbLinkInFg));
			p.drawTextLeft(nameleft, linktop, _width, thumbed->_link, thumbed->_linkw);
		}
	} else {
		nameleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
		nametop = st::msgFileNameTop;
		nameright = st::msgFilePadding.left();
		statustop = st::msgFileStatusTop;
		bottom = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom();

		QRect inner(rtlrect(st::msgFilePadding.left(), st::msgFilePadding.top(), st::msgFileSize, st::msgFileSize, _width));
		p.setPen(Qt::NoPen);
		if (selected) {
			p.setBrush(outbg ? st::msgFileOutBgSelected : st::msgFileInBgSelected);
		} else if (isThumbAnimation(ms)) {
			float64 over = _animation->a_thumbOver.current();
			p.setBrush(style::interpolate(outbg ? st::msgFileOutBg : st::msgFileInBg, outbg ? st::msgFileOutBgOver : st::msgFileInBgOver, over));
		} else {
			bool over = ClickHandler::showAsActive(_data->loading() ? _cancell : _savel);
			p.setBrush(outbg ? (over ? st::msgFileOutBgOver : st::msgFileOutBg) : (over ? st::msgFileInBgOver : st::msgFileInBg));
		}

		p.setRenderHint(QPainter::HighQualityAntialiasing);
		p.drawEllipse(inner);
		p.setRenderHint(QPainter::HighQualityAntialiasing, false);

		if (radial) {
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			style::color bg(outbg ? (selected ? st::msgOutBgSelected : st::msgOutBg) : (selected ? st::msgInBgSelected : st::msgInBg));
			_animation->radial.draw(p, rinner, st::msgFileRadialLine, bg);
		}

		style::sprite icon;
		if (showPause) {
			icon = outbg ? (selected ? st::msgFileOutPauseSelected : st::msgFileOutPause) : (selected ? st::msgFileInPauseSelected : st::msgFileInPause);
		} else if (radial || _data->loading()) {
			icon = outbg ? (selected ? st::msgFileOutCancelSelected : st::msgFileOutCancel) : (selected ? st::msgFileInCancelSelected : st::msgFileInCancel);
		} else if (loaded) {
			if (_data->song() || _data->voice()) {
				icon = outbg ? (selected ? st::msgFileOutPlaySelected : st::msgFileOutPlay) : (selected ? st::msgFileInPlaySelected : st::msgFileInPlay);
			} else if (_data->isImage()) {
				icon = outbg ? (selected ? st::msgFileOutImageSelected : st::msgFileOutImage) : (selected ? st::msgFileInImageSelected : st::msgFileInImage);
			} else {
				icon = outbg ? (selected ? st::msgFileOutFileSelected : st::msgFileOutFile) : (selected ? st::msgFileInFileSelected : st::msgFileInFile);
			}
		} else {
			icon = outbg ? (selected ? st::msgFileOutDownloadSelected : st::msgFileOutDownload) : (selected ? st::msgFileInDownloadSelected : st::msgFileInDownload);
		}
		p.drawSpriteCenter(inner, icon);
	}
	int32 namewidth = _width - nameleft - nameright;

	if (auto voice = Get<HistoryDocumentVoice>()) {
		const VoiceWaveform *wf = 0;
		uchar norm_value = 0;
		if (_data->voice()) {
			wf = &_data->voice()->waveform;
			if (wf->isEmpty()) {
				wf = 0;
				if (loaded) {
					Local::countVoiceWaveform(_data);
				}
			} else if (wf->at(0) < 0) {
				wf = 0;
			} else {
				norm_value = _data->voice()->wavemax;
			}
		}
		float64 prg = voice->_playback ? voice->_playback->a_progress.current() : 0;

		// rescale waveform by going in waveform.size * bar_count 1D grid
		style::color active(outbg ? (selected ? st::msgWaveformOutActiveSelected : st::msgWaveformOutActive) : (selected ? st::msgWaveformInActiveSelected : st::msgWaveformInActive));
		style::color inactive(outbg ? (selected ? st::msgWaveformOutInactiveSelected : st::msgWaveformOutInactive) : (selected ? st::msgWaveformInInactiveSelected : st::msgWaveformInInactive));
		int32 wf_size = wf ? wf->size() : WaveformSamplesCount, availw = int32(namewidth + st::msgWaveformSkip), activew = qRound(availw * prg);
		if (!outbg && !voice->_playback && _parent->isMediaUnread()) {
			activew = availw;
		}
		int32 bar_count = qMin(availw / int32(st::msgWaveformBar + st::msgWaveformSkip), wf_size);
		uchar max_value = 0;
		int32 max_delta = st::msgWaveformMax - st::msgWaveformMin, bottom = st::msgFilePadding.top() + st::msgWaveformMax;
		p.setPen(Qt::NoPen);
		for (int32 i = 0, bar_x = 0, sum_i = 0; i < wf_size; ++i) {
			uchar value = wf ? wf->at(i) : 0;
			if (sum_i + bar_count >= wf_size) { // draw bar
				sum_i = sum_i + bar_count - wf_size;
				if (sum_i < (bar_count + 1) / 2) {
					if (max_value < value) max_value = value;
				}
				int32 bar_value = ((max_value * max_delta) + ((norm_value + 1) / 2)) / (norm_value + 1);

				if (bar_x >= activew) {
					p.fillRect(nameleft + bar_x, bottom - bar_value, st::msgWaveformBar, st::msgWaveformMin + bar_value, inactive);
				} else if (bar_x + st::msgWaveformBar <= activew) {
					p.fillRect(nameleft + bar_x, bottom - bar_value, st::msgWaveformBar, st::msgWaveformMin + bar_value, active);
				} else {
					p.fillRect(nameleft + bar_x, bottom - bar_value, activew - bar_x, st::msgWaveformMin + bar_value, active);
					p.fillRect(nameleft + activew, bottom - bar_value, st::msgWaveformBar - (activew - bar_x), st::msgWaveformMin + bar_value, inactive);
				}
				bar_x += st::msgWaveformBar + st::msgWaveformSkip;

				if (sum_i < (bar_count + 1) / 2) {
					max_value = 0;
				} else {
					max_value = value;
				}
			} else {
				if (max_value < value) max_value = value;

				sum_i += bar_count;
			}
		}
	} else if (auto named = Get<HistoryDocumentNamed>()) {
		p.setFont(st::semiboldFont);
		p.setPen(st::black);
		if (namewidth < named->_namew) {
			p.drawTextLeft(nameleft, nametop, _width, st::semiboldFont->elided(named->_name, namewidth));
		} else {
			p.drawTextLeft(nameleft, nametop, _width, named->_name, named->_namew);
		}
	}

	style::color status(outbg ? (selected ? st::mediaOutFgSelected : st::mediaOutFg) : (selected ? st::mediaInFgSelected : st::mediaInFg));
	p.setFont(st::normalFont);
	p.setPen(status);
	p.drawTextLeft(nameleft, statustop, _width, _statusText);

	if (_parent->isMediaUnread()) {
		int32 w = st::normalFont->width(_statusText);
		if (w + st::mediaUnreadSkip + st::mediaUnreadSize <= namewidth) {
			p.setPen(Qt::NoPen);
			p.setBrush(outbg ? (selected ? st::msgFileOutBgSelected : st::msgFileOutBg) : (selected ? st::msgFileInBgSelected : st::msgFileInBg));

			p.setRenderHint(QPainter::HighQualityAntialiasing, true);
			p.drawEllipse(rtlrect(nameleft + w + st::mediaUnreadSkip, statustop + st::mediaUnreadTop, st::mediaUnreadSize, st::mediaUnreadSize, _width));
			p.setRenderHint(QPainter::HighQualityAntialiasing, false);
		}
	}

	if (auto captioned = Get<HistoryDocumentCaptioned>()) {
		p.setPen(st::black);
		captioned->_caption.draw(p, st::msgPadding.left(), bottom, captionw, style::al_left, 0, -1, selection);
	}
}

HistoryTextState HistoryDocument::getState(int x, int y, HistoryStateRequest request) const {
	HistoryTextState result;

	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return result;

	bool out = _parent->out(), isPost = _parent->isPost(), outbg = out && !isPost;
	bool loaded = _data->loaded();

	bool showPause = updateStatusText();

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0, bottom = 0;
	if (auto thumbed = Get<HistoryDocumentThumbed>()) {
		nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		linktop = st::msgFileThumbLinkTop;
		bottom = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom();

		QRect rthumb(rtlrect(st::msgFileThumbPadding.left(), st::msgFileThumbPadding.top(), st::msgFileThumbSize, st::msgFileThumbSize, _width));

		if ((_data->loading() || _data->uploading() || !loaded) && rthumb.contains(x, y)) {
			result.link = (_data->loading() || _data->uploading()) ? _cancell : _savel;
			return result;
		}

		if (_data->status != FileUploadFailed) {
			if (rtlrect(nameleft, linktop, thumbed->_linkw, st::semiboldFont->height, _width).contains(x, y)) {
				result.link = (_data->loading() || _data->uploading()) ? thumbed->_linkcancell : thumbed->_linksavel;
				return result;
			}
		}
	} else {
		bottom = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom();

		QRect inner(rtlrect(st::msgFilePadding.left(), st::msgFilePadding.top(), st::msgFileSize, st::msgFileSize, _width));
		if ((_data->loading() || _data->uploading() || !loaded) && inner.contains(x, y)) {
			result.link = (_data->loading() || _data->uploading()) ? _cancell : _savel;
			return result;
		}
	}

	int32 height = _height;
	if (auto captioned = Get<HistoryDocumentCaptioned>()) {
		if (y >= bottom) {
			result = captioned->_caption.getState(x - st::msgPadding.left(), y - bottom, _width - st::msgPadding.left() - st::msgPadding.right(), request.forText());
			return result;
		}
		height -= captioned->_caption.countHeight(_width - st::msgPadding.left() - st::msgPadding.right()) + st::msgPadding.bottom();
	}
	if (x >= 0 && y >= 0 && x < _width && y < height && !_data->loading() && !_data->uploading() && _data->isValid()) {
		result.link = _openl;
		return result;
	}
	return result;
}

QString HistoryDocument::inDialogsText() const {
	QString result;
	if (Has<HistoryDocumentVoice>()) {
		result = lang(lng_in_dlg_audio);
	} else if (auto song = _data->song()) {
		result = documentName(_data);
		if (result.isEmpty()) {
			result = lang(lng_in_dlg_audio_file);
		}
	} else {
		auto named = Get<HistoryDocumentNamed>();
		result = (!named || named->_name.isEmpty()) ? lang(lng_in_dlg_file) : named->_name;
	}
	if (auto captioned = Get<HistoryDocumentCaptioned>()) {
		if (!captioned->_caption.isEmpty()) {
			result.append(' ').append(captioned->_caption.originalText(AllTextSelection, ExpandLinksNone));
		}
	}
	return result;
}

TextWithEntities HistoryDocument::selectedText(TextSelection selection) const {
	const Text emptyCaption;
	const Text *caption = &emptyCaption;
	if (auto captioned = Get<HistoryDocumentCaptioned>()) {
		caption = &captioned->_caption;
	}
	QString attachType = lang(lng_in_dlg_file);
	if (Has<HistoryDocumentVoice>()) {
		attachType = lang(lng_in_dlg_audio);
	} else if (_data->song()) {
		attachType = lang(lng_in_dlg_audio_file);
	}
	if (auto named = Get<HistoryDocumentNamed>()) {
		if (!named->_name.isEmpty()) {
			attachType.append(qstr(" : ")).append(named->_name);
		}
	}
	return captionedSelectedText(attachType, *caption, selection);
}

void HistoryDocument::setStatusSize(int32 newSize, qint64 realDuration) const {
	int32 duration = _data->song() ? _data->song()->duration : (_data->voice() ? _data->voice()->duration : -1);
	HistoryFileMedia::setStatusSize(newSize, _data->size, duration, realDuration);
	if (auto thumbed = Get<HistoryDocumentThumbed>()) {
		if (_statusSize == FileStatusSizeReady) {
			thumbed->_link = lang(lng_media_download).toUpper();
		} else if (_statusSize == FileStatusSizeLoaded) {
			thumbed->_link = lang(lng_media_open_with).toUpper();
		} else if (_statusSize == FileStatusSizeFailed) {
			thumbed->_link = lang(lng_media_download).toUpper();
		} else if (_statusSize >= 0) {
			thumbed->_link = lang(lng_media_cancel).toUpper();
		} else {
			thumbed->_link = lang(lng_media_open_with).toUpper();
		}
		thumbed->_linkw = st::semiboldFont->width(thumbed->_link);
	}
}

bool HistoryDocument::updateStatusText() const {
	bool showPause = false;
	int32 statusSize = 0, realDuration = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_data->status == FileUploading) {
		statusSize = _data->uploadOffset;
	} else if (_data->loading()) {
		statusSize = _data->loadOffset();
	} else if (_data->loaded()) {
		if (_data->voice()) {
			AudioMsgId playing;
			AudioPlayerState playingState = AudioPlayerStopped;
			int64 playingPosition = 0, playingDuration = 0;
			int32 playingFrequency = 0;
			if (audioPlayer()) {
				audioPlayer()->currentState(&playing, &playingState, &playingPosition, &playingDuration, &playingFrequency);
			}

			if (playing == AudioMsgId(_data, _parent->fullId()) && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
				if (auto voice = Get<HistoryDocumentVoice>()) {
					bool was = voice->_playback;
					voice->ensurePlayback(this);
					if (!was || playingPosition != voice->_playback->_position) {
						float64 prg = playingDuration ? snap(float64(playingPosition) / playingDuration, 0., 1.) : 0.;
						if (voice->_playback->_position < playingPosition) {
							voice->_playback->a_progress.start(prg);
						} else {
							voice->_playback->a_progress = anim::fvalue(0., prg);
						}
						voice->_playback->_position = playingPosition;
						voice->_playback->_a_progress.start();
					}
				}

				statusSize = -1 - (playingPosition / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency));
				realDuration = playingDuration / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency);
				showPause = (playingState == AudioPlayerPlaying || playingState == AudioPlayerResuming || playingState == AudioPlayerStarting);
			} else {
				statusSize = FileStatusSizeLoaded;
				if (auto voice = Get<HistoryDocumentVoice>()) {
					voice->checkPlaybackFinished();
				}
			}
		} else if (_data->song()) {
			SongMsgId playing;
			AudioPlayerState playingState = AudioPlayerStopped;
			int64 playingPosition = 0, playingDuration = 0;
			int32 playingFrequency = 0;
			if (audioPlayer()) {
				audioPlayer()->currentState(&playing, &playingState, &playingPosition, &playingDuration, &playingFrequency);
			}

			if (playing == SongMsgId(_data, _parent->fullId()) && !(playingState & AudioPlayerStoppedMask) && playingState != AudioPlayerFinishing) {
				statusSize = -1 - (playingPosition / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency));
				realDuration = playingDuration / (playingFrequency ? playingFrequency : AudioVoiceMsgFrequency);
				showPause = (playingState == AudioPlayerPlaying || playingState == AudioPlayerResuming || playingState == AudioPlayerStarting);
			} else {
				statusSize = FileStatusSizeLoaded;
			}
			if (!showPause && (playing == SongMsgId(_data, _parent->fullId())) && App::main() && App::main()->player()->seekingSong(playing)) {
				showPause = true;
			}
		} else {
			statusSize = FileStatusSizeLoaded;
		}
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _statusSize) {
		setStatusSize(statusSize, realDuration);
	}
	return showPause;
}

void HistoryDocument::step_voiceProgress(float64 ms, bool timer) {
	if (auto voice = Get<HistoryDocumentVoice>()) {
		if (voice->_playback) {
			float64 dt = ms / (2 * AudioVoiceMsgUpdateView);
			if (dt >= 1) {
				voice->_playback->_a_progress.stop();
				voice->_playback->a_progress.finish();
			} else {
				voice->_playback->a_progress.update(qMin(dt, 1.), anim::linear);
			}
			if (timer) Ui::repaintHistoryItem(_parent);
		}
	}
}

void HistoryDocument::attachToParent() {
	App::regDocumentItem(_data, _parent);
}

void HistoryDocument::detachFromParent() {
	App::unregDocumentItem(_data, _parent);
}

void HistoryDocument::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaDocument) {
		App::feedDocument(media.c_messageMediaDocument().vdocument, _data);
		if (!_data->data().isEmpty()) {
			if (_data->voice()) {
				Local::writeAudio(_data->mediaKey(), _data->data());
			} else {
				Local::writeStickerImage(_data->mediaKey(), _data->data());
			}
		}
	}
}

bool HistoryDocument::needReSetInlineResultMedia(const MTPMessageMedia &media) {
	return needReSetInlineResultDocument(media, _data);
}

ImagePtr HistoryDocument::replyPreview() {
	return _data->makeReplyPreview();
}

HistoryGif::HistoryGif(HistoryItem *parent, DocumentData *document, const QString &caption) : HistoryFileMedia(parent)
, _data(document)
, _thumbw(1)
, _thumbh(1)
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right())
, _gif(nullptr) {
	setDocumentLinks(_data, true);

	setStatusSize(FileStatusSizeReady);

	if (!caption.isEmpty()) {
		_caption.setText(st::msgFont, caption + _parent->skipBlock(), itemTextNoMonoOptions(_parent));
	}

	_data->thumb->load();
}

HistoryGif::HistoryGif(HistoryItem *parent, const HistoryGif &other) : HistoryFileMedia(parent)
, _data(other._data)
, _thumbw(other._thumbw)
, _thumbh(other._thumbh)
, _caption(other._caption)
, _gif(nullptr) {
	setDocumentLinks(_data, true);

	setStatusSize(other._statusSize);
}

void HistoryGif::initDimensions() {
	if (_caption.hasSkipBlock()) {
		_caption.setSkipBlock(_parent->skipBlockWidth(), _parent->skipBlockHeight());
	}

	bool bubble = _parent->hasBubble();
	int32 tw = 0, th = 0;
	if (gif() && _gif->state() == ClipError) {
		if (!_gif->autoplay()) {
			Ui::showLayer(new InformBox(lang(lng_gif_error)));
		}
		App::unregGifItem(_gif);
		delete _gif;
		_gif = BadClipReader;
	}

	if (gif() && _gif->ready()) {
		tw = convertScale(_gif->width());
		th = convertScale(_gif->height());
	} else {
		tw = convertScale(_data->dimensions.width()), th = convertScale(_data->dimensions.height());
		if (!tw || !th) {
			tw = convertScale(_data->thumb->width());
			th = convertScale(_data->thumb->height());
		}
	}
	if (tw > st::maxGifSize) {
		th = (st::maxGifSize * th) / tw;
		tw = st::maxGifSize;
	}
	if (th > st::maxGifSize) {
		tw = (st::maxGifSize * tw) / th;
		th = st::maxGifSize;
	}
	if (!tw || !th) {
		tw = th = 1;
	}
	_thumbw = tw;
	_thumbh = th;
	_maxw = qMax(tw, int32(st::minPhotoSize));
	_minh = qMax(th, int32(st::minPhotoSize));
	_maxw = qMax(_maxw, _parent->infoWidth() + 2 * int32(st::msgDateImgDelta + st::msgDateImgPadding.x()));
	if (!gif() || !_gif->ready()) {
		_maxw = qMax(_maxw, gifMaxStatusWidth(_data) + 2 * int32(st::msgDateImgDelta + st::msgDateImgPadding.x()));
	}
	if (bubble) {
		_maxw += st::mediaPadding.left() + st::mediaPadding.right();
		_minh += st::mediaPadding.top() + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			_minh += st::mediaCaptionSkip + _caption.countHeight(_maxw - st::msgPadding.left() - st::msgPadding.right()) + st::msgPadding.bottom();
		}
	}
}

int HistoryGif::resizeGetHeight(int width) {
	bool bubble = _parent->hasBubble();

	int tw = 0, th = 0;
	if (gif() && _gif->ready()) {
		tw = convertScale(_gif->width());
		th = convertScale(_gif->height());
	} else {
		tw = convertScale(_data->dimensions.width()), th = convertScale(_data->dimensions.height());
		if (!tw || !th) {
			tw = convertScale(_data->thumb->width());
			th = convertScale(_data->thumb->height());
		}
	}
	if (tw > st::maxGifSize) {
		th = (st::maxGifSize * th) / tw;
		tw = st::maxGifSize;
	}
	if (th > st::maxGifSize) {
		tw = (st::maxGifSize * tw) / th;
		th = st::maxGifSize;
	}
	if (!tw || !th) {
		tw = th = 1;
	}

	if (bubble) {
		width -= st::mediaPadding.left() + st::mediaPadding.right();
	}
	if (width < tw) {
		th = qRound((width / float64(tw)) * th);
		tw = width;
	}
	_thumbw = tw;
	_thumbh = th;

	_width = qMax(tw, int32(st::minPhotoSize));
	_height = qMax(th, int32(st::minPhotoSize));
	_width = qMax(_width, _parent->infoWidth() + 2 * int32(st::msgDateImgDelta + st::msgDateImgPadding.x()));
	if (gif() && _gif->ready()) {
		if (!_gif->started()) {
			_gif->start(_thumbw, _thumbh, _width, _height, true);
		}
	} else {
		_width = qMax(_width, gifMaxStatusWidth(_data) + 2 * int32(st::msgDateImgDelta + st::msgDateImgPadding.x()));
	}
	if (bubble) {
		_width += st::mediaPadding.left() + st::mediaPadding.right();
		_height += st::mediaPadding.top() + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			_height += st::mediaCaptionSkip + _caption.countHeight(_width - st::msgPadding.left() - st::msgPadding.right()) + st::msgPadding.bottom();
		}
	}

	return _height;
}

void HistoryGif::draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_data->automaticLoad(_parent);
	bool loaded = _data->loaded(), displayLoading = (_parent->id < 0) || _data->displayLoading();
	bool selected = (selection == FullSelection);

	if (loaded && !gif() && _gif != BadClipReader && cAutoPlayGif()) {
		Ui::autoplayMediaInlineAsync(_parent->fullId());
	}

	int32 skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = _parent->hasBubble();
	bool out = _parent->out(), isPost = _parent->isPost(), outbg = out && !isPost;

	int32 captionw = width - st::msgPadding.left() - st::msgPadding.right();

	bool animating = (gif() && _gif->started());

	if (!animating || _parent->id < 0) {
		if (displayLoading) {
			ensureAnimation();
			if (!_animation->radial.animating()) {
				_animation->radial.start(dataProgress());
			}
		}
		updateStatusText();
	}
	bool radial = isRadialAnimation(ms);

	if (bubble) {
		skipx = st::mediaPadding.left();
		skipy = st::mediaPadding.top();

		width -= st::mediaPadding.left() + st::mediaPadding.right();
		height -= skipy + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			height -= st::mediaCaptionSkip + _caption.countHeight(captionw) + st::msgPadding.bottom();
		}
	} else {
		App::roundShadow(p, 0, 0, width, _height, selected ? st::msgInShadowSelected : st::msgInShadow, selected ? InSelectedShadowCorners : InShadowCorners);
	}

	QRect rthumb(rtlrect(skipx, skipy, width, height, _width));

	if (animating) {
		p.drawPixmap(rthumb.topLeft(), _gif->current(_thumbw, _thumbh, width, height, (Ui::isLayerShown() || Ui::isMediaViewShown() || Ui::isInlineItemBeingChosen()) ? 0 : ms));
	} else {
		p.drawPixmap(rthumb.topLeft(), _data->thumb->pixBlurredSingle(_thumbw, _thumbh, width, height));
	}
	if (selected) {
		App::roundRect(p, rthumb, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
	}

	if (radial || (!_gif && ((!loaded && !_data->loading()) || !cAutoPlayGif())) || (_gif == BadClipReader)) {
        float64 radialOpacity = (radial && loaded && _parent->id > 0) ? _animation->radial.opacity() : 1;
		QRect inner(rthumb.x() + (rthumb.width() - st::msgFileSize) / 2, rthumb.y() + (rthumb.height() - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
		p.setPen(Qt::NoPen);
		if (selected) {
			p.setBrush(st::msgDateImgBgSelected);
		} else if (isThumbAnimation(ms)) {
			float64 over = _animation->a_thumbOver.current();
			p.setOpacity((st::msgDateImgBg->c.alphaF() * (1 - over)) + (st::msgDateImgBgOver->c.alphaF() * over));
			p.setBrush(st::white);
		} else {
			bool over = ClickHandler::showAsActive(_data->loading() ? _cancell : _savel);
			p.setBrush(over ? st::msgDateImgBgOver : st::msgDateImgBg);
		}
		p.setOpacity(radialOpacity * p.opacity());

		p.setRenderHint(QPainter::HighQualityAntialiasing);
		p.drawEllipse(inner);
		p.setRenderHint(QPainter::HighQualityAntialiasing, false);

		p.setOpacity(radialOpacity);
		style::sprite icon;
		if (_data->loaded() && !radial) {
			icon = (selected ? st::msgFileInPlaySelected : st::msgFileInPlay);
		} else if (radial || _data->loading()) {
			if (_parent->id > 0 || _data->uploading()) {
				icon = (selected ? st::msgFileInCancelSelected : st::msgFileInCancel);
			}
		} else {
			icon = (selected ? st::msgFileInDownloadSelected : st::msgFileInDownload);
		}
		if (!icon.isEmpty()) {
			p.drawSpriteCenter(inner, icon);
		}
		if (radial) {
			p.setOpacity(1);
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			_animation->radial.draw(p, rinner, st::msgFileRadialLine, selected ? st::msgInBgSelected : st::msgInBg);
		}

		if (!animating || _parent->id < 0) {
			int32 statusX = skipx + st::msgDateImgDelta + st::msgDateImgPadding.x(), statusY = skipy + st::msgDateImgDelta + st::msgDateImgPadding.y();
			int32 statusW = st::normalFont->width(_statusText) + 2 * st::msgDateImgPadding.x();
			int32 statusH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
			App::roundRect(p, rtlrect(statusX - st::msgDateImgPadding.x(), statusY - st::msgDateImgPadding.y(), statusW, statusH, _width), selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);
			p.setFont(st::normalFont);
			p.setPen(st::black);
			p.drawTextLeft(statusX, statusY, _width, _statusText, statusW - 2 * st::msgDateImgPadding.x());
		}
	}

	if (!_caption.isEmpty()) {
		p.setPen(st::black);
		_caption.draw(p, st::msgPadding.left(), skipy + height + st::mediaPadding.bottom() + st::mediaCaptionSkip, captionw, style::al_left, 0, -1, selection);
	} else if (_parent->getMedia() == this && (_data->uploading() || App::hoveredItem() == _parent)) {
		int32 fullRight = skipx + width, fullBottom = skipy + height;
		_parent->drawInfo(p, fullRight, fullBottom, 2 * skipx + width, selected, InfoDisplayOverImage);
	}
}

HistoryTextState HistoryGif::getState(int x, int y, HistoryStateRequest request) const {
	HistoryTextState result;

	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return result;
	int32 skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = _parent->hasBubble();

	if (bubble) {
		skipx = st::mediaPadding.left();
		skipy = st::mediaPadding.top();
		if (!_caption.isEmpty()) {
			int32 captionw = width - st::msgPadding.left() - st::msgPadding.right();
			height -= _caption.countHeight(captionw) + st::msgPadding.bottom();
			if (x >= st::msgPadding.left() && y >= height && x < st::msgPadding.left() + captionw && y < _height) {
				result = _caption.getState(x - st::msgPadding.left(), y - height, captionw, request.forText());
				return result;
			}
			height -= st::mediaCaptionSkip;
		}
		width -= st::mediaPadding.left() + st::mediaPadding.right();
		height -= skipy + st::mediaPadding.bottom();
	}
	if (x >= skipx && y >= skipy && x < skipx + width && y < skipy + height) {
		if (_data->uploading()) {
			result.link = _cancell;
		} else if (!gif() || !cAutoPlayGif()) {
			result.link = _data->loaded() ? _openl : (_data->loading() ? _cancell : _savel);
		}
		if (_parent->getMedia() == this) {
			int32 fullRight = skipx + width, fullBottom = skipy + height;
			bool inDate = _parent->pointInTime(fullRight, fullBottom, x, y, InfoDisplayOverImage);
			if (inDate) {
				result.cursor = HistoryInDateCursorState;
			}
		}
		return result;
	}
	return result;
}

QString HistoryGif::inDialogsText() const {
	return qsl("GIF") + (_caption.isEmpty() ? QString() : (' ' + _caption.originalText(AllTextSelection, ExpandLinksNone)));
}

TextWithEntities HistoryGif::selectedText(TextSelection selection) const {
	return captionedSelectedText(qsl("GIF"), _caption, selection);
}

void HistoryGif::setStatusSize(int32 newSize) const {
	HistoryFileMedia::setStatusSize(newSize, _data->size, -2, 0);
}

void HistoryGif::updateStatusText() const {
	bool showPause = false;
	int32 statusSize = 0, realDuration = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_data->status == FileUploading) {
		statusSize = _data->uploadOffset;
	} else if (_data->loading()) {
		statusSize = _data->loadOffset();
	} else if (_data->loaded()) {
		statusSize = FileStatusSizeLoaded;
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _statusSize) {
		setStatusSize(statusSize);
	}
}

void HistoryGif::attachToParent() {
	App::regDocumentItem(_data, _parent);
}

void HistoryGif::detachFromParent() {
	App::unregDocumentItem(_data, _parent);
}

void HistoryGif::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaDocument) {
		App::feedDocument(media.c_messageMediaDocument().vdocument, _data);
	}
}

bool HistoryGif::needReSetInlineResultMedia(const MTPMessageMedia &media) {
	return needReSetInlineResultDocument(media, _data);
}

ImagePtr HistoryGif::replyPreview() {
	return _data->makeReplyPreview();
}

bool HistoryGif::playInline(bool autoplay) {
	if (gif()) {
		stopInline();
	} else if (_data->loaded(DocumentData::FilePathResolveChecked)) {
		if (!cAutoPlayGif()) {
			App::stopGifItems();
		}
		_gif = new ClipReader(_data->location(), _data->data(), func(_parent, &HistoryItem::clipCallback));
		App::regGifItem(_gif, _parent);
		if (gif()) _gif->setAutoplay();
	}
	return true;
}

void HistoryGif::stopInline() {
	if (gif()) {
		App::unregGifItem(_gif);
		delete _gif;
		_gif = 0;
	}

	_parent->setPendingInitDimensions();
	Notify::historyItemLayoutChanged(_parent);
}

HistoryGif::~HistoryGif() {
	if (gif()) {
		App::unregGifItem(_gif);
		deleteAndMark(_gif);
	}
}

float64 HistoryGif::dataProgress() const {
	return (_data->uploading() || !_parent || _parent->id > 0) ? _data->progress() : 0;
}

bool HistoryGif::dataFinished() const {
	return (!_parent || _parent->id > 0) ? (!_data->loading() && !_data->uploading()) : false;
}

bool HistoryGif::dataLoaded() const {
	return (!_parent || _parent->id > 0) ? _data->loaded() : false;
}

namespace {

class StickerClickHandler : public LeftButtonClickHandler {
public:
	StickerClickHandler(const HistoryItem *item) : _item(item) {
	}

protected:
	void onClickImpl() const override {
		if (HistoryMedia *media = _item->getMedia()) {
			if (DocumentData *document = media->getDocument()) {
				if (StickerData *sticker = document->sticker()) {
					if (sticker->set.type() != mtpc_inputStickerSetEmpty && App::main()) {
						App::main()->stickersBox(sticker->set);
					}
				}
			}
		}
	}

private:
	const HistoryItem *_item;

};

} // namespace

HistorySticker::HistorySticker(HistoryItem *parent, DocumentData *document) : HistoryMedia(parent)
, _pixw(1)
, _pixh(1)
, _data(document)
, _emoji(_data->sticker()->alt) {
	_data->thumb->load();
	if (EmojiPtr e = emojiFromText(_emoji)) {
		_emoji = emojiString(e);
	}
}

void HistorySticker::initDimensions() {
	if (!_packLink && _data->sticker() && _data->sticker()->set.type() != mtpc_inputStickerSetEmpty) {
		_packLink = ClickHandlerPtr(new StickerClickHandler(_parent));
	}
	_pixw = _data->dimensions.width();
	_pixh = _data->dimensions.height();
	if (_pixw > st::maxStickerSize) {
		_pixh = (st::maxStickerSize * _pixh) / _pixw;
		_pixw = st::maxStickerSize;
	}
	if (_pixh > st::maxStickerSize) {
		_pixw = (st::maxStickerSize * _pixw) / _pixh;
		_pixh = st::maxStickerSize;
	}
	if (_pixw < 1) _pixw = 1;
	if (_pixh < 1) _pixh = 1;
	_maxw = qMax(_pixw, int16(st::minPhotoSize));
	_minh = qMax(_pixh, int16(st::minPhotoSize));
	if (_parent->getMedia() == this) {
		_maxw += additionalWidth();
	}

	_height = _minh;
}

int HistorySticker::resizeGetHeight(int width) { // return new height
	_width = qMin(width, _maxw);
	if (_parent->getMedia() == this) {
		auto via = _parent->Get<HistoryMessageVia>();
		auto reply = _parent->Get<HistoryMessageReply>();
		if (via || reply) {
			int usew = _maxw - additionalWidth(via, reply);
			int availw = _width - usew - st::msgReplyPadding.left() - st::msgReplyPadding.left() - st::msgReplyPadding.left();
			if (via) {
				via->resize(availw);
			}
			if (reply) {
				reply->resize(availw);
			}
		}
	}
	return _height;
}

void HistorySticker::draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_data->checkSticker();
	bool loaded = _data->loaded();
	bool selected = (selection == FullSelection);

	bool out = _parent->out(), isPost = _parent->isPost(), childmedia = (_parent->getMedia() != this);

	int usew = _maxw, usex = 0;
	auto via = childmedia ? nullptr : _parent->Get<HistoryMessageVia>();
	auto reply = childmedia ? nullptr : _parent->Get<HistoryMessageReply>();
	if (via || reply) {
		usew -= additionalWidth(via, reply);
		if (isPost) {
		} else if (out) {
			usex = _width - usew;
		}
	}
	if (rtl()) usex = _width - usex - usew;

	if (selected) {
		if (_data->sticker()->img->isNull()) {
			p.drawPixmap(QPoint(usex + (usew - _pixw) / 2, (_minh - _pixh) / 2), _data->thumb->pixBlurredColored(st::msgStickerOverlay, _pixw, _pixh));
		} else {
			p.drawPixmap(QPoint(usex + (usew - _pixw) / 2, (_minh - _pixh) / 2), _data->sticker()->img->pixColored(st::msgStickerOverlay, _pixw, _pixh));
		}
	} else {
		if (_data->sticker()->img->isNull()) {
			p.drawPixmap(QPoint(usex + (usew - _pixw) / 2, (_minh - _pixh) / 2), _data->thumb->pixBlurred(_pixw, _pixh));
		} else {
			p.drawPixmap(QPoint(usex + (usew - _pixw) / 2, (_minh - _pixh) / 2), _data->sticker()->img->pix(_pixw, _pixh));
		}
	}

	if (!childmedia) {
		_parent->drawInfo(p, usex + usew, _height, usex * 2 + usew, selected, InfoDisplayOverBackground);

		if (via || reply) {
			int rectw = _width - usew - st::msgReplyPadding.left();
			int recth = st::msgReplyPadding.top() + st::msgReplyPadding.bottom();
			if (via) {
				recth += st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
			}
			if (reply) {
				recth += st::msgReplyBarSize.height();
			}
			int rectx = isPost ? (usew + st::msgReplyPadding.left()) : (out ? 0 : (usew + st::msgReplyPadding.left()));
			int recty = _height - recth;
			if (rtl()) rectx = _width - rectx - rectw;

			// Make the bottom of the rect at the same level as the bottom of the info rect.
			recty -= st::msgDateImgDelta;

			App::roundRect(p, rectx, recty, rectw, recth, selected ? App::msgServiceSelectBg() : App::msgServiceBg(), selected ? ServiceSelectedCorners : ServiceCorners);
			rectx += st::msgReplyPadding.left();
			rectw -= st::msgReplyPadding.left() + st::msgReplyPadding.right();
			if (via) {
				p.drawTextLeft(rectx, recty + st::msgReplyPadding.top(), 2 * rectx + rectw, via->_text);
				int skip = st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
				recty += skip;
			}
			if (reply) {
				HistoryMessageReply::PaintFlags flags = 0;
				if (selected) {
					flags |= HistoryMessageReply::PaintSelected;
				}
				reply->paint(p, _parent, rectx, recty, rectw, flags);
			}
		}
	}
}

HistoryTextState HistorySticker::getState(int x, int y, HistoryStateRequest request) const {
	HistoryTextState result;
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return result;

	bool out = _parent->out(), isPost = _parent->isPost(), childmedia = (_parent->getMedia() != this);

	int usew = _maxw, usex = 0;
	auto via = childmedia ? nullptr : _parent->Get<HistoryMessageVia>();
	auto reply = childmedia ? nullptr : _parent->Get<HistoryMessageReply>();
	if (via || reply) {
		usew -= additionalWidth(via, reply);
		if (isPost) {
		} else if (out) {
			usex = _width - usew;
		}
	}
	if (rtl()) usex = _width - usex - usew;

	if (via || reply) {
		int rectw = _width - usew - st::msgReplyPadding.left();
		int recth = st::msgReplyPadding.top() + st::msgReplyPadding.bottom();
		if (via) {
			recth += st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
		}
		if (reply) {
			recth += st::msgReplyBarSize.height();
		}
		int rectx = isPost ? (usew + st::msgReplyPadding.left()) : (out ? 0 : (usew + st::msgReplyPadding.left()));
		int recty = _height - recth;
		if (rtl()) rectx = _width - rectx - rectw;

		// Make the bottom of the rect at the same level as the bottom of the info rect.
		recty -= st::msgDateImgDelta;

		if (via) {
			int viah = st::msgReplyPadding.top() + st::msgServiceNameFont->height + (reply ? 0 : st::msgReplyPadding.bottom());
			if (x >= rectx && y >= recty && x < rectx + rectw && y < recty + viah) {
				result.link = via->_lnk;
				return result;
			}
			int skip = st::msgServiceNameFont->height + (reply ? 2 * st::msgReplyPadding.top() : 0);
			recty += skip;
			recth -= skip;
		}
		if (reply) {
			if (x >= rectx && y >= recty && x < rectx + rectw && y < recty + recth) {
				result.link = reply->replyToLink();
				return result;
			}
		}
	}
	if (_parent->getMedia() == this) {
		bool inDate = _parent->pointInTime(usex + usew, _height, x, y, InfoDisplayOverImage);
		if (inDate) {
			result.cursor = HistoryInDateCursorState;
		}
	}

	int pixLeft = usex + (usew - _pixw) / 2, pixTop = (_minh - _pixh) / 2;
	if (x >= pixLeft && x < pixLeft + _pixw && y >= pixTop && y < pixTop + _pixh) {
		result.link = _packLink;
		return result;
	}
	return result;
}

QString HistorySticker::inDialogsText() const {
	return _emoji.isEmpty() ? lang(lng_in_dlg_sticker) : lng_in_dlg_sticker_emoji(lt_emoji, _emoji);
}

TextWithEntities HistorySticker::selectedText(TextSelection selection) const {
	if (selection != FullSelection) {
		return TextWithEntities();
	}
	return { qsl("[ ") + inDialogsText() + qsl(" ]"), EntitiesInText() };
}

void HistorySticker::attachToParent() {
	App::regDocumentItem(_data, _parent);
}

void HistorySticker::detachFromParent() {
	App::unregDocumentItem(_data, _parent);
}

void HistorySticker::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaDocument) {
		App::feedDocument(media.c_messageMediaDocument().vdocument, _data);
		if (!_data->data().isEmpty()) {
			Local::writeStickerImage(_data->mediaKey(), _data->data());
		}
	}
}

bool HistorySticker::needReSetInlineResultMedia(const MTPMessageMedia &media) {
	return needReSetInlineResultDocument(media, _data);
}

int HistorySticker::additionalWidth(const HistoryMessageVia *via, const HistoryMessageReply *reply) const {
	int result = 0;
	if (via) {
		accumulate_max(result, st::msgReplyPadding.left() + st::msgReplyPadding.left() + via->_maxWidth + st::msgReplyPadding.left());
	}
	if (reply) {
		accumulate_max(result, st::msgReplyPadding.left() + reply->replyToWidth());
	}
	return result;
}

void SendMessageClickHandler::onClickImpl() const {
	Ui::showPeerHistory(peer()->id, ShowAtUnreadMsgId);
}

void AddContactClickHandler::onClickImpl() const {
	if (HistoryItem *item = App::histItemById(peerToChannel(peer()), msgid())) {
		if (HistoryMedia *media = item->getMedia()) {
			if (media->type() == MediaTypeContact) {
				QString fname = static_cast<HistoryContact*>(media)->fname();
				QString lname = static_cast<HistoryContact*>(media)->lname();
				QString phone = static_cast<HistoryContact*>(media)->phone();
				Ui::showLayer(new AddContactBox(fname, lname, phone));
			}
		}
	}
}

HistoryContact::HistoryContact(HistoryItem *parent, int32 userId, const QString &first, const QString &last, const QString &phone) : HistoryMedia(parent)
, _userId(userId)
, _contact(0)
, _phonew(0)
, _fname(first)
, _lname(last)
, _phone(App::formatPhone(phone))
, _linkw(0) {
	_name.setText(st::semiboldFont, lng_full_name(lt_first_name, first, lt_last_name, last).trimmed(), _textNameOptions);

	_phonew = st::normalFont->width(_phone);
}

void HistoryContact::initDimensions() {
	_maxw = st::msgFileMinWidth;

	_contact = _userId ? App::userLoaded(_userId) : 0;
	if (_contact) {
		_contact->loadUserpic();
	}
	if (_contact && _contact->contact > 0) {
		_linkl.reset(new SendMessageClickHandler(_contact));
		_link = lang(lng_profile_send_message).toUpper();
	} else if (_userId) {
		_linkl.reset(new AddContactClickHandler(_parent->history()->peer->id, _parent->id));
		_link = lang(lng_profile_add_contact).toUpper();
	}
	_linkw = _link.isEmpty() ? 0 : st::semiboldFont->width(_link);

	int32 tleft = 0, tright = 0;
	if (_userId) {
		tleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		tright = st::msgFileThumbPadding.left();
		_maxw = qMax(_maxw, tleft + _phonew + tright);
	} else {
		tleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
		tright = st::msgFileThumbPadding.left();
		_maxw = qMax(_maxw, tleft + _phonew + _parent->skipBlockWidth() + st::msgPadding.right());
	}

	_maxw = qMax(tleft + _name.maxWidth() + tright, _maxw);
	_maxw = qMin(_maxw, int(st::msgMaxWidth));

	if (_userId) {
		_minh = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom();
		if (_parent->Has<HistoryMessageSigned>()) {
			_minh += st::msgDateFont->height - st::msgDateDelta.y();
		}
	} else {
		_minh = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom();
	}
	_height = _minh;
}

void HistoryContact::draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	int32 skipx = 0, skipy = 0, width = _width, height = _height;

	bool out = _parent->out(), isPost = _parent->isPost(), outbg = out && !isPost;
	bool selected = (selection == FullSelection);

	if (width >= _maxw) {
		width = _maxw;
	}

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0;
	if (_userId) {
		nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		nametop = st::msgFileThumbNameTop;
		nameright = st::msgFileThumbPadding.left();
		statustop = st::msgFileThumbStatusTop;
		linktop = st::msgFileThumbLinkTop;

		QRect rthumb(rtlrect(st::msgFileThumbPadding.left(), st::msgFileThumbPadding.top(), st::msgFileThumbSize, st::msgFileThumbSize, width));
		if (_contact) {
			_contact->paintUserpic(p, st::msgFileThumbSize, rthumb.x(), rthumb.y());
		} else {
			p.drawPixmap(rthumb.topLeft(), userDefPhoto(qAbs(_userId) % UserColorsCount)->pixCircled(st::msgFileThumbSize, st::msgFileThumbSize));
		}
		if (selected) {
			App::roundRect(p, rthumb, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
		}

		bool over = ClickHandler::showAsActive(_linkl);
		p.setFont(over ? st::semiboldFont->underline() : st::semiboldFont);
		p.setPen(outbg ? (selected ? st::msgFileThumbLinkOutFgSelected : st::msgFileThumbLinkOutFg) : (selected ? st::msgFileThumbLinkInFgSelected : st::msgFileThumbLinkInFg));
		p.drawTextLeft(nameleft, linktop, width, _link, _linkw);
	} else {
		nameleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
		nametop = st::msgFileNameTop;
		nameright = st::msgFilePadding.left();
		statustop = st::msgFileStatusTop;

		QRect inner(rtlrect(st::msgFilePadding.left(), st::msgFilePadding.top(), st::msgFileSize, st::msgFileSize, width));
		p.drawPixmap(inner.topLeft(), userDefPhoto(qAbs(_parent->id) % UserColorsCount)->pixCircled(st::msgFileSize, st::msgFileSize));
	}
	int32 namewidth = width - nameleft - nameright;

	p.setFont(st::semiboldFont);
	p.setPen(st::black);
	_name.drawLeftElided(p, nameleft, nametop, namewidth, width);

	style::color status(outbg ? (selected ? st::mediaOutFgSelected : st::mediaOutFg) : (selected ? st::mediaInFgSelected : st::mediaInFg));
	p.setFont(st::normalFont);
	p.setPen(status);
	p.drawTextLeft(nameleft, statustop, width, _phone);
}

HistoryTextState HistoryContact::getState(int x, int y, HistoryStateRequest request) const {
	HistoryTextState result;
	bool out = _parent->out(), isPost = _parent->isPost(), outbg = out && !isPost;

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0;
	if (_userId) {
		nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		linktop = st::msgFileThumbLinkTop;
		if (rtlrect(nameleft, linktop, _linkw, st::semiboldFont->height, _width).contains(x, y)) {
			result.link = _linkl;
			return result;
		}
	}
	if (x >= 0 && y >= 0 && x < _width && y < _height && _contact) {
		result.link = _contact->openLink();
		return result;
	}
	return result;
}

QString HistoryContact::inDialogsText() const {
	return lang(lng_in_dlg_contact);
}

TextWithEntities HistoryContact::selectedText(TextSelection selection) const {
	if (selection != FullSelection) {
		return TextWithEntities();
	}
	return { qsl("[ ") + lang(lng_in_dlg_contact) + qsl(" ]\n") + _name.originalText() + '\n' + _phone, EntitiesInText() };
}

void HistoryContact::attachToParent() {
	if (_userId) {
		App::regSharedContactItem(_userId, _parent);
	}
}

void HistoryContact::detachFromParent() {
	if (_userId) {
		App::unregSharedContactItem(_userId, _parent);
	}
}

void HistoryContact::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaContact) {
		if (_userId != media.c_messageMediaContact().vuser_id.v) {
			detachFromParent();
			_userId = media.c_messageMediaContact().vuser_id.v;
			attachToParent();
		}
	}
}

namespace {
	QString siteNameFromUrl(const QString &url) {
		QUrl u(url);
		QString pretty = u.isValid() ? u.toDisplayString() : url;
		QRegularExpressionMatch m = QRegularExpression(qsl("^[a-zA-Z0-9]+://")).match(pretty);
		if (m.hasMatch()) pretty = pretty.mid(m.capturedLength());
		int32 slash = pretty.indexOf('/');
		if (slash > 0) pretty = pretty.mid(0, slash);
		QStringList components = pretty.split('.', QString::SkipEmptyParts);
		if (components.size() >= 2) {
			components = components.mid(components.size() - 2);
			return components.at(0).at(0).toUpper() + components.at(0).mid(1) + '.' + components.at(1);
		}
		return QString();
	}

	int32 articleThumbWidth(PhotoData *thumb, int32 height) {
		int32 w = thumb->medium->width(), h = thumb->medium->height();
		return qMax(qMin(height * w / h, height), 1);
	}

	int32 articleThumbHeight(PhotoData *thumb, int32 width) {
		return qMax(thumb->medium->height() * width / thumb->medium->width(), 1);
	}

	int32 _lineHeight = 0;
}

HistoryWebPage::HistoryWebPage(HistoryItem *parent, WebPageData *data) : HistoryMedia(parent)
, _data(data)
, _openl(0)
, _attach(nullptr)
, _asArticle(false)
, _title(st::msgMinWidth - st::webPageLeft)
, _description(st::msgMinWidth - st::webPageLeft)
, _siteNameWidth(0)
, _durationWidth(0)
, _pixw(0)
, _pixh(0) {
}

HistoryWebPage::HistoryWebPage(HistoryItem *parent, const HistoryWebPage &other) : HistoryMedia(parent)
, _data(other._data)
, _openl(0)
, _attach(other._attach ? other._attach->clone(parent) : nullptr)
, _asArticle(other._asArticle)
, _title(other._title)
, _description(other._description)
, _siteNameWidth(other._siteNameWidth)
, _durationWidth(other._durationWidth)
, _pixw(other._pixw)
, _pixh(other._pixh) {
}

void HistoryWebPage::initDimensions() {
	if (_data->pendingTill) {
		_maxw = _minh = _height = 0;
		return;
	}
	if (!_lineHeight) _lineHeight = qMax(st::webPageTitleFont->height, st::webPageDescriptionFont->height);

	if (!_openl && !_data->url.isEmpty()) _openl.reset(new UrlClickHandler(_data->url, true));

	// init layout
	QString title(_data->title.isEmpty() ? _data->author : _data->title);
	if (!_data->description.isEmpty() && title.isEmpty() && _data->siteName.isEmpty() && !_data->url.isEmpty()) {
		_data->siteName = siteNameFromUrl(_data->url);
	}
	if (!_data->document && _data->photo && _data->type != WebPagePhoto && _data->type != WebPageVideo) {
		if (_data->type == WebPageProfile) {
			_asArticle = true;
		} else if (_data->siteName == qstr("Twitter") || _data->siteName == qstr("Facebook")) {
			_asArticle = false;
		} else {
			_asArticle = true;
		}
		if (_asArticle && _data->description.isEmpty() && title.isEmpty() && _data->siteName.isEmpty()) {
			_asArticle = false;
		}
	} else {
		_asArticle = false;
	}

	// init attach
	if (!_asArticle && !_attach) {
		if (_data->document) {
			if (_data->document->sticker()) {
				_attach = new HistorySticker(_parent, _data->document);
			} else if (_data->document->isAnimation()) {
				_attach = new HistoryGif(_parent, _data->document, QString());
			} else if (_data->document->isVideo()) {
				_attach = new HistoryVideo(_parent, _data->document, QString());
			} else {
				_attach = new HistoryDocument(_parent, _data->document, QString());
			}
		} else if (_data->photo) {
			_attach = new HistoryPhoto(_parent, _data->photo, QString());
		}
	}

	// init strings
	if (_description.isEmpty() && !_data->description.isEmpty()) {
		QString text = textClean(_data->description);
		if (text.isEmpty()) {
			_data->description = QString();
		} else {
			if (!_asArticle && !_attach) {
				text += _parent->skipBlock();
			}
			const TextParseOptions *opts = &_webpageDescriptionOptions;
			if (_data->siteName == qstr("Twitter")) {
				opts = &_twitterDescriptionOptions;
			} else if (_data->siteName == qstr("Instagram")) {
				opts = &_instagramDescriptionOptions;
			}
			_description.setText(st::webPageDescriptionFont, text, *opts);
		}
	}
	if (_title.isEmpty() && !title.isEmpty()) {
		title = textOneLine(textClean(title));
		if (title.isEmpty()) {
			if (_data->title.isEmpty()) {
				_data->author = QString();
			} else {
				_data->title = QString();
			}
		} else {
			if (!_asArticle && !_attach && _description.isEmpty()) {
				title += _parent->skipBlock();
			}
			_title.setText(st::webPageTitleFont, title, _webpageTitleOptions);
		}
	}
	if (!_siteNameWidth && !_data->siteName.isEmpty()) {
		_siteNameWidth = st::webPageTitleFont->width(_data->siteName);
	}

	// init dimensions
	int32 l = st::msgPadding.left() + st::webPageLeft, r = st::msgPadding.right();
	int32 skipBlockWidth = _parent->skipBlockWidth();
	_maxw = skipBlockWidth;
	_minh = 0;

	int32 siteNameHeight = _data->siteName.isEmpty() ? 0 : _lineHeight;
	int32 titleMinHeight = _title.isEmpty() ? 0 : _lineHeight;
	int32 descMaxLines = (3 + (siteNameHeight ? 0 : 1) + (titleMinHeight ? 0 : 1));
	int32 descriptionMinHeight = _description.isEmpty() ? 0 : qMin(_description.minHeight(), descMaxLines * _lineHeight);
	int32 articleMinHeight = siteNameHeight + titleMinHeight + descriptionMinHeight;
	int32 articlePhotoMaxWidth = 0;
	if (_asArticle) {
		articlePhotoMaxWidth = st::webPagePhotoDelta + qMax(articleThumbWidth(_data->photo, articleMinHeight), _lineHeight);
	}

	if (_siteNameWidth) {
		if (_title.isEmpty() && _description.isEmpty()) {
			_maxw = qMax(_maxw, int32(_siteNameWidth + _parent->skipBlockWidth()));
		} else {
			_maxw = qMax(_maxw, int32(_siteNameWidth + articlePhotoMaxWidth));
		}
		_minh += _lineHeight;
	}
	if (!_title.isEmpty()) {
		_maxw = qMax(_maxw, int32(_title.maxWidth() + articlePhotoMaxWidth));
		_minh += titleMinHeight;
	}
	if (!_description.isEmpty()) {
		_maxw = qMax(_maxw, int32(_description.maxWidth() + articlePhotoMaxWidth));
		_minh += descriptionMinHeight;
	}
	if (_attach) {
		if (_minh) _minh += st::webPagePhotoSkip;
		_attach->initDimensions();
		QMargins bubble(_attach->bubbleMargins());
		_maxw = qMax(_maxw, int32(_attach->maxWidth() - bubble.left() - bubble.top() + (_attach->customInfoLayout() ? skipBlockWidth : 0)));
		_minh += _attach->minHeight() - bubble.top() - bubble.bottom();
	}
	if (_data->type == WebPageVideo && _data->duration) {
		_duration = formatDurationText(_data->duration);
		_durationWidth = st::msgDateFont->width(_duration);
	}
	_maxw += st::msgPadding.left() + st::webPageLeft + st::msgPadding.right();
	_minh += st::msgPadding.bottom();
	if (_asArticle) {
		_minh = resizeGetHeight(_maxw); // hack
//		_minh += st::msgDateFont->height;
	}
}

int HistoryWebPage::resizeGetHeight(int width) {
	if (_data->pendingTill) {
		_width = width;
		_height = _minh;
		return _height;
	}

	_width = qMin(width, _maxw);
	width -= st::msgPadding.left() + st::webPageLeft + st::msgPadding.right();

	int32 linesMax = 5;
	int32 siteNameLines = _siteNameWidth ? 1 : 0, siteNameHeight = _siteNameWidth ? _lineHeight : 0;
	if (_asArticle) {
		_pixh = linesMax * _lineHeight;
		do {
			_pixw = articleThumbWidth(_data->photo, _pixh);
			int32 wleft = width - st::webPagePhotoDelta - qMax(_pixw, int16(_lineHeight));

			_height = siteNameHeight;

			if (_title.isEmpty()) {
				_titleLines = 0;
			} else {
				if (_title.countHeight(wleft) < 2 * st::webPageTitleFont->height) {
					_titleLines = 1;
				} else {
					_titleLines = 2;
				}
				_height += _titleLines * _lineHeight;
			}

			int32 descriptionHeight = _description.countHeight(wleft);
			if (descriptionHeight < (linesMax - siteNameLines - _titleLines) * st::webPageDescriptionFont->height) {
				_descriptionLines = (descriptionHeight / st::webPageDescriptionFont->height);
			} else {
				_descriptionLines = (linesMax - siteNameLines - _titleLines);
			}
			_height += _descriptionLines * _lineHeight;

			if (_height >= _pixh) {
				break;
			}

			_pixh -= _lineHeight;
		} while (_pixh > _lineHeight);
		_height += st::msgDateFont->height;
	} else {
		_height = siteNameHeight;

		if (_title.isEmpty()) {
			_titleLines = 0;
		} else {
			if (_title.countHeight(width) < 2 * st::webPageTitleFont->height) {
				_titleLines = 1;
			} else {
				_titleLines = 2;
			}
			_height += _titleLines * _lineHeight;
		}

		if (_description.isEmpty()) {
			_descriptionLines = 0;
		} else {
			int32 descriptionHeight = _description.countHeight(width);
			if (descriptionHeight < (linesMax - siteNameLines - _titleLines) * st::webPageDescriptionFont->height) {
				_descriptionLines = (descriptionHeight / st::webPageDescriptionFont->height);
			} else {
				_descriptionLines = (linesMax - siteNameLines - _titleLines);
			}
			_height += _descriptionLines * _lineHeight;
		}

		if (_attach) {
			if (_height) _height += st::webPagePhotoSkip;

			QMargins bubble(_attach->bubbleMargins());

			_attach->resizeGetHeight(width + bubble.left() + bubble.right());
			_height += _attach->height() - bubble.top() - bubble.bottom();
			if (_attach->customInfoLayout() && _attach->currentWidth() + _parent->skipBlockWidth() > width + bubble.left() + bubble.right()) {
				_height += st::msgDateFont->height;
			}
		}
	}
	_height += st::msgPadding.bottom();

	return _height;
}

void HistoryWebPage::draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	int32 skipx = 0, skipy = 0, width = _width, height = _height;

	bool out = _parent->out(), isPost = _parent->isPost(), outbg = out && !isPost;
	bool selected = (selection == FullSelection);

	style::color barfg = (selected ? (outbg ? st::msgOutReplyBarSelColor : st::msgInReplyBarSelColor) : (outbg ? st::msgOutReplyBarColor : st::msgInReplyBarColor));
	style::color semibold = (selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg));
	style::color regular = (selected ? (outbg ? st::msgOutDateFgSelected : st::msgInDateFgSelected) : (outbg ? st::msgOutDateFg : st::msgInDateFg));

	int32 lshift = st::msgPadding.left() + st::webPageLeft, rshift = st::msgPadding.right(), bshift = st::msgPadding.bottom();
	width -= lshift + rshift;
	QMargins bubble(_attach ? _attach->bubbleMargins() : QMargins());
	if (_asArticle || (_attach && _attach->customInfoLayout() && _attach->currentWidth() + _parent->skipBlockWidth() > width + bubble.left() + bubble.right())) {
		bshift += st::msgDateFont->height;
	}

	QRect bar(rtlrect(st::msgPadding.left(), 0, st::webPageBar, _height - bshift, _width));
	p.fillRect(bar, barfg);

	if (_asArticle) {
		_data->photo->medium->load(false, false);
		bool full = _data->photo->medium->loaded();
		QPixmap pix;
		int32 pw = qMax(_pixw, int16(_lineHeight)), ph = _pixh;
		int32 pixw = _pixw, pixh = articleThumbHeight(_data->photo, _pixw);
		int32 maxw = convertScale(_data->photo->medium->width()), maxh = convertScale(_data->photo->medium->height());
		if (pixw * ph != pixh * pw) {
			float64 coef = (pixw * ph > pixh * pw) ? qMin(ph / float64(pixh), maxh / float64(pixh)) : qMin(pw / float64(pixw), maxw / float64(pixw));
			pixh = qRound(pixh * coef);
			pixw = qRound(pixw * coef);
		}
		if (full) {
			pix = _data->photo->medium->pixSingle(pixw, pixh, pw, ph);
		} else {
			pix = _data->photo->thumb->pixBlurredSingle(pixw, pixh, pw, ph);
		}
		p.drawPixmapLeft(lshift + width - pw, 0, _width, pix);
		if (selected) {
			App::roundRect(p, rtlrect(lshift + width - pw, 0, pw, _pixh, _width), textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
		}
		width -= pw + st::webPagePhotoDelta;
	}
	int32 tshift = 0;
	if (_siteNameWidth) {
		p.setFont(st::webPageTitleFont);
		p.setPen(semibold);
		p.drawTextLeft(lshift, tshift, _width, (width >= _siteNameWidth) ? _data->siteName : st::webPageTitleFont->elided(_data->siteName, width));
		tshift += _lineHeight;
	}
	if (_titleLines) {
		p.setPen(st::black);
		int32 endskip = 0;
		if (_title.hasSkipBlock()) {
			endskip = _parent->skipBlockWidth();
		}
		_title.drawLeftElided(p, lshift, tshift, width, _width, _titleLines, style::al_left, 0, -1, endskip, false, selection);
		tshift += _titleLines * _lineHeight;
	}
	if (_descriptionLines) {
		p.setPen(st::black);
		int32 endskip = 0;
		if (_description.hasSkipBlock()) {
			endskip = _parent->skipBlockWidth();
		}
		_description.drawLeftElided(p, lshift, tshift, width, _width, _descriptionLines, style::al_left, 0, -1, endskip, false, toDescriptionSelection(selection));
		tshift += _descriptionLines * _lineHeight;
	}
	if (_attach) {
		if (tshift) tshift += st::webPagePhotoSkip;

		int32 attachLeft = lshift - bubble.left(), attachTop = tshift - bubble.top();
		if (rtl()) attachLeft = _width - attachLeft - _attach->currentWidth();

		p.save();
		p.translate(attachLeft, attachTop);

		auto attachSelection = selected ? FullSelection : TextSelection{ 0, 0 };
		_attach->draw(p, r.translated(-attachLeft, -attachTop), attachSelection, ms);
		int32 pixwidth = _attach->currentWidth(), pixheight = _attach->height();

		if (_data->type == WebPageVideo && _attach->type() == MediaTypePhoto) {
			if (_data->siteName == qstr("YouTube")) {
				p.drawSprite(QPoint((pixwidth - st::youtubeIcon.pxWidth()) / 2, (pixheight - st::youtubeIcon.pxHeight()) / 2), st::youtubeIcon);
			} else {
				p.drawSprite(QPoint((pixwidth - st::videoIcon.pxWidth()) / 2, (pixheight - st::videoIcon.pxHeight()) / 2), st::videoIcon);
			}
			if (_durationWidth) {
				int32 dateX = pixwidth - _durationWidth - st::msgDateImgDelta - 2 * st::msgDateImgPadding.x();
				int32 dateY = pixheight - st::msgDateFont->height - 2 * st::msgDateImgPadding.y() - st::msgDateImgDelta;
				int32 dateW = pixwidth - dateX - st::msgDateImgDelta;
				int32 dateH = pixheight - dateY - st::msgDateImgDelta;

				App::roundRect(p, dateX, dateY, dateW, dateH, selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);

				p.setFont(st::msgDateFont);
				p.setPen(st::msgDateImgColor);
				p.drawTextLeft(dateX + st::msgDateImgPadding.x(), dateY + st::msgDateImgPadding.y(), pixwidth, _duration);
			}
		}

		p.restore();
	}
}

HistoryTextState HistoryWebPage::getState(int x, int y, HistoryStateRequest request) const {
	HistoryTextState result;

	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return result;
	int32 skipx = 0, skipy = 0, width = _width, height = _height;

	int32 lshift = st::msgPadding.left() + st::webPageLeft, rshift = st::msgPadding.right(), bshift = st::msgPadding.bottom();
	width -= lshift + rshift;
	QMargins bubble(_attach ? _attach->bubbleMargins() : QMargins());
	if (_asArticle || (_attach && _attach->customInfoLayout() && _attach->currentWidth() + _parent->skipBlockWidth() > width + bubble.left() + bubble.right())) {
		bshift += st::msgDateFont->height;
	}

	bool inThumb = false;
	if (_asArticle) {
		int32 pw = qMax(_pixw, int16(_lineHeight));
		if (rtlrect(lshift + width - pw, 0, pw, _pixh, _width).contains(x, y)) {
			inThumb = true;
		}
		width -= pw + st::webPagePhotoDelta;
	}
	int tshift = 0, symbolAdd = 0;
	if (_siteNameWidth) {
		tshift += _lineHeight;
	}
	if (_titleLines) {
		if (y >= tshift && y < tshift + _titleLines * _lineHeight) {
			Text::StateRequestElided titleRequest = request.forText();
			titleRequest.lines = _titleLines;
			result = _title.getStateElidedLeft(x - lshift, y - tshift, width, _width, titleRequest);
		} else if (y >= tshift + _titleLines * _lineHeight) {
			symbolAdd += _title.length();
		}
		tshift += _titleLines * _lineHeight;
	}
	if (_descriptionLines) {
		if (y >= tshift && y < tshift + _descriptionLines * _lineHeight) {
			Text::StateRequestElided descriptionRequest = request.forText();
			descriptionRequest.lines = _descriptionLines;
			result = _description.getStateElidedLeft(x - lshift, y - tshift, width, _width, descriptionRequest);
		} else if (y >= tshift + _descriptionLines * _lineHeight) {
			symbolAdd += _description.length();
		}
		tshift += _descriptionLines * _lineHeight;
	}
	if (inThumb) {
		result.link = _openl;
	} else if (_attach) {
		if (tshift) tshift += st::webPagePhotoSkip;

		if (x >= lshift && x < lshift + width && y >= tshift && y < _height - st::msgPadding.bottom()) {
			int32 attachLeft = lshift - bubble.left(), attachTop = tshift - bubble.top();
			if (rtl()) attachLeft = _width - attachLeft - _attach->currentWidth();
			result = _attach->getState(x - attachLeft, y - attachTop, request);

			if (result.link && !_data->document && _data->photo) {
				if (_data->type == WebPageProfile || _data->type == WebPageVideo) {
					result.link = _openl;
				} else if (_data->type == WebPagePhoto || _data->siteName == qstr("Twitter") || _data->siteName == qstr("Facebook")) {
					// leave photo link
				} else {
					result.link = _openl;
				}
			}
		}
	}

	result.symbol += symbolAdd;
	return result;
}

TextSelection HistoryWebPage::adjustSelection(TextSelection selection, TextSelectType type) const {
	if (!_descriptionLines || selection.to <= _title.length()) {
		return _title.adjustSelection(selection, type);
	}
	auto descriptionSelection = _description.adjustSelection(toDescriptionSelection(selection), type);
	if (selection.from >= _title.length()) {
		return fromDescriptionSelection(descriptionSelection);
	}
	auto titleSelection = _title.adjustSelection(selection, type);
	return { titleSelection.from, fromDescriptionSelection(descriptionSelection).to };
}

void HistoryWebPage::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (_attach) {
		_attach->clickHandlerActiveChanged(p, active);
	}
}

void HistoryWebPage::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	if (_attach) {
		_attach->clickHandlerPressedChanged(p, pressed);
	}
}

void HistoryWebPage::attachToParent() {
	App::regWebPageItem(_data, _parent);
	if (_attach) _attach->attachToParent();
}

void HistoryWebPage::detachFromParent() {
	App::unregWebPageItem(_data, _parent);
	if (_attach) _attach->detachFromParent();
}

QString HistoryWebPage::inDialogsText() const {
	return QString();
}

TextWithEntities HistoryWebPage::selectedText(TextSelection selection) const {
	if (selection == FullSelection) {
		return TextWithEntities();
	}
	auto titleResult = _title.originalTextWithEntities(selection, ExpandLinksAll);
	auto descriptionResult = _description.originalTextWithEntities(toDescriptionSelection(selection), ExpandLinksAll);
	if (titleResult.text.isEmpty()) {
		return descriptionResult;
	} else if (descriptionResult.text.isEmpty()) {
		return titleResult;
	}

	titleResult.text += '\n';
	appendTextWithEntities(titleResult, std_::move(descriptionResult));
	return titleResult;
}

ImagePtr HistoryWebPage::replyPreview() {
	return _attach ? _attach->replyPreview() : (_data->photo ? _data->photo->makeReplyPreview() : ImagePtr());
}

HistoryWebPage::~HistoryWebPage() {
	deleteAndMark(_attach);
}

namespace {
	LocationManager manager;
}

void LocationManager::init() {
	if (manager) delete manager;
	manager = new QNetworkAccessManager();
	App::setProxySettings(*manager);

	connect(manager, SIGNAL(authenticationRequired(QNetworkReply*, QAuthenticator*)), this, SLOT(onFailed(QNetworkReply*)));
	connect(manager, SIGNAL(sslErrors(QNetworkReply*, const QList<QSslError>&)), this, SLOT(onFailed(QNetworkReply*)));
	connect(manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(onFinished(QNetworkReply*)));

	if (black) delete black;
	QImage b(cIntRetinaFactor(), cIntRetinaFactor(), QImage::Format_ARGB32_Premultiplied);
	{
		QPainter p(&b);
		p.fillRect(QRect(0, 0, cIntRetinaFactor(), cIntRetinaFactor()), st::white->b);
	}
	QPixmap p = QPixmap::fromImage(b, Qt::ColorOnly);
	p.setDevicePixelRatio(cRetinaFactor());
	black = new ImagePtr(p, "PNG");
}

void LocationManager::reinit() {
	if (manager) App::setProxySettings(*manager);
}

void LocationManager::deinit() {
	if (manager) {
		delete manager;
		manager = 0;
	}
	if (black) {
		delete black;
		black = 0;
	}
	dataLoadings.clear();
	imageLoadings.clear();
}

void initImageLinkManager() {
	manager.init();
}

void reinitImageLinkManager() {
	manager.reinit();
}

void deinitImageLinkManager() {
	manager.deinit();
}

void LocationManager::getData(LocationData *data) {
	if (!manager) {
		DEBUG_LOG(("App Error: getting image link data without manager init!"));
		return failed(data);
	}

	int32 w = st::locationSize.width(), h = st::locationSize.height();
	int32 zoom = 13, scale = 1;
	if (cScale() == dbisTwo || cRetina()) {
		scale = 2;
	} else {
		w = convertScale(w);
		h = convertScale(h);
	}
	QString coords = qsl("%1,%2").arg(data->coords.lat).arg(data->coords.lon);
	QString url = qsl("https://maps.googleapis.com/maps/api/staticmap?center=") + coords + qsl("&zoom=%1&size=%2x%3&maptype=roadmap&scale=%4&markers=color:red|size:big|").arg(zoom).arg(w).arg(h).arg(scale) + coords + qsl("&sensor=false");
	QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(url)));
	imageLoadings[reply] = data;
}

void LocationManager::onFinished(QNetworkReply *reply) {
	if (!manager) return;
	if (reply->error() != QNetworkReply::NoError) return onFailed(reply);

	QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
	if (statusCode.isValid()) {
		int status = statusCode.toInt();
		if (status == 301 || status == 302) {
			QString loc = reply->header(QNetworkRequest::LocationHeader).toString();
			if (!loc.isEmpty()) {
				QMap<QNetworkReply*, LocationData*>::iterator i = dataLoadings.find(reply);
				if (i != dataLoadings.cend()) {
					LocationData *d = i.value();
					if (serverRedirects.constFind(d) == serverRedirects.cend()) {
						serverRedirects.insert(d, 1);
					} else if (++serverRedirects[d] > MaxHttpRedirects) {
						DEBUG_LOG(("Network Error: Too many HTTP redirects in onFinished() for image link: %1").arg(loc));
						return onFailed(reply);
					}
					dataLoadings.erase(i);
					dataLoadings.insert(manager->get(QNetworkRequest(loc)), d);
					return;
				} else if ((i = imageLoadings.find(reply)) != imageLoadings.cend()) {
					LocationData *d = i.value();
					if (serverRedirects.constFind(d) == serverRedirects.cend()) {
						serverRedirects.insert(d, 1);
					} else if (++serverRedirects[d] > MaxHttpRedirects) {
						DEBUG_LOG(("Network Error: Too many HTTP redirects in onFinished() for image link: %1").arg(loc));
						return onFailed(reply);
					}
					imageLoadings.erase(i);
					imageLoadings.insert(manager->get(QNetworkRequest(loc)), d);
					return;
				}
			}
		}
		if (status != 200) {
			DEBUG_LOG(("Network Error: Bad HTTP status received in onFinished() for image link: %1").arg(status));
			return onFailed(reply);
		}
	}

	LocationData *d = 0;
	QMap<QNetworkReply*, LocationData*>::iterator i = dataLoadings.find(reply);
	if (i != dataLoadings.cend()) {
		d = i.value();
		dataLoadings.erase(i);

		QJsonParseError e;
		QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &e);
		if (e.error != QJsonParseError::NoError) {
			DEBUG_LOG(("JSON Error: Bad json received in onFinished() for image link"));
			return onFailed(reply);
		}
		failed(d);

		if (App::main()) App::main()->update();
	} else {
		i = imageLoadings.find(reply);
		if (i != imageLoadings.cend()) {
			d = i.value();
			imageLoadings.erase(i);

			QPixmap thumb;
			QByteArray format;
			QByteArray data(reply->readAll());
			{
				QBuffer buffer(&data);
				QImageReader reader(&buffer);
#if QT_VERSION >= QT_VERSION_CHECK(5, 5, 0)
				reader.setAutoTransform(true);
#endif
				thumb = QPixmap::fromImageReader(&reader, Qt::ColorOnly);
				format = reader.format();
				thumb.setDevicePixelRatio(cRetinaFactor());
				if (format.isEmpty()) format = QByteArray("JPG");
			}
			d->loading = false;
			d->thumb = thumb.isNull() ? (*black) : ImagePtr(thumb, format);
			serverRedirects.remove(d);
			if (App::main()) App::main()->update();
		}
	}
}

void LocationManager::onFailed(QNetworkReply *reply) {
	if (!manager) return;

	LocationData *d = 0;
	QMap<QNetworkReply*, LocationData*>::iterator i = dataLoadings.find(reply);
	if (i != dataLoadings.cend()) {
		d = i.value();
		dataLoadings.erase(i);
	} else {
		i = imageLoadings.find(reply);
		if (i != imageLoadings.cend()) {
			d = i.value();
			imageLoadings.erase(i);
		}
	}
	DEBUG_LOG(("Network Error: failed to get data for image link %1,%2 error %3").arg(d ? d->coords.lat : 0).arg(d ? d->coords.lon : 0).arg(reply->errorString()));
	if (d) {
		failed(d);
	}
}

void LocationManager::failed(LocationData *data) {
	data->loading = false;
	data->thumb = *black;
	serverRedirects.remove(data);
}

void LocationData::load() {
	if (!thumb->isNull()) return thumb->load(false, false);
	if (loading) return;

	loading = true;
	manager.getData(this);
}

HistoryLocation::HistoryLocation(HistoryItem *parent, const LocationCoords &coords, const QString &title, const QString &description) : HistoryMedia(parent)
, _data(App::location(coords))
, _title(st::msgMinWidth)
, _description(st::msgMinWidth)
, _link(new LocationClickHandler(coords)) {
	if (!title.isEmpty()) {
		_title.setText(st::webPageTitleFont, textClean(title), _webpageTitleOptions);
	}
	if (!description.isEmpty()) {
		_description.setText(st::webPageDescriptionFont, textClean(description), _webpageDescriptionOptions);
	}
}

HistoryLocation::HistoryLocation(HistoryItem *parent, const HistoryLocation &other) : HistoryMedia(parent)
, _data(other._data)
, _title(other._title)
, _description(other._description)
, _link(new LocationClickHandler(_data->coords)) {
}

void HistoryLocation::initDimensions() {
	bool bubble = _parent->hasBubble();

	int32 tw = fullWidth(), th = fullHeight();
	if (tw > st::maxMediaSize) {
		th = (st::maxMediaSize * th) / tw;
		tw = st::maxMediaSize;
	}
	int32 minWidth = qMax(st::minPhotoSize, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	_maxw = qMax(tw, int32(minWidth));
	_minh = qMax(th, int32(st::minPhotoSize));

	if (bubble) {
		_maxw += st::mediaPadding.left() + st::mediaPadding.right();
		if (!_title.isEmpty()) {
			_minh += qMin(_title.countHeight(_maxw - st::msgPadding.left() - st::msgPadding.right()), 2 * st::webPageTitleFont->height);
		}
		if (!_description.isEmpty()) {
			_maxw = qMax(_maxw, int32(st::msgPadding.left() + _description.maxWidth() + st::msgPadding.right()));
			_minh += qMin(_description.countHeight(_maxw - st::msgPadding.left() - st::msgPadding.right()), 3 * st::webPageDescriptionFont->height);
		}
		_minh += st::mediaPadding.top() + st::mediaPadding.bottom();
		if (!_title.isEmpty() || !_description.isEmpty()) {
			_minh += st::webPagePhotoSkip;
			if (!_parent->Has<HistoryMessageForwarded>() && !_parent->Has<HistoryMessageReply>()) {
				_minh += st::msgPadding.top();
			}
		}
	}
}

int HistoryLocation::resizeGetHeight(int width) {
	bool bubble = _parent->hasBubble();

	_width = qMin(width, _maxw);
	if (bubble) {
		_width -= st::mediaPadding.left() + st::mediaPadding.right();
	}

	int32 tw = fullWidth(), th = fullHeight();
	if (tw > st::maxMediaSize) {
		th = (st::maxMediaSize * th) / tw;
		tw = st::maxMediaSize;
	}
	_height = th;
	if (tw > _width) {
		_height = (_width * _height / tw);
	} else {
		_width = tw;
	}
	int32 minWidth = qMax(st::minPhotoSize, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	_width = qMax(_width, int32(minWidth));
	_height = qMax(_height, int32(st::minPhotoSize));
	if (bubble) {
		_width += st::mediaPadding.left() + st::mediaPadding.right();
		_height += st::mediaPadding.top() + st::mediaPadding.bottom();
		if (!_title.isEmpty()) {
			_height += qMin(_title.countHeight(_width - st::msgPadding.left() - st::msgPadding.right()), st::webPageTitleFont->height * 2);
		}
		if (!_description.isEmpty()) {
			_height += qMin(_description.countHeight(_width - st::msgPadding.left() - st::msgPadding.right()), st::webPageDescriptionFont->height * 3);
		}
		if (!_title.isEmpty() || !_description.isEmpty()) {
			_height += st::webPagePhotoSkip;
			if (!_parent->Has<HistoryMessageForwarded>() && !_parent->Has<HistoryMessageReply>()) {
				_height += st::msgPadding.top();
			}
		}
	}
	return _height;
}

void HistoryLocation::draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	int32 skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = _parent->hasBubble();
	bool out = _parent->out(), isPost = _parent->isPost(), outbg = out && !isPost;
	bool selected = (selection == FullSelection);

	if (bubble) {
		skipx = st::mediaPadding.left();
		skipy = st::mediaPadding.top();

		if (!_title.isEmpty() || !_description.isEmpty()) {
			if (!_parent->Has<HistoryMessageForwarded>() && !_parent->Has<HistoryMessageReply>()) {
				skipy += st::msgPadding.top();
			}
		}

		width -= st::mediaPadding.left() + st::mediaPadding.right();
		int32 textw = _width - st::msgPadding.left() - st::msgPadding.right();

		p.setPen(st::black);
		if (!_title.isEmpty()) {
			_title.drawLeftElided(p, skipx + st::msgPadding.left(), skipy, textw, _width, 2, style::al_left, 0, -1, 0, false, selection);
			skipy += qMin(_title.countHeight(textw), 2 * st::webPageTitleFont->height);
		}
		if (!_description.isEmpty()) {
			_description.drawLeftElided(p, skipx + st::msgPadding.left(), skipy, textw, _width, 3, style::al_left, 0, -1, 0, false, toDescriptionSelection(selection));
			skipy += qMin(_description.countHeight(textw), 3 * st::webPageDescriptionFont->height);
		}
		if (!_title.isEmpty() || !_description.isEmpty()) {
			skipy += st::webPagePhotoSkip;
		}
		height -= skipy + st::mediaPadding.bottom();
	} else {
		App::roundShadow(p, 0, 0, width, height, selected ? st::msgInShadowSelected : st::msgInShadow, selected ? InSelectedShadowCorners : InShadowCorners);
	}

	_data->load();
	QPixmap toDraw;
	if (_data && !_data->thumb->isNull()) {
		int32 w = _data->thumb->width(), h = _data->thumb->height();
		QPixmap pix;
		if (width * h == height * w || (w == fullWidth() && h == fullHeight())) {
			pix = _data->thumb->pixSingle(width, height, width, height);
		} else if (width * h > height * w) {
			int32 nw = height * w / h;
			pix = _data->thumb->pixSingle(nw, height, width, height);
		} else {
			int32 nh = width * h / w;
			pix = _data->thumb->pixSingle(width, nh, width, height);
		}
		p.drawPixmap(QPoint(skipx, skipy), pix);
	} else {
		App::roundRect(p, skipx, skipy, width, height, st::white, MessageInCorners);
	}
	if (selected) {
		App::roundRect(p, skipx, skipy, width, height, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
	}

	if (_parent->getMedia() == this) {
		int32 fullRight = skipx + width, fullBottom = _height - (skipx ? st::mediaPadding.bottom() : 0);
		_parent->drawInfo(p, fullRight, fullBottom, skipx * 2 + width, selected, InfoDisplayOverImage);
	}
}

HistoryTextState HistoryLocation::getState(int x, int y, HistoryStateRequest request) const {
	HistoryTextState result;
	auto symbolAdd = 0;

	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return result;
	int32 skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = _parent->hasBubble();

	if (bubble) {
		skipx = st::mediaPadding.left();
		skipy = st::mediaPadding.top();

		if (!_title.isEmpty() || !_description.isEmpty()) {
			if (!_parent->Has<HistoryMessageForwarded>() && !_parent->Has<HistoryMessageReply>()) {
				skipy += st::msgPadding.top();
			}
		}

		width -= st::mediaPadding.left() + st::mediaPadding.right();
		int32 textw = _width - st::msgPadding.left() - st::msgPadding.right();

		if (!_title.isEmpty()) {
			auto titleh = qMin(_title.countHeight(textw), 2 * st::webPageTitleFont->height);
			if (y >= skipy && y < skipy + titleh) {
				result = _title.getStateLeft(x - skipx - st::msgPadding.left(), y - skipy, textw, _width, request.forText());
				return result;
			} else if (y >= skipy + titleh) {
				symbolAdd += _title.length();
			}
			skipy += titleh;
		}
		if (!_description.isEmpty()) {
			auto descriptionh = qMin(_description.countHeight(textw), 3 * st::webPageDescriptionFont->height);
			if (y >= skipy && y < skipy + descriptionh) {
				result = _description.getStateLeft(x - skipx - st::msgPadding.left(), y - skipy, textw, _width, request.forText());
			} else if (y >= skipy + descriptionh) {
				symbolAdd += _description.length();
			}
			skipy += descriptionh;
		}
		if (!_title.isEmpty() || !_description.isEmpty()) {
			skipy += st::webPagePhotoSkip;
		}
		height -= skipy + st::mediaPadding.bottom();
	}
	if (x >= skipx && y >= skipy && x < skipx + width && y < skipy + height && _data) {
		result.link = _link;

		int32 fullRight = skipx + width, fullBottom = _height - (skipx ? st::mediaPadding.bottom() : 0);
		bool inDate = _parent->pointInTime(fullRight, fullBottom, x, y, InfoDisplayOverImage);
		if (inDate) {
			result.cursor = HistoryInDateCursorState;
		}
	}
	result.symbol += symbolAdd;
	return result;
}

TextSelection HistoryLocation::adjustSelection(TextSelection selection, TextSelectType type) const {
	if (_description.isEmpty() || selection.to <= _title.length()) {
		return _title.adjustSelection(selection, type);
	}
	auto descriptionSelection = _description.adjustSelection(toDescriptionSelection(selection), type);
	if (selection.from >= _title.length()) {
		return fromDescriptionSelection(descriptionSelection);
	}
	auto titleSelection = _title.adjustSelection(selection, type);
	return { titleSelection.from, fromDescriptionSelection(descriptionSelection).to };
}

QString HistoryLocation::inDialogsText() const {
	return _title.isEmpty() ? lang(lng_maps_point) : _title.originalText(AllTextSelection);
}

TextWithEntities HistoryLocation::selectedText(TextSelection selection) const {
	if (selection == FullSelection) {
		TextWithEntities result = { qsl("[ ") + lang(lng_maps_point) + qsl(" ]\n"), EntitiesInText() };
		auto info = selectedText(AllTextSelection);
		if (!info.text.isEmpty()) {
			appendTextWithEntities(result, std_::move(info));
			result.text.append('\n');
		}
		result.text += _link->dragText();
		return result;
	}

	auto titleResult = _title.originalTextWithEntities(selection);
	auto descriptionResult = _description.originalTextWithEntities(toDescriptionSelection(selection));
	if (titleResult.text.isEmpty()) {
		return descriptionResult;
	} else if (descriptionResult.text.isEmpty()) {
		return titleResult;
	}
	titleResult.text += '\n';
	appendTextWithEntities(titleResult, std_::move(descriptionResult));
	return titleResult;
}

int32 HistoryLocation::fullWidth() const {
	return st::locationSize.width();
}

int32 HistoryLocation::fullHeight() const {
	return st::locationSize.height();
}

void ViaInlineBotClickHandler::onClickImpl() const {
	App::insertBotCommand('@' + _bot->username);
}

void HistoryMessageVia::create(int32 userId) {
	_bot = App::user(peerFromUser(userId));
	_maxWidth = st::msgServiceNameFont->width(lng_inline_bot_via(lt_inline_bot, '@' + _bot->username));
	_lnk.reset(new ViaInlineBotClickHandler(_bot));
}

void HistoryMessageVia::resize(int32 availw) const {
	if (availw < 0) {
		_text = QString();
		_width = 0;
	} else {
		_text = lng_inline_bot_via(lt_inline_bot, '@' + _bot->username);
		if (availw < _maxWidth) {
			_text = st::msgServiceNameFont->elided(_text, availw);
			_width = st::msgServiceNameFont->width(_text);
		} else if (_width < _maxWidth) {
			_width = _maxWidth;
		}
	}
}

void HistoryMessageSigned::create(UserData *from, const QDateTime &date) {
	QString time = qsl(", ") + date.toString(cTimeFormat()), name = App::peerName(from);
	int32 timew = st::msgDateFont->width(time), namew = st::msgDateFont->width(name);
	if (timew + namew > st::maxSignatureSize) {
		name = st::msgDateFont->elided(from->firstName, st::maxSignatureSize - timew);
	}
	_signature.setText(st::msgDateFont, name + time, _textNameOptions);
}

int HistoryMessageSigned::maxWidth() const {
	return _signature.maxWidth();
}

void HistoryMessageEdited::create(const QDateTime &editDate, const QDateTime &date) {
	_editDate = editDate;

	QString time = date.toString(cTimeFormat());
	_edited.setText(st::msgDateFont, lang(lng_edited) + ' ' + time, _textNameOptions);
}

int HistoryMessageEdited::maxWidth() const {
	return _edited.maxWidth();
}

void HistoryMessageForwarded::create(const HistoryMessageVia *via) const {
	QString text;
	if (_authorOriginal != _fromOriginal) {
		text = lng_forwarded_signed(lt_channel, App::peerName(_authorOriginal), lt_user, App::peerName(_fromOriginal));
	} else {
		text = App::peerName(_authorOriginal);
	}
	if (via) {
		if (_authorOriginal->isChannel()) {
			text = lng_forwarded_channel_via(lt_channel, textcmdLink(1, text), lt_inline_bot, textcmdLink(2, '@' + via->_bot->username));
		} else {
			text = lng_forwarded_via(lt_user, textcmdLink(1, text), lt_inline_bot, textcmdLink(2, '@' + via->_bot->username));
		}
	} else {
		if (_authorOriginal->isChannel()) {
			text = lng_forwarded_channel(lt_channel, textcmdLink(1, text));
		} else {
			text = lng_forwarded(lt_user, textcmdLink(1, text));
		}
	}
	TextParseOptions opts = { TextParseRichText, 0, 0, Qt::LayoutDirectionAuto };
	textstyleSet(&st::inFwdTextStyle);
	_text.setText(st::msgServiceNameFont, text, opts);
	textstyleRestore();
	_text.setLink(1, (_originalId && _authorOriginal->isChannel()) ? ClickHandlerPtr(new GoToMessageClickHandler(_authorOriginal->id, _originalId)) : _authorOriginal->openLink());
	if (via) {
		_text.setLink(2, via->_lnk);
	}
}

bool HistoryMessageReply::updateData(HistoryMessage *holder, bool force) {
	if (!force) {
		if (replyToMsg || !replyToMsgId) {
			return true;
		}
	}
	if (!replyToMsg) {
		replyToMsg = App::histItemById(holder->channelId(), replyToMsgId);
		if (replyToMsg) {
			App::historyRegDependency(holder, replyToMsg);
		}
	}

	if (replyToMsg) {
		replyToText.setText(st::msgFont, replyToMsg->inReplyText(), _textDlgOptions);

		updateName();

		replyToLnk.reset(new GoToMessageClickHandler(replyToMsg->history()->peer->id, replyToMsg->id));
		if (!replyToMsg->Has<HistoryMessageForwarded>()) {
			if (UserData *bot = replyToMsg->viaBot()) {
				_replyToVia.reset(new HistoryMessageVia());
				_replyToVia->create(peerToUser(bot->id));
			}
		}
	} else if (force) {
		replyToMsgId = 0;
	}
	if (force) {
		holder->setPendingInitDimensions();
	}
	return (replyToMsg || !replyToMsgId);
}

void HistoryMessageReply::clearData(HistoryMessage *holder) {
	_replyToVia = nullptr;
	if (replyToMsg) {
		App::historyUnregDependency(holder, replyToMsg);
		replyToMsg = nullptr;
	}
	replyToMsgId = 0;
}

bool HistoryMessageReply::isNameUpdated() const {
	if (replyToMsg && replyToMsg->author()->nameVersion > replyToVersion) {
		updateName();
		return true;
	}
	return false;
}

void HistoryMessageReply::updateName() const {
	if (replyToMsg) {
		QString name = (_replyToVia && replyToMsg->author()->isUser()) ? replyToMsg->author()->asUser()->firstName : App::peerName(replyToMsg->author());
		replyToName.setText(st::msgServiceNameFont, name, _textNameOptions);
		replyToVersion = replyToMsg->author()->nameVersion;
		bool hasPreview = replyToMsg->getMedia() ? replyToMsg->getMedia()->hasReplyPreview() : false;
		int32 previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;
		int32 w = replyToName.maxWidth();
		if (_replyToVia) {
			w += st::msgServiceFont->spacew + _replyToVia->_maxWidth;
		}

		_maxReplyWidth = previewSkip + qMax(w, qMin(replyToText.maxWidth(), int32(st::maxSignatureSize)));
	} else {
		_maxReplyWidth = st::msgDateFont->width(lang(replyToMsgId ? lng_profile_loading : lng_deleted_message));
	}
	_maxReplyWidth = st::msgReplyPadding.left() + st::msgReplyBarSkip + _maxReplyWidth + st::msgReplyPadding.right();
}

void HistoryMessageReply::resize(int width) const {
	if (_replyToVia) {
		bool hasPreview = replyToMsg->getMedia() ? replyToMsg->getMedia()->hasReplyPreview() : false;
		int previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;
		_replyToVia->resize(width - st::msgReplyBarSkip - previewSkip - replyToName.maxWidth() - st::msgServiceFont->spacew);
	}
}

void HistoryMessageReply::itemRemoved(HistoryMessage *holder, HistoryItem *removed) {
	if (replyToMsg == removed) {
		clearData(holder);
		holder->setPendingInitDimensions();
	}
}

void HistoryMessageReply::paint(Painter &p, const HistoryItem *holder, int x, int y, int w, PaintFlags flags) const {
	bool selected = (flags & PaintSelected), outbg = holder->hasOutLayout();

	style::color bar;
	if (flags & PaintInBubble) {
		bar = ((flags & PaintSelected) ? (outbg ? st::msgOutReplyBarSelColor : st::msgInReplyBarSelColor) : (outbg ? st::msgOutReplyBarColor : st::msgInReplyBarColor));
	} else {
		bar = st::white;
	}
	QRect rbar(rtlrect(x + st::msgReplyBarPos.x(), y + st::msgReplyPadding.top() + st::msgReplyBarPos.y(), st::msgReplyBarSize.width(), st::msgReplyBarSize.height(), w + 2 * x));
	p.fillRect(rbar, bar);

	if (w > st::msgReplyBarSkip) {
		if (replyToMsg) {
			bool hasPreview = replyToMsg->getMedia() ? replyToMsg->getMedia()->hasReplyPreview() : false;
			int previewSkip = hasPreview ? (st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x()) : 0;

			if (hasPreview) {
				ImagePtr replyPreview = replyToMsg->getMedia()->replyPreview();
				if (!replyPreview->isNull()) {
					QRect to(rtlrect(x + st::msgReplyBarSkip, y + st::msgReplyPadding.top() + st::msgReplyBarPos.y(), st::msgReplyBarSize.height(), st::msgReplyBarSize.height(), w + 2 * x));
					p.drawPixmap(to.x(), to.y(), replyPreview->pixSingle(replyPreview->width() / cIntRetinaFactor(), replyPreview->height() / cIntRetinaFactor(), to.width(), to.height()));
					if (selected) {
						App::roundRect(p, to, textstyleCurrent()->selectOverlay, SelectedOverlayCorners);
					}
				}
			}
			if (w > st::msgReplyBarSkip + previewSkip) {
				if (flags & PaintInBubble) {
					p.setPen(selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg));
				} else {
					p.setPen(st::black);
				}
				replyToName.drawLeftElided(p, x + st::msgReplyBarSkip + previewSkip, y + st::msgReplyPadding.top(), w - st::msgReplyBarSkip - previewSkip, w + 2 * x);
				if (_replyToVia && w > st::msgReplyBarSkip + previewSkip + replyToName.maxWidth() + st::msgServiceFont->spacew) {
					p.setFont(st::msgServiceFont);
					p.drawText(x + st::msgReplyBarSkip + previewSkip + replyToName.maxWidth() + st::msgServiceFont->spacew, y + st::msgReplyPadding.top() + st::msgServiceFont->ascent, _replyToVia->_text);
				}

				HistoryMessage *replyToAsMsg = replyToMsg->toHistoryMessage();
				if (!(flags & PaintInBubble)) {
				} else if ((replyToAsMsg && replyToAsMsg->emptyText()) || replyToMsg->serviceMsg()) {
					style::color date(outbg ? (selected ? st::msgOutDateFgSelected : st::msgOutDateFg) : (selected ? st::msgInDateFgSelected : st::msgInDateFg));
					p.setPen(date);
				} else {
					p.setPen(st::msgColor);
				}
				replyToText.drawLeftElided(p, x + st::msgReplyBarSkip + previewSkip, y + st::msgReplyPadding.top() + st::msgServiceNameFont->height, w - st::msgReplyBarSkip - previewSkip, w + 2 * x);
			}
		} else {
			p.setFont(st::msgDateFont);
			style::color date(outbg ? (selected ? st::msgOutDateFgSelected : st::msgOutDateFg) : (selected ? st::msgInDateFgSelected : st::msgInDateFg));
			p.setPen((flags & PaintInBubble) ? date : st::black);
			p.drawTextLeft(x + st::msgReplyBarSkip, y + st::msgReplyPadding.top() + (st::msgReplyBarSize.height() - st::msgDateFont->height) / 2, w + 2 * x, st::msgDateFont->elided(lang(replyToMsgId ? lng_profile_loading : lng_deleted_message), w - st::msgReplyBarSkip));
		}
	}
}

void HistoryMessage::KeyboardStyle::startPaint(Painter &p) const {
	p.setPen(st::msgServiceColor);
}

style::font HistoryMessage::KeyboardStyle::textFont() const {
	return st::msgServiceFont;
}

void HistoryMessage::KeyboardStyle::repaint(const HistoryItem *item) const {
	Ui::repaintHistoryItem(item);
}

void HistoryMessage::KeyboardStyle::paintButtonBg(Painter &p, const QRect &rect, bool down, float64 howMuchOver) const {
	App::roundRect(p, rect, App::msgServiceBg(), ServiceCorners);
	if (down) {
		howMuchOver = 1.;
	}
	if (howMuchOver > 0) {
		float64 o = p.opacity();
		p.setOpacity(o * (howMuchOver * st::msgBotKbOverOpacity));
		App::roundRect(p, rect, st::white, WhiteCorners);
		p.setOpacity(o);
	}
}

void HistoryMessage::KeyboardStyle::paintButtonIcon(Painter &p, const QRect &rect, HistoryMessageReplyMarkup::Button::Type type) const {
	using Button = HistoryMessageReplyMarkup::Button;
	style::sprite sprite;
	switch (type) {
	case Button::Url: sprite = st::msgBotKbUrlIcon; break;
//	case Button::RequestPhone: sprite = st::msgBotKbRequestPhoneIcon; break;
//	case Button::RequestLocation: sprite = st::msgBotKbRequestLocationIcon; break;
	case Button::SwitchInline: sprite = st::msgBotKbSwitchPmIcon; break;
	}
	if (!sprite.isEmpty()) {
		p.drawSprite(rect.x() + rect.width() - sprite.pxWidth() - st::msgBotKbIconPadding, rect.y() + st::msgBotKbIconPadding, sprite);
	}
}

void HistoryMessage::KeyboardStyle::paintButtonLoading(Painter &p, const QRect &rect) const {
	style::sprite sprite = st::msgInvSendingImg;
	p.drawSprite(rect.x() + rect.width() - sprite.pxWidth() - st::msgBotKbIconPadding, rect.y() + rect.height() - sprite.pxHeight() - st::msgBotKbIconPadding, sprite);
}

int HistoryMessage::KeyboardStyle::minButtonWidth(HistoryMessageReplyMarkup::Button::Type type) const {
	using Button = HistoryMessageReplyMarkup::Button;
	int result = 2 * buttonPadding(), iconWidth = 0;
	switch (type) {
	case Button::Url: iconWidth = st::msgBotKbUrlIcon.pxWidth(); break;
	//case Button::RequestPhone: iconWidth = st::msgBotKbRequestPhoneIcon.pxWidth(); break;
	//case Button::RequestLocation: iconWidth = st::msgBotKbRequestLocationIcon.pxWidth(); break;
	case Button::SwitchInline: iconWidth = st::msgBotKbSwitchPmIcon.pxWidth(); break;
	case Button::Callback: iconWidth = st::msgInvSendingImg.pxWidth(); break;
	}
	if (iconWidth > 0) {
		result = std::max(result, 2 * iconWidth + 4 * int(st::msgBotKbIconPadding));
	}
	return result;
}

HistoryMessage::HistoryMessage(History *history, const MTPDmessage &msg)
: HistoryItem(history, msg.vid.v, msg.vflags.v, ::date(msg.vdate), msg.has_from_id() ? msg.vfrom_id.v : 0) {
	CreateConfig config;

	if (msg.has_fwd_from() && msg.vfwd_from.type() == mtpc_messageFwdHeader) {
		const auto &f(msg.vfwd_from.c_messageFwdHeader());
		if (f.has_from_id() || f.has_channel_id()) {
			config.authorIdOriginal = f.has_channel_id() ? peerFromChannel(f.vchannel_id) : peerFromUser(f.vfrom_id);
			config.fromIdOriginal = f.has_from_id() ? peerFromUser(f.vfrom_id) : peerFromChannel(f.vchannel_id);
			if (f.has_channel_post()) config.originalId = f.vchannel_post.v;
		}
	}
	if (msg.has_reply_to_msg_id()) config.replyTo = msg.vreply_to_msg_id.v;
	if (msg.has_via_bot_id()) config.viaBotId = msg.vvia_bot_id.v;
	if (msg.has_views()) config.viewsCount = msg.vviews.v;
	if (msg.has_reply_markup()) config.markup = &msg.vreply_markup;
	if (msg.has_edit_date()) config.editDate = ::date(msg.vedit_date);

	createComponents(config);

	QString text(textClean(qs(msg.vmessage)));
	initMedia(msg.has_media() ? (&msg.vmedia) : nullptr, text);

	TextWithEntities textWithEntities = { text, EntitiesInText() };
	if (msg.has_entities()) {
		textWithEntities.entities = entitiesFromMTP(msg.ventities.c_vector().v);
	}
	setText(textWithEntities);
}

namespace {

MTPDmessage::Flags newForwardedFlags(PeerData *p, int32 from, HistoryMessage *fwd) {
	MTPDmessage::Flags result = newMessageFlags(p) | MTPDmessage::Flag::f_fwd_from;
	if (from) {
		result |= MTPDmessage::Flag::f_from_id;
	}
	if (fwd->Has<HistoryMessageVia>()) {
		result |= MTPDmessage::Flag::f_via_bot_id;
	}
	if (!p->isChannel()) {
		if (HistoryMedia *media = fwd->getMedia()) {
			if (media->type() == MediaTypeVoiceFile) {
				result |= MTPDmessage::Flag::f_media_unread;
//			} else if (media->type() == MediaTypeVideo) {
//				result |= MTPDmessage::flag_media_unread;
			}
		}
	}
	if (fwd->hasViews()) {
		result |= MTPDmessage::Flag::f_views;
	}
	return result;
}

} // namespace

HistoryMessage::HistoryMessage(History *history, MsgId id, MTPDmessage::Flags flags, QDateTime date, int32 from, HistoryMessage *fwd)
: HistoryItem(history, id, newForwardedFlags(history->peer, from, fwd) | flags, date, from) {
	CreateConfig config;

	config.authorIdOriginal = fwd->authorOriginal()->id;
	config.fromIdOriginal = fwd->fromOriginal()->id;
	if (fwd->authorOriginal()->isChannel()) {
		config.originalId = fwd->id;
	}
	UserData *fwdViaBot = fwd->viaBot();
	if (fwdViaBot) config.viaBotId = peerToUser(fwdViaBot->id);
	int fwdViewsCount = fwd->viewsCount();
	if (fwdViewsCount > 0) {
		config.viewsCount = fwdViewsCount;
	} else if (isPost()) {
		config.viewsCount = 1;
	}

	createComponents(config);

	if (HistoryMedia *mediaOriginal = fwd->getMedia()) {
		_media.reset(mediaOriginal->clone(this));
	}
	setText(fwd->originalText());
}

HistoryMessage::HistoryMessage(History *history, MsgId id, MTPDmessage::Flags flags, MsgId replyTo, int32 viaBotId, QDateTime date, int32 from, const TextWithEntities &textWithEntities)
: HistoryItem(history, id, flags, date, (flags & MTPDmessage::Flag::f_from_id) ? from : 0) {
	createComponentsHelper(flags, replyTo, viaBotId, MTPnullMarkup);

	setText(textWithEntities);
}

HistoryMessage::HistoryMessage(History *history, MsgId msgId, MTPDmessage::Flags flags, MsgId replyTo, int32 viaBotId, QDateTime date, int32 from, DocumentData *doc, const QString &caption, const MTPReplyMarkup &markup)
: HistoryItem(history, msgId, flags, date, (flags & MTPDmessage::Flag::f_from_id) ? from : 0) {
	createComponentsHelper(flags, replyTo, viaBotId, markup);

	initMediaFromDocument(doc, caption);
	setText(TextWithEntities());
}

HistoryMessage::HistoryMessage(History *history, MsgId msgId, MTPDmessage::Flags flags, MsgId replyTo, int32 viaBotId, QDateTime date, int32 from, PhotoData *photo, const QString &caption, const MTPReplyMarkup &markup)
: HistoryItem(history, msgId, flags, date, (flags & MTPDmessage::Flag::f_from_id) ? from : 0) {
	createComponentsHelper(flags, replyTo, viaBotId, markup);

	_media.reset(new HistoryPhoto(this, photo, caption));
	setText(TextWithEntities());
}

void HistoryMessage::createComponentsHelper(MTPDmessage::Flags flags, MsgId replyTo, int32 viaBotId, const MTPReplyMarkup &markup) {
	CreateConfig config;

	if (flags & MTPDmessage::Flag::f_via_bot_id) config.viaBotId = viaBotId;
	if (flags & MTPDmessage::Flag::f_reply_to_msg_id) config.replyTo = replyTo;
	if (flags & MTPDmessage::Flag::f_reply_markup) config.markup = &markup;
	if (isPost()) config.viewsCount = 1;

	createComponents(config);
}

bool HistoryMessage::displayEditedBadge(bool hasViaBot) const {
	if (!(_flags & MTPDmessage::Flag::f_edit_date)) {
		return false;
	}
	if (auto fromUser = from()->asUser()) {
		if (fromUser->botInfo) {
			return false;
		}
	}
	if (hasViaBot) {
		return false;
	}
	return true;
}


void HistoryMessage::createComponents(const CreateConfig &config) {
	uint64 mask = 0;
	if (config.replyTo) {
		mask |= HistoryMessageReply::Bit();
	}
	if (config.viaBotId) {
		mask |= HistoryMessageVia::Bit();
	}
	if (config.viewsCount >= 0) {
		mask |= HistoryMessageViews::Bit();
	}
	if (isPost() && _from->isUser()) {
		mask |= HistoryMessageSigned::Bit();
	}
	if (displayEditedBadge(config.viaBotId != 0)) {
		mask |= HistoryMessageEdited::Bit();
	}
	if (config.authorIdOriginal && config.fromIdOriginal) {
		mask |= HistoryMessageForwarded::Bit();
	}
	if (config.markup) {
		// optimization: don't create markup component for the case
		// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
		if (config.markup->type() != mtpc_replyKeyboardHide || config.markup->c_replyKeyboardHide().vflags.v != 0) {
			mask |= HistoryMessageReplyMarkup::Bit();
		}
	}
	UpdateComponents(mask);
	if (auto reply = Get<HistoryMessageReply>()) {
		reply->replyToMsgId = config.replyTo;
		if (!reply->updateData(this) && App::api()) {
			App::api()->requestMessageData(history()->peer->asChannel(), reply->replyToMsgId, std_::make_unique<HistoryDependentItemCallback>(fullId()));
		}
	}
	if (auto via = Get<HistoryMessageVia>()) {
		via->create(config.viaBotId);
	}
	if (auto views = Get<HistoryMessageViews>()) {
		views->_views = config.viewsCount;
	}
	if (auto msgsigned = Get<HistoryMessageSigned>()) {
		msgsigned->create(_from->asUser(), date);
	}
	if (auto edited = Get<HistoryMessageEdited>()) {
		edited->create(config.editDate, date);
	}
	if (auto fwd = Get<HistoryMessageForwarded>()) {
		fwd->_authorOriginal = App::peer(config.authorIdOriginal);
		fwd->_fromOriginal = App::peer(config.fromIdOriginal);
		fwd->_originalId = config.originalId;
	}
	if (auto markup = Get<HistoryMessageReplyMarkup>()) {
		markup->create(*config.markup);
		if (markup->flags & MTPDreplyKeyboardMarkup_ClientFlag::f_has_switch_inline_button) {
			_flags |= MTPDmessage_ClientFlag::f_has_switch_inline_button;
		}
	}
	initTime();
}

QString formatViewsCount(int32 views) {
	if (views > 999999) {
		views /= 100000;
		if (views % 10) {
			return QString::number(views / 10) + '.' + QString::number(views % 10) + 'M';
		}
		return QString::number(views / 10) + 'M';
	} else if (views > 9999) {
		views /= 100;
		if (views % 10) {
			return QString::number(views / 10) + '.' + QString::number(views % 10) + 'K';
		}
		return QString::number(views / 10) + 'K';
	} else if (views > 0) {
		return QString::number(views);
	}
	return qsl("1");
}

void HistoryMessage::initTime() {
	if (auto msgsigned = Get<HistoryMessageSigned>()) {
		_timeWidth = msgsigned->maxWidth();
	} else if (auto edited = Get<HistoryMessageEdited>()) {
		_timeWidth = edited->maxWidth();
	} else {
		_timeText = date.toString(cTimeFormat());
		_timeWidth = st::msgDateFont->width(_timeText);
	}
	if (auto views = Get<HistoryMessageViews>()) {
		views->_viewsText = (views->_views >= 0) ? formatViewsCount(views->_views) : QString();
		views->_viewsWidth = views->_viewsText.isEmpty() ? 0 : st::msgDateFont->width(views->_viewsText);
	}
}

void HistoryMessage::initMedia(const MTPMessageMedia *media, QString &currentText) {
	switch (media ? media->type() : mtpc_messageMediaEmpty) {
	case mtpc_messageMediaContact: {
		const auto &d(media->c_messageMediaContact());
		_media.reset(new HistoryContact(this, d.vuser_id.v, qs(d.vfirst_name), qs(d.vlast_name), qs(d.vphone_number)));
	} break;
	case mtpc_messageMediaGeo: {
		const auto &point(media->c_messageMediaGeo().vgeo);
		if (point.type() == mtpc_geoPoint) {
			_media.reset(new HistoryLocation(this, LocationCoords(point.c_geoPoint())));
		}
	} break;
	case mtpc_messageMediaVenue: {
		const auto &d(media->c_messageMediaVenue());
		if (d.vgeo.type() == mtpc_geoPoint) {
			_media.reset(new HistoryLocation(this, LocationCoords(d.vgeo.c_geoPoint()), qs(d.vtitle), qs(d.vaddress)));
		}
	} break;
	case mtpc_messageMediaPhoto: {
		const auto &photo(media->c_messageMediaPhoto());
		if (photo.vphoto.type() == mtpc_photo) {
			_media.reset(new HistoryPhoto(this, App::feedPhoto(photo.vphoto.c_photo()), qs(photo.vcaption)));
		}
	} break;
	case mtpc_messageMediaDocument: {
		const auto &document(media->c_messageMediaDocument().vdocument);
		if (document.type() == mtpc_document) {
			return initMediaFromDocument(App::feedDocument(document), qs(media->c_messageMediaDocument().vcaption));
		}
	} break;
	case mtpc_messageMediaWebPage: {
		const auto &d(media->c_messageMediaWebPage().vwebpage);
		switch (d.type()) {
		case mtpc_webPageEmpty: break;
		case mtpc_webPagePending: {
			_media.reset(new HistoryWebPage(this, App::feedWebPage(d.c_webPagePending())));
		} break;
		case mtpc_webPage: {
			_media.reset(new HistoryWebPage(this, App::feedWebPage(d.c_webPage())));
		} break;
		}
	} break;
	};
}

void HistoryMessage::initMediaFromDocument(DocumentData *doc, const QString &caption) {
	if (doc->sticker()) {
		_media.reset(new HistorySticker(this, doc));
	} else if (doc->isAnimation()) {
		_media.reset(new HistoryGif(this, doc, caption));
	} else if (doc->isVideo()) {
		_media.reset(new HistoryVideo(this, doc, caption));
	} else {
		_media.reset(new HistoryDocument(this, doc, caption));
	}
}

int32 HistoryMessage::plainMaxWidth() const {
	return st::msgPadding.left() + _text.maxWidth() + st::msgPadding.right();
}

void HistoryMessage::initDimensions() {
	auto reply = Get<HistoryMessageReply>();
	if (reply) {
		reply->updateName();
	}
	if (drawBubble()) {
		auto fwd = Get<HistoryMessageForwarded>();
		auto via = Get<HistoryMessageVia>();
		if (fwd) {
			fwd->create(via);
		}

		if (_media) {
			_media->initDimensions();
			if (_media->isDisplayed()) {
				if (_text.hasSkipBlock()) {
					_text.removeSkipBlock();
					_textWidth = -1;
					_textHeight = 0;
				}
			} else if (!_text.hasSkipBlock()) {
				_text.setSkipBlock(skipBlockWidth(), skipBlockHeight());
				_textWidth = -1;
				_textHeight = 0;
			}
		}

		_maxw = plainMaxWidth();
		if (_text.isEmpty()) {
			_minh = 0;
		} else {
			_minh = st::msgPadding.top() + _text.minHeight() + st::msgPadding.bottom();
		}
		if (_media && _media->isDisplayed()) {
			int32 maxw = _media->maxWidth();
			if (maxw > _maxw) _maxw = maxw;
			_minh += _media->minHeight();
		}
		if (!_media) {
			if (displayFromName()) {
				int32 namew = st::msgPadding.left() + author()->nameText.maxWidth() + st::msgPadding.right();
				if (via && !fwd) {
					namew += st::msgServiceFont->spacew + via->_maxWidth;
				}
				if (namew > _maxw) _maxw = namew;
			} else if (via && !fwd) {
				if (st::msgPadding.left() + via->_maxWidth + st::msgPadding.right() > _maxw) {
					_maxw = st::msgPadding.left() + via->_maxWidth + st::msgPadding.right();
				}
			}
			if (fwd) {
				int32 _namew = st::msgPadding.left() + fwd->_text.maxWidth() + st::msgPadding.right();
				if (via) {
					_namew += st::msgServiceFont->spacew + via->_maxWidth;
				}
				if (_namew > _maxw) _maxw = _namew;
			}
		}
	} else if (_media) {
		_media->initDimensions();
		_maxw = _media->maxWidth();
		_minh = _media->minHeight();
	} else {
		_maxw = st::msgMinWidth;
		_minh = 0;
	}
	if (reply && !_text.isEmpty()) {
		int replyw = st::msgPadding.left() + reply->_maxReplyWidth - st::msgReplyPadding.left() - st::msgReplyPadding.right() + st::msgPadding.right();
		if (reply->_replyToVia) {
			replyw += st::msgServiceFont->spacew + reply->_replyToVia->_maxWidth;
		}
		if (replyw > _maxw) _maxw = replyw;
	}
	if (auto markup = inlineReplyMarkup()) {
		if (!markup->inlineKeyboard) {
			markup->inlineKeyboard.reset(new ReplyKeyboard(this, std_::make_unique<KeyboardStyle>(st::msgBotKbButton)));
		}

		// if we have a text bubble we can resize it to fit the keyboard
		// but if we have only media we don't do that
		if (!_text.isEmpty()) {
			_maxw = qMax(_maxw, markup->inlineKeyboard->naturalWidth());
		}
	}
}

void HistoryMessage::countPositionAndSize(int32 &left, int32 &width) const {
	int32 maxwidth = qMin(int(st::msgMaxWidth), _maxw), hwidth = _history->width;
	if (_media && _media->currentWidth() < maxwidth) {
		maxwidth = qMax(_media->currentWidth(), qMin(maxwidth, plainMaxWidth()));
	}

	left = (!isPost() && out() && !Adaptive::Wide()) ? st::msgMargin.right() : st::msgMargin.left();
	if (hasFromPhoto()) {
		left += st::msgPhotoSkip;
//	} else if (!Adaptive::Wide() && !out() && !fromChannel() && st::msgPhotoSkip - (hmaxwidth - hwidth) > 0) {
//		left += st::msgPhotoSkip - (hmaxwidth - hwidth);
	}

	width = hwidth - st::msgMargin.left() - st::msgMargin.right();
	if (width > maxwidth) {
		if (!isPost() && out() && !Adaptive::Wide()) {
			left += width - maxwidth;
		}
		width = maxwidth;
	}
}

void HistoryMessage::fromNameUpdated(int32 width) const {
	_authorNameVersion = author()->nameVersion;
	if (!Has<HistoryMessageForwarded>()) {
		if (auto via = Get<HistoryMessageVia>()) {
			via->resize(width - st::msgPadding.left() - st::msgPadding.right() - author()->nameText.maxWidth() - st::msgServiceFont->spacew);
		}
	}
}

void HistoryMessage::applyEdition(const MTPDmessage &message) {
	int keyboardTop = -1;
	if (!pendingResize()) {
		if (auto keyboard = inlineReplyKeyboard()) {
			int h = st::msgBotKbButton.margin + keyboard->naturalHeight();
			keyboardTop = _height - h + st::msgBotKbButton.margin - marginBottom();
		}
	}

	if (message.has_edit_date()) {
		_flags |= MTPDmessage::Flag::f_edit_date;
		if (displayEditedBadge(Has<HistoryMessageVia>())) {
			if (!Has<HistoryMessageEdited>()) {
				AddComponents(HistoryMessageEdited::Bit());
			}
			Get<HistoryMessageEdited>()->create(::date(message.vedit_date), date);
		} else if (Has<HistoryMessageEdited>()) {
			RemoveComponents(HistoryMessageEdited::Bit());
		}
		initTime();
	}

	TextWithEntities textWithEntities = { qs(message.vmessage), EntitiesInText() };
	if (message.has_entities()) {
		textWithEntities.entities = entitiesFromMTP(message.ventities.c_vector().v);
	}
	setText(textWithEntities);
	setMedia(message.has_media() ? (&message.vmedia) : nullptr);
	setReplyMarkup(message.has_reply_markup() ? (&message.vreply_markup) : nullptr);
	setViewsCount(message.has_views() ? message.vviews.v : -1);

	finishEdition(keyboardTop);
}

void HistoryMessage::applyEditionToEmpty() {
	setEmptyText();
	setMedia(nullptr);
	setReplyMarkup(nullptr);
	setViewsCount(-1);

	finishEditionToEmpty();
}

void HistoryMessage::updateMedia(const MTPMessageMedia *media) {
	if (_flags & MTPDmessage_ClientFlag::f_from_inline_bot) {
		bool needReSet = true;
		if (media && _media) {
			needReSet = _media->needReSetInlineResultMedia(*media);
		}
		if (needReSet) {
			setMedia(media);
		}
		_flags &= ~MTPDmessage_ClientFlag::f_from_inline_bot;
	} else if (media && _media && _media->type() != MediaTypeWebPage) {
		_media->updateSentMedia(*media);
	} else {
		setMedia(media);
	}
	setPendingInitDimensions();
}

int32 HistoryMessage::addToOverview(AddToOverviewMethod method) {
	if (!indexInOverview()) return 0;

	int32 result = 0;
	if (HistoryMedia *media = getMedia()) {
		MediaOverviewType type = messageMediaToOverviewType(media);
		if (type != OverviewCount) {
			if (history()->addToOverview(type, id, method)) {
				result |= (1 << type);
			}
		}
	}
	if (hasTextLinks()) {
		if (history()->addToOverview(OverviewLinks, id, method)) {
			result |= (1 << OverviewLinks);
		}
	}
	return result;
}

void HistoryMessage::eraseFromOverview() {
	if (HistoryMedia *media = getMedia()) {
		MediaOverviewType type = messageMediaToOverviewType(media);
		if (type != OverviewCount) {
			history()->eraseFromOverview(type, id);
		}
	}
	if (hasTextLinks()) {
		history()->eraseFromOverview(OverviewLinks, id);
	}
}

TextWithEntities HistoryMessage::selectedText(TextSelection selection) const {
	TextWithEntities result, textResult, mediaResult;
	if (selection == FullSelection) {
		textResult = _text.originalTextWithEntities(AllTextSelection, ExpandLinksAll);
	} else {
		textResult = _text.originalTextWithEntities(selection, ExpandLinksAll);
	}
	if (_media) {
		mediaResult = _media->selectedText(toMediaSelection(selection));
	}
	if (textResult.text.isEmpty()) {
		result = mediaResult;
	} else if (mediaResult.text.isEmpty()) {
		result = textResult;
	} else {
		result.text = textResult.text + qstr("\n\n");
		result.entities = textResult.entities;
		appendTextWithEntities(result, std_::move(mediaResult));
	}
	if (auto fwd = Get<HistoryMessageForwarded>()) {
		if (selection == FullSelection) {
			auto fwdinfo = fwd->_text.originalTextWithEntities(AllTextSelection, ExpandLinksAll);
			TextWithEntities wrapped;
			wrapped.text.reserve(fwdinfo.text.size() + 4 + result.text.size());
			wrapped.entities.reserve(fwdinfo.entities.size() + result.entities.size());
			wrapped.text.append('[');
			appendTextWithEntities(wrapped, std_::move(fwdinfo));
			wrapped.text.append(qsl("]\n"));
			appendTextWithEntities(wrapped, std_::move(result));
			result = wrapped;
		}
	}
	if (auto reply = Get<HistoryMessageReply>()) {
		if (selection == FullSelection && reply->replyToMsg) {
			TextWithEntities wrapped;
			wrapped.text.reserve(lang(lng_in_reply_to).size() + reply->replyToMsg->author()->name.size() + 4 + result.text.size());
			wrapped.text.append('[').append(lang(lng_in_reply_to)).append(' ').append(reply->replyToMsg->author()->name).append(qsl("]\n"));
			appendTextWithEntities(wrapped, std_::move(result));
			result = wrapped;
		}
	}
	return result;
}

QString HistoryMessage::inDialogsText() const {
	return emptyText() ? (_media ? _media->inDialogsText() : QString()) : _text.originalText(AllTextSelection, ExpandLinksNone);
}

void HistoryMessage::setMedia(const MTPMessageMedia *media) {
	if (!_media && (!media || media->type() == mtpc_messageMediaEmpty)) return;

	bool mediaWasDisplayed = false;
	if (_media) {
		mediaWasDisplayed = _media->isDisplayed();
		_media.clear();
	}
	QString t;
	initMedia(media, t);
	if (_media && _media->isDisplayed() && !mediaWasDisplayed) {
		_text.removeSkipBlock();
		_textWidth = -1;
		_textHeight = 0;
	} else if (mediaWasDisplayed && (!_media || !_media->isDisplayed())) {
		_text.setSkipBlock(skipBlockWidth(), skipBlockHeight());
		_textWidth = -1;
		_textHeight = 0;
	}
}

void HistoryMessage::setText(const TextWithEntities &textWithEntities) {
	textstyleSet(&((out() && !isPost()) ? st::outTextStyle : st::inTextStyle));
	if (_media && _media->isDisplayed()) {
		_text.setMarkedText(st::msgFont, textWithEntities, itemTextOptions(this));
	} else {
		_text.setMarkedText(st::msgFont, { textWithEntities.text + skipBlock(), textWithEntities.entities }, itemTextOptions(this));
	}
	textstyleRestore();

	for_const (auto &entity, textWithEntities.entities) {
		auto type = entity.type();
		if (type == EntityInTextUrl || type == EntityInTextCustomUrl || type == EntityInTextEmail) {
			_flags |= MTPDmessage_ClientFlag::f_has_text_links;
			break;
		}
	}
	_textWidth = -1;
	_textHeight = 0;
}

void HistoryMessage::setEmptyText() {
	textstyleSet(&((out() && !isPost()) ? st::outTextStyle : st::inTextStyle));
	_text.setMarkedText(st::msgFont, { QString(), EntitiesInText() }, itemTextOptions(this));
	textstyleRestore();

	_textWidth = -1;
	_textHeight = 0;
}

void HistoryMessage::setReplyMarkup(const MTPReplyMarkup *markup) {
	if (!markup) {
		if (_flags & MTPDmessage::Flag::f_reply_markup) {
			_flags &= ~MTPDmessage::Flag::f_reply_markup;
			if (Has<HistoryMessageReplyMarkup>()) {
				RemoveComponents(HistoryMessageReplyMarkup::Bit());
			}
			setPendingInitDimensions();
			Notify::replyMarkupUpdated(this);
		}
		return;
	}

	// optimization: don't create markup component for the case
	// MTPDreplyKeyboardHide with flags = 0, assume it has f_zero flag
	if (markup->type() == mtpc_replyKeyboardHide && markup->c_replyKeyboardHide().vflags.v == 0) {
		bool changed = false;
		if (Has<HistoryMessageReplyMarkup>()) {
			RemoveComponents(HistoryMessageReplyMarkup::Bit());
			changed = true;
		}
		if (!(_flags & MTPDmessage::Flag::f_reply_markup)) {
			_flags |= MTPDmessage::Flag::f_reply_markup;
			changed = true;
		}
		if (changed) {
			setPendingInitDimensions();

			Notify::replyMarkupUpdated(this);
		}
	} else {
		if (!(_flags & MTPDmessage::Flag::f_reply_markup)) {
			_flags |= MTPDmessage::Flag::f_reply_markup;
		}
		if (!Has<HistoryMessageReplyMarkup>()) {
			AddComponents(HistoryMessageReplyMarkup::Bit());
		}
		Get<HistoryMessageReplyMarkup>()->create(*markup);
		setPendingInitDimensions();

		Notify::replyMarkupUpdated(this);
	}
}

TextWithEntities HistoryMessage::originalText() const {
	if (emptyText()) {
		return { QString(), EntitiesInText() };
	}
	return _text.originalTextWithEntities();
}

bool HistoryMessage::textHasLinks() const {
	return emptyText() ? false : _text.hasLinks();
}

void HistoryMessage::drawInfo(Painter &p, int32 right, int32 bottom, int32 width, bool selected, InfoDisplayType type) const {
	p.setFont(st::msgDateFont);

	bool outbg = out() && !isPost();
	bool invertedsprites = (type == InfoDisplayOverImage || type == InfoDisplayOverBackground);
	int32 infoRight = right, infoBottom = bottom;
	switch (type) {
	case InfoDisplayDefault:
		infoRight -= st::msgPadding.right() - st::msgDateDelta.x();
		infoBottom -= st::msgPadding.bottom() - st::msgDateDelta.y();
		p.setPen(selected ? (outbg ? st::msgOutDateFgSelected : st::msgInDateFgSelected) : (outbg ? st::msgOutDateFg : st::msgInDateFg));
	break;
	case InfoDisplayOverImage:
		infoRight -= st::msgDateImgDelta + st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgDelta + st::msgDateImgPadding.y();
		p.setPen(st::msgDateImgColor);
	break;
	case InfoDisplayOverBackground:
		infoRight -= st::msgDateImgDelta + st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgDelta + st::msgDateImgPadding.y();
		p.setPen(st::msgServiceColor);
	break;
	}

	int32 infoW = HistoryMessage::infoWidth();
	if (rtl()) infoRight = width - infoRight + infoW;

	int32 dateX = infoRight - infoW;
	int32 dateY = infoBottom - st::msgDateFont->height;
	if (type == InfoDisplayOverImage) {
		int32 dateW = infoW + 2 * st::msgDateImgPadding.x(), dateH = st::msgDateFont->height + 2 * st::msgDateImgPadding.y();
		App::roundRect(p, dateX - st::msgDateImgPadding.x(), dateY - st::msgDateImgPadding.y(), dateW, dateH, selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);
	} else if (type == InfoDisplayOverBackground) {
		int32 dateW = infoW + 2 * st::msgDateImgPadding.x(), dateH = st::msgDateFont->height + 2 * st::msgDateImgPadding.y();
		App::roundRect(p, dateX - st::msgDateImgPadding.x(), dateY - st::msgDateImgPadding.y(), dateW, dateH, selected ? App::msgServiceSelectBg() : App::msgServiceBg(), selected ? ServiceSelectedCorners : ServiceCorners);
	}
	dateX += HistoryMessage::timeLeft();

	if (auto msgsigned = Get<HistoryMessageSigned>()) {
		msgsigned->_signature.drawElided(p, dateX, dateY, _timeWidth);
	} else if (auto edited = Get<HistoryMessageEdited>()) {
		edited->_edited.drawElided(p, dateX, dateY, _timeWidth);
	} else {
		p.drawText(dateX, dateY + st::msgDateFont->ascent, _timeText);
	}

	QPoint iconPos;
	const style::sprite *iconRect = nullptr;
	if (auto views = Get<HistoryMessageViews>()) {
		iconPos = QPoint(infoRight - infoW + st::msgViewsPos.x(), infoBottom - st::msgViewsImg.pxHeight() + st::msgViewsPos.y());
		if (id > 0) {
			if (outbg) {
				iconRect = &(invertedsprites ? st::msgInvViewsImg : (selected ? st::msgSelectOutViewsImg : st::msgOutViewsImg));
			} else {
				iconRect = &(invertedsprites ? st::msgInvViewsImg : (selected ? st::msgSelectViewsImg : st::msgViewsImg));
			}
			p.drawText(iconPos.x() + st::msgViewsImg.pxWidth() + st::msgDateCheckSpace, infoBottom - st::msgDateFont->descent, views->_viewsText);
		} else {
			iconPos.setX(iconPos.x() + st::msgDateViewsSpace + views->_viewsWidth);
			if (outbg) {
				iconRect = &(invertedsprites ? st::msgInvSendingViewsImg : st::msgSendingOutViewsImg);
			} else {
				iconRect = &(invertedsprites ? st::msgInvSendingViewsImg : st::msgSendingViewsImg);
			}
		}
		p.drawSprite(iconPos, *iconRect);
	} else if (id < 0 && history()->peer->isSelf()) {
		iconPos = QPoint(infoRight - infoW, infoBottom - st::msgViewsImg.pxHeight() + st::msgViewsPos.y());
		iconRect = &(invertedsprites ? st::msgInvSendingViewsImg : st::msgSendingViewsImg);
		p.drawSprite(iconPos, *iconRect);
	}
	if (outbg) {
		iconPos = QPoint(infoRight - st::msgCheckImg.pxWidth() + st::msgCheckPos.x(), infoBottom - st::msgCheckImg.pxHeight() + st::msgCheckPos.y());
		if (id > 0) {
			if (unread()) {
				iconRect = &(invertedsprites ? st::msgInvCheckImg : (selected ? st::msgSelectCheckImg : st::msgCheckImg));
			} else {
				iconRect = &(invertedsprites ? st::msgInvDblCheckImg : (selected ? st::msgSelectDblCheckImg : st::msgDblCheckImg));
			}
		} else {
			iconRect = &(invertedsprites ? st::msgInvSendingImg : st::msgSendingImg);
		}
		p.drawSprite(iconPos, *iconRect);
	}
}

void HistoryMessage::setViewsCount(int32 count) {
	auto views = Get<HistoryMessageViews>();
	if (!views || views->_views == count || (count >= 0 && views->_views > count)) return;

	int32 was = views->_viewsWidth;
	views->_views = count;
	views->_viewsText = (views->_views >= 0) ? formatViewsCount(views->_views) : QString();
	views->_viewsWidth = views->_viewsText.isEmpty() ? 0 : st::msgDateFont->width(views->_viewsText);
	if (was == views->_viewsWidth) {
		Ui::repaintHistoryItem(this);
	} else {
		if (_text.hasSkipBlock()) {
			_text.setSkipBlock(HistoryMessage::skipBlockWidth(), HistoryMessage::skipBlockHeight());
			_textWidth = -1;
			_textHeight = 0;
		}
		setPendingInitDimensions();
	}
}

void HistoryMessage::setId(MsgId newId) {
	bool wasPositive = (id > 0), positive = (newId > 0);
	HistoryItem::setId(newId);
	if (wasPositive == positive) {
		Ui::repaintHistoryItem(this);
	} else {
		if (_text.hasSkipBlock()) {
			_text.setSkipBlock(HistoryMessage::skipBlockWidth(), HistoryMessage::skipBlockHeight());
			_textWidth = -1;
			_textHeight = 0;
		}
		setPendingInitDimensions();
	}
}

void HistoryMessage::draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const {
	bool outbg = out() && !isPost(), bubble = drawBubble(), selected = (selection == FullSelection);

	int left = 0, width = 0, height = _height;
	countPositionAndSize(left, width);
	if (width < 1) return;

	int dateh = 0, unreadbarh = 0;
	if (auto date = Get<HistoryMessageDate>()) {
		dateh = date->height();
		//if (r.intersects(QRect(0, 0, _history->width, dateh))) {
		//	date->paint(p, 0, _history->width);
		//}
	}
	if (auto unreadbar = Get<HistoryMessageUnreadBar>()) {
		unreadbarh = unreadbar->height();
		if (r.intersects(QRect(0, dateh, _history->width, unreadbarh))) {
			p.translate(0, dateh);
			unreadbar->paint(p, 0, _history->width);
			p.translate(0, -dateh);
		}
	}

	uint64 fullAnimMs = App::main() ? App::main()->animActiveTimeStart(this) : 0;
	if (fullAnimMs > 0 && fullAnimMs <= ms) {
		int animms = ms - fullAnimMs;
		if (animms > st::activeFadeInDuration + st::activeFadeOutDuration) {
			App::main()->stopAnimActive();
		} else {
			int skiph = marginTop() - marginBottom();

			float64 dt = (animms > st::activeFadeInDuration) ? (1 - (animms - st::activeFadeInDuration) / float64(st::activeFadeOutDuration)) : (animms / float64(st::activeFadeInDuration));
			float64 o = p.opacity();
			p.setOpacity(o * dt);
			p.fillRect(0, skiph, _history->width, height - skiph, textstyleCurrent()->selectOverlay->b);
			p.setOpacity(o);
		}
	}

	textstyleSet(&(outbg ? st::outTextStyle : st::inTextStyle));

	if (const ReplyKeyboard *keyboard = inlineReplyKeyboard()) {
		int h = st::msgBotKbButton.margin + keyboard->naturalHeight();
		height -= h;
		int top = height + st::msgBotKbButton.margin - marginBottom();
		p.translate(left, top);
		keyboard->paint(p, r.translated(-left, -top));
		p.translate(-left, -top);
	}

	if (bubble) {
		auto fwd = Get<HistoryMessageForwarded>();
		auto via = Get<HistoryMessageVia>();
		if (displayFromName() && author()->nameVersion > _authorNameVersion) {
			fromNameUpdated(width);
		}

		int32 top = marginTop();
		QRect r(left, top, width, height - top - marginBottom());

		style::color bg(selected ? (outbg ? st::msgOutBgSelected : st::msgInBgSelected) : (outbg ? st::msgOutBg : st::msgInBg));
		style::color sh(selected ? (outbg ? st::msgOutShadowSelected : st::msgInShadowSelected) : (outbg ? st::msgOutShadow : st::msgInShadow));
		RoundCorners cors(selected ? (outbg ? MessageOutSelectedCorners : MessageInSelectedCorners) : (outbg ? MessageOutCorners : MessageInCorners));
		App::roundRect(p, r, bg, cors, &sh);

		if (displayFromName()) {
			p.setFont(st::msgNameFont);
			if (isPost()) {
				p.setPen(selected ? st::msgInServiceFgSelected : st::msgInServiceFg);
			} else {
				p.setPen(author()->color);
			}
			author()->nameText.drawElided(p, r.left() + st::msgPadding.left(), r.top() + st::msgPadding.top(), width - st::msgPadding.left() - st::msgPadding.right());
			if (via && !fwd && width > st::msgPadding.left() + st::msgPadding.right() + author()->nameText.maxWidth() + st::msgServiceFont->spacew) {
				p.setPen(selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg));
				p.drawText(r.left() + st::msgPadding.left() + author()->nameText.maxWidth() + st::msgServiceFont->spacew, r.top() + st::msgPadding.top() + st::msgServiceFont->ascent, via->_text);
			}
			r.setTop(r.top() + st::msgNameFont->height);
		}

		QRect trect(r.marginsAdded(-st::msgPadding));

		paintForwardedInfo(p, trect, selected);
		paintReplyInfo(p, trect, selected);
		paintViaBotIdInfo(p, trect, selected);

		p.setPen(st::msgColor);
		p.setFont(st::msgFont);
		_text.draw(p, trect.x(), trect.y(), trect.width(), style::al_left, 0, -1, selection);

		if (_media && _media->isDisplayed()) {
			int32 top = height - marginBottom() - _media->height();
			p.translate(left, top);
			_media->draw(p, r.translated(-left, -top), toMediaSelection(selection), ms);
			p.translate(-left, -top);
			if (!_media->customInfoLayout()) {
				HistoryMessage::drawInfo(p, r.x() + r.width(), r.y() + r.height(), 2 * r.x() + r.width(), selected, InfoDisplayDefault);
			}
		} else {
			HistoryMessage::drawInfo(p, r.x() + r.width(), r.y() + r.height(), 2 * r.x() + r.width(), selected, InfoDisplayDefault);
		}
	} else if (_media) {
		int32 top = marginTop();
		p.translate(left, top);
		_media->draw(p, r.translated(-left, -top), toMediaSelection(selection), ms);
		p.translate(-left, -top);
	}

	textstyleRestore();

	auto reply = Get<HistoryMessageReply>();
	if (reply && reply->isNameUpdated()) {
		const_cast<HistoryMessage*>(this)->setPendingInitDimensions();
	}
}

void HistoryMessage::paintForwardedInfo(Painter &p, QRect &trect, bool selected) const {
	if (displayForwardedFrom()) {
		style::font serviceFont(st::msgServiceFont), serviceName(st::msgServiceNameFont);

		p.setPen(selected ? (hasOutLayout() ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (hasOutLayout() ? st::msgOutServiceFg : st::msgInServiceFg));
		p.setFont(serviceFont);

		auto fwd = Get<HistoryMessageForwarded>();
		bool breakEverywhere = (fwd->_text.countHeight(trect.width()) > 2 * serviceFont->height);
		textstyleSet(&(selected ? (hasOutLayout() ? st::outFwdTextStyleSelected : st::inFwdTextStyleSelected) : (hasOutLayout() ? st::outFwdTextStyle : st::inFwdTextStyle)));
		fwd->_text.drawElided(p, trect.x(), trect.y(), trect.width(), 2, style::al_left, 0, -1, 0, breakEverywhere);
		textstyleSet(&(hasOutLayout() ? st::outTextStyle : st::inTextStyle));

		trect.setY(trect.y() + (((fwd->_text.maxWidth() > trect.width()) ? 2 : 1) * serviceFont->height));
	}
}

void HistoryMessage::paintReplyInfo(Painter &p, QRect &trect, bool selected) const {
	if (auto reply = Get<HistoryMessageReply>()) {
		int32 h = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();

		HistoryMessageReply::PaintFlags flags = HistoryMessageReply::PaintInBubble;
		if (selected) {
			flags |= HistoryMessageReply::PaintSelected;
		}
		reply->paint(p, this, trect.x(), trect.y(), trect.width(), flags);

		trect.setY(trect.y() + h);
	}
}

void HistoryMessage::paintViaBotIdInfo(Painter &p, QRect &trect, bool selected) const {
	if (!displayFromName() && !Has<HistoryMessageForwarded>()) {
		if (auto via = Get<HistoryMessageVia>()) {
			p.setFont(st::msgServiceNameFont);
			p.setPen(selected ? (hasOutLayout() ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (hasOutLayout() ? st::msgOutServiceFg : st::msgInServiceFg));
			p.drawTextLeft(trect.left(), trect.top(), _history->width, via->_text);
			trect.setY(trect.y() + st::msgServiceNameFont->height);
		}
	}
}

void HistoryMessage::dependencyItemRemoved(HistoryItem *dependency) {
	if (auto reply = Get<HistoryMessageReply>()) {
		reply->itemRemoved(this, dependency);
	}
}

int HistoryMessage::resizeGetHeight_(int width) {
	int result = performResizeGetHeight(width);

	auto keyboard = inlineReplyKeyboard();
	if (auto markup = Get<HistoryMessageReplyMarkup>()) {
		int oldTop = markup->oldTop;
		if (oldTop >= 0) {
			markup->oldTop = -1;
			if (keyboard) {
				int h = st::msgBotKbButton.margin + keyboard->naturalHeight();
				int keyboardTop = _height - h + st::msgBotKbButton.margin - marginBottom();
				if (keyboardTop != oldTop) {
					Notify::inlineKeyboardMoved(this, oldTop, keyboardTop);
				}
			}
		}
	}

	return result;
}

int HistoryMessage::performResizeGetHeight(int width) {
	if (width < st::msgMinWidth) return _height;

	width -= st::msgMargin.left() + st::msgMargin.right();
	if (width < st::msgPadding.left() + st::msgPadding.right() + 1) {
		width = st::msgPadding.left() + st::msgPadding.right() + 1;
	} else if (width > st::msgMaxWidth) {
		width = st::msgMaxWidth;
	}
	if (drawBubble()) {
		auto fwd = Get<HistoryMessageForwarded>();
		auto reply = Get<HistoryMessageReply>();
		auto via = Get<HistoryMessageVia>();

		bool media = (_media && _media->isDisplayed());
		if (width >= _maxw) {
			_height = _minh;
			if (media) _media->resizeGetHeight(_maxw);
		} else {
			if (_text.isEmpty()) {
				_height = 0;
			} else {
				int32 textWidth = qMax(width - st::msgPadding.left() - st::msgPadding.right(), 1);
				if (textWidth != _textWidth) {
					textstyleSet(&((out() && !isPost()) ? st::outTextStyle : st::inTextStyle));
					_textWidth = textWidth;
					_textHeight = _text.countHeight(textWidth);
					textstyleRestore();
				}
				_height = st::msgPadding.top() + _textHeight + st::msgPadding.bottom();
			}
			if (media) _height += _media->resizeGetHeight(width);
		}

		if (displayFromName()) {
			if (emptyText()) {
				_height += st::msgPadding.top() + st::msgNameFont->height + st::mediaHeaderSkip;
			} else {
				_height += st::msgNameFont->height;
			}
			int32 l = 0, w = 0;
			countPositionAndSize(l, w);
			fromNameUpdated(w);
		} else if (via && !fwd) {
			int32 l = 0, w = 0;
			countPositionAndSize(l, w);
			via->resize(w - st::msgPadding.left() - st::msgPadding.right());
			if (emptyText() && !displayFromName()) {
				_height += st::msgPadding.top() + st::msgNameFont->height + st::mediaHeaderSkip;
			} else {
				_height += st::msgNameFont->height;
			}
		}

		if (displayForwardedFrom()) {
			int32 l = 0, w = 0;
			countPositionAndSize(l, w);
			int32 fwdheight = ((fwd->_text.maxWidth() > (w - st::msgPadding.left() - st::msgPadding.right())) ? 2 : 1) * st::semiboldFont->height;

			if (emptyText() && !displayFromName()) {
				_height += st::msgPadding.top() + fwdheight + st::mediaHeaderSkip;
			} else {
				_height += fwdheight;
			}
		}

		if (reply) {
			int32 l = 0, w = 0;
			countPositionAndSize(l, w);

			if (emptyText() && !displayFromName() && !Has<HistoryMessageVia>()) {
				_height += st::msgPadding.top() + st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom() + st::mediaHeaderSkip;
			} else {
				_height += st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
			}
			reply->resize(w - st::msgPadding.left() - st::msgPadding.right());
		}
	} else if (_media) {
		_height = _media->resizeGetHeight(width);
	} else {
		_height = 0;
	}
	if (auto keyboard = inlineReplyKeyboard()) {
		int32 l = 0, w = 0;
		countPositionAndSize(l, w);

		int h = st::msgBotKbButton.margin + keyboard->naturalHeight();
		_height += h;
		keyboard->resize(w, h - st::msgBotKbButton.margin);
	}

	_height += marginTop() + marginBottom();
	return _height;
}

bool HistoryMessage::hasPoint(int x, int y) const {
	int left = 0, width = 0, height = _height;
	countPositionAndSize(left, width);
	if (width < 1) return false;

	if (drawBubble()) {
		int top = marginTop();
		QRect r(left, top, width, height - top - marginBottom());
		return r.contains(x, y);
	} else if (_media) {
		return _media->hasPoint(x - left, y - marginTop());
	} else {
		return false;
	}
}

bool HistoryMessage::pointInTime(int32 right, int32 bottom, int x, int y, InfoDisplayType type) const {
	int32 infoRight = right, infoBottom = bottom;
	switch (type) {
	case InfoDisplayDefault:
		infoRight -= st::msgPadding.right() - st::msgDateDelta.x();
		infoBottom -= st::msgPadding.bottom() - st::msgDateDelta.y();
	break;
	case InfoDisplayOverImage:
		infoRight -= st::msgDateImgDelta + st::msgDateImgPadding.x();
		infoBottom -= st::msgDateImgDelta + st::msgDateImgPadding.y();
	break;
	}
	int32 dateX = infoRight - HistoryMessage::infoWidth() + HistoryMessage::timeLeft();
	int32 dateY = infoBottom - st::msgDateFont->height;
	return QRect(dateX, dateY, HistoryMessage::timeWidth(), st::msgDateFont->height).contains(x, y);
}

HistoryTextState HistoryMessage::getState(int x, int y, HistoryStateRequest request) const {
	HistoryTextState result;

	int left = 0, width = 0, height = _height;
	countPositionAndSize(left, width);

	if (width < 1) return result;

	auto keyboard = inlineReplyKeyboard();
	if (keyboard) {
		int h = st::msgBotKbButton.margin + keyboard->naturalHeight();
		height -= h;
	}

	if (drawBubble()) {
		auto fwd = Get<HistoryMessageForwarded>();
		auto via = Get<HistoryMessageVia>();
		auto reply = Get<HistoryMessageReply>();

		int top = marginTop();
		QRect r(left, top, width, height - top - marginBottom());
		QRect trect(r.marginsAdded(-st::msgPadding));
		if (displayFromName()) {
			if (y >= trect.top() && y < trect.top() + st::msgNameFont->height) {
				if (x >= trect.left() && x < trect.left() + trect.width() && x < trect.left() + author()->nameText.maxWidth()) {
					result.link = author()->openLink();
					return result;
				}
				if (via && !fwd && x >= trect.left() + author()->nameText.maxWidth() + st::msgServiceFont->spacew && x < trect.left() + author()->nameText.maxWidth() + st::msgServiceFont->spacew + via->_width) {
					result.link = via->_lnk;
					return result;
				}
			}
			trect.setTop(trect.top() + st::msgNameFont->height);
		}
		if (displayForwardedFrom()) {
			int32 fwdheight = ((fwd->_text.maxWidth() > trect.width()) ? 2 : 1) * st::semiboldFont->height;
			if (y >= trect.top() && y < trect.top() + fwdheight) {
				bool breakEverywhere = (fwd->_text.countHeight(trect.width()) > 2 * st::semiboldFont->height);
				auto textRequest = request.forText();
				if (breakEverywhere) {
					textRequest.flags |= Text::StateRequest::Flag::BreakEverywhere;
				}
				textstyleSet(&st::inFwdTextStyle);
				result = fwd->_text.getState(x - trect.left(), y - trect.top(), trect.width(), textRequest);
				textstyleRestore();
				result.symbol = 0;
				result.afterSymbol = false;
				if (breakEverywhere) {
					result.cursor = HistoryInForwardedCursorState;
				} else {
					result.cursor = HistoryDefaultCursorState;
				}
				return result;
			}
			trect.setTop(trect.top() + fwdheight);
		}
		if (reply) {
			int32 h = st::msgReplyPadding.top() + st::msgReplyBarSize.height() + st::msgReplyPadding.bottom();
			if (y >= trect.top() && y < trect.top() + h) {
				if (reply->replyToMsg && y >= trect.top() + st::msgReplyPadding.top() && y < trect.top() + st::msgReplyPadding.top() + st::msgReplyBarSize.height() && x >= trect.left() && x < trect.left() + trect.width()) {
					result.link = reply->replyToLink();
				}
				return result;
			}
			trect.setTop(trect.top() + h);
		}
		if (via && !displayFromName() && !displayForwardedFrom()) {
			if (x >= trect.left() && y >= trect.top() && y < trect.top() + st::msgNameFont->height && x < trect.left() + via->_width) {
				result.link = via->_lnk;
				return result;
			}
			trect.setTop(trect.top() + st::msgNameFont->height);
		}

		bool inDate = false, mediaDisplayed = _media && _media->isDisplayed();
		if (!mediaDisplayed || !_media->customInfoLayout()) {
			inDate = HistoryMessage::pointInTime(r.x() + r.width(), r.y() + r.height(), x, y, InfoDisplayDefault);
		}

		if (mediaDisplayed) {
			trect.setBottom(trect.bottom() - _media->height());
			if (y >= r.bottom() - _media->height()) {
				result = _media->getState(x - r.left(), y - (r.bottom() - _media->height()), request);
				result.symbol += _text.length();
			}
		}
		if (!mediaDisplayed || (y < r.bottom() - _media->height())) {
			textstyleSet(&((out() && !isPost()) ? st::outTextStyle : st::inTextStyle));
			result = _text.getState(x - trect.x(), y - trect.y(), trect.width(), request.forText());
			textstyleRestore();
		}
		if (inDate) {
			result.cursor = HistoryInDateCursorState;
		}
	} else if (_media) {
		result = _media->getState(x - left, y - marginTop(), request);
		result.symbol += _text.length();
	}

	if (keyboard) {
		int top = height + st::msgBotKbButton.margin - marginBottom();
		if (x >= left && x < left + width && y >= top && y < _height - marginBottom()) {
			result.link = keyboard->getState(x - left, y - top);
			return result;
		}
	}

	return result;
}

TextSelection HistoryMessage::adjustSelection(TextSelection selection, TextSelectType type) const {
	if (!_media || selection.to <= _text.length()) {
		return _text.adjustSelection(selection, type);
	}
	auto mediaSelection = _media->adjustSelection(toMediaSelection(selection), type);
	if (selection.from >= _text.length()) {
		return fromMediaSelection(mediaSelection);
	}
	auto textSelection = _text.adjustSelection(selection, type);
	return { textSelection.from, fromMediaSelection(mediaSelection).to };
}

void HistoryMessage::drawInDialog(Painter &p, const QRect &r, bool act, const HistoryItem *&cacheFor, Text &cache) const {
	if (cacheFor != this) {
		cacheFor = this;
		QString msg(inDialogsText());
		if ((!_history->peer->isUser() || out()) && !isPost() && !isEmpty()) {
			TextCustomTagsMap custom;
			custom.insert(QChar('c'), qMakePair(textcmdStartLink(1), textcmdStopLink()));
			msg = lng_message_with_from(lt_from, textRichPrepare((author() == App::self()) ? lang(lng_from_you) : author()->shortName()), lt_message, textRichPrepare(msg));
			cache.setRichText(st::dialogsTextFont, msg, _textDlgOptions, custom);
		} else {
			cache.setText(st::dialogsTextFont, msg, _textDlgOptions);
		}
	}
	if (r.width()) {
		textstyleSet(&(act ? st::dialogsTextStyleActive : st::dialogsTextStyle));
		p.setFont(st::dialogsTextFont);
		p.setPen(act ? st::dialogsTextFgActive : (emptyText() ? st::dialogsTextFgService : st::dialogsTextFg));
		cache.drawElided(p, r.left(), r.top(), r.width(), r.height() / st::dialogsTextFont->height);
		textstyleRestore();
	}
}

QString HistoryMessage::notificationHeader() const {
    return (!_history->peer->isUser() && !isPost()) ? from()->name : QString();
}

QString HistoryMessage::notificationText() const {
	QString msg(inDialogsText());
    if (msg.size() > 0xFF) msg = msg.mid(0, 0xFF) + qsl("...");
    return msg;
}

bool HistoryMessage::displayFromPhoto() const {
	return hasFromPhoto() && !isAttachedToPrevious();
}

bool HistoryMessage::hasFromPhoto() const {
	return (Adaptive::Wide() || (!out() && !history()->peer->isUser())) && !isPost() && !isEmpty();
}

HistoryMessage::~HistoryMessage() {
	_media.clear();
	if (auto reply = Get<HistoryMessageReply>()) {
		reply->clearData(this);
	}
}

void HistoryService::setMessageByAction(const MTPmessageAction &action) {
	QList<ClickHandlerPtr> links;
	LangString text = lang(lng_message_empty);
	QString from = textcmdLink(1, _from->name);

	switch (action.type()) {
	case mtpc_messageActionChatAddUser: {
		const auto &d(action.c_messageActionChatAddUser());
		const auto &v(d.vusers.c_vector().v);
		bool foundSelf = false;
		for (int32 i = 0, l = v.size(); i < l; ++i) {
			if (v.at(i).v == MTP::authedId()) {
				foundSelf = true;
				break;
			}
		}
		if (v.size() == 1) {
			UserData *u = App::user(peerFromUser(v.at(0)));
			if (u == _from) {
				text = lng_action_user_joined(lt_from, from);
			} else {
				links.push_back(MakeShared<PeerOpenClickHandler>(u));
				text = lng_action_add_user(lt_from, from, lt_user, textcmdLink(2, u->name));
			}
		} else if (v.isEmpty()) {
			text = lng_action_add_user(lt_from, from, lt_user, "somebody");
		} else {
			for (int32 i = 0, l = v.size(); i < l; ++i) {
				UserData *u = App::user(peerFromUser(v.at(i)));
				QString linkText = textcmdLink(i + 2, u->name);
				if (i == 0) {
					text = linkText;
				} else if (i + 1 < l) {
					text = lng_action_add_users_and_one(lt_accumulated, text, lt_user, linkText);
				} else {
					text = lng_action_add_users_and_last(lt_accumulated, text, lt_user, linkText);
				}
				links.push_back(MakeShared<PeerOpenClickHandler>(u));
			}
			text = lng_action_add_users_many(lt_from, from, lt_users, text);
		}
		if (foundSelf) {
			if (history()->peer->isMegagroup()) {
				history()->peer->asChannel()->mgInfo->joinedMessageFound = true;
			}
		}
	} break;

	case mtpc_messageActionChatJoinedByLink: {
		const auto &d(action.c_messageActionChatJoinedByLink());
		//if (true || peerFromUser(d.vinviter_id) == _from->id) {
			text = lng_action_user_joined_by_link(lt_from, from);
		//} else {
		//	UserData *u = App::user(App::peerFromUser(d.vinviter_id));
		//	links.push_back(MakeShared<PeerOpenClickHandler>(u));
		//	text = lng_action_user_joined_by_link_from(lt_from, from, lt_inviter, textcmdLink(2, u->name));
		//}
		if (_from->isSelf() && history()->peer->isMegagroup()) {
			history()->peer->asChannel()->mgInfo->joinedMessageFound = true;
		}
	} break;

	case mtpc_messageActionChatCreate: {
		const auto &d(action.c_messageActionChatCreate());
		text = lng_action_created_chat(lt_from, from, lt_title, textClean(qs(d.vtitle)));
	} break;

	case mtpc_messageActionChannelCreate: {
		const auto &d(action.c_messageActionChannelCreate());
		if (isPost()) {
			text = lng_action_created_channel(lt_title, textClean(qs(d.vtitle)));
		} else {
			text = lng_action_created_chat(lt_from, from, lt_title, textClean(qs(d.vtitle)));
		}
	} break;

	case mtpc_messageActionHistoryClear: {
		text = QString();
	} break;

	case mtpc_messageActionChatDeletePhoto: {
		text = isPost() ? lang(lng_action_removed_photo_channel) : lng_action_removed_photo(lt_from, from);
	} break;

	case mtpc_messageActionChatDeleteUser: {
		const auto &d(action.c_messageActionChatDeleteUser());
		if (peerFromUser(d.vuser_id) == _from->id) {
			text = lng_action_user_left(lt_from, from);
		} else {
			UserData *u = App::user(peerFromUser(d.vuser_id));
			links.push_back(MakeShared<PeerOpenClickHandler>(u));
			text = lng_action_kick_user(lt_from, from, lt_user, textcmdLink(2, u->name));
		}
	} break;

	case mtpc_messageActionChatEditPhoto: {
		const auto &d(action.c_messageActionChatEditPhoto());
		if (d.vphoto.type() == mtpc_photo) {
			_media.reset(new HistoryPhoto(this, history()->peer, d.vphoto.c_photo(), st::msgServicePhotoWidth));
		}
		text = isPost() ? lang(lng_action_changed_photo_channel) : lng_action_changed_photo(lt_from, from);
	} break;

	case mtpc_messageActionChatEditTitle: {
		const auto &d(action.c_messageActionChatEditTitle());
		text = isPost() ? lng_action_changed_title_channel(lt_title, textClean(qs(d.vtitle))) : lng_action_changed_title(lt_from, from, lt_title, textClean(qs(d.vtitle)));
	} break;

	case mtpc_messageActionChatMigrateTo: {
		_flags |= MTPDmessage_ClientFlag::f_is_group_migrate;
		const auto &d(action.c_messageActionChatMigrateTo());
		if (true/*PeerData *channel = App::channelLoaded(d.vchannel_id.v)*/) {
			text = lang(lng_action_group_migrate);
		} else {
			text = lang(lng_contacts_loading);
		}
	} break;

	case mtpc_messageActionChannelMigrateFrom: {
		_flags |= MTPDmessage_ClientFlag::f_is_group_migrate;
		const auto &d(action.c_messageActionChannelMigrateFrom());
		if (true/*PeerData *chat = App::chatLoaded(d.vchat_id.v)*/) {
			text = lang(lng_action_group_migrate);
		} else {
			text = lang(lng_contacts_loading);
		}
	} break;

	case mtpc_messageActionPinMessage: {
		if (updatePinnedText(&from, &text)) {
			auto pinned = Get<HistoryServicePinned>();
			t_assert(pinned != nullptr);

			links.push_back(pinned->lnk);
		}
	} break;

	default: from = QString(); break;
	}

	textstyleSet(&st::serviceTextStyle);
	_text.setText(st::msgServiceFont, text, _historySrvOptions);
	textstyleRestore();
	if (!from.isEmpty()) {
		_text.setLink(1, MakeShared<PeerOpenClickHandler>(_from));
	}
	for (int32 i = 0, l = links.size(); i < l; ++i) {
		_text.setLink(i + 2, links.at(i));
	}
}

bool HistoryService::updatePinned(bool force) {
	auto pinned = Get<HistoryServicePinned>();
	t_assert(pinned != nullptr);

	if (!force) {
		if (!pinned->msgId || pinned->msg) {
			return true;
		}
	}

	if (!pinned->lnk) {
		pinned->lnk.reset(new GoToMessageClickHandler(history()->peer->id, pinned->msgId));
	}
	bool gotDependencyItem = false;
	if (!pinned->msg) {
		pinned->msg = App::histItemById(channelId(), pinned->msgId);
		if (pinned->msg) {
			App::historyRegDependency(this, pinned->msg);
			gotDependencyItem = true;
		}
	}
	if (pinned->msg) {
		updatePinnedText();
	} else if (force) {
		if (pinned->msgId > 0) {
			pinned->msgId = 0;
			gotDependencyItem = true;
		}
		updatePinnedText();
	}
	if (force) {
		if (gotDependencyItem && App::wnd()) {
			App::wnd()->notifySettingGot();
		}
	}
	return (pinned->msg || !pinned->msgId);
}

bool HistoryService::updatePinnedText(const QString *pfrom, QString *ptext) {
	bool result = false;
	QString from, text;
	if (pfrom) {
		from = *pfrom;
	} else {
		from = textcmdLink(1, _from->name);
	}

	ClickHandlerPtr second;
	auto pinned = Get<HistoryServicePinned>();
	if (pinned && pinned->msg) {
		HistoryMedia *media = pinned->msg->getMedia();
		QString mediaText;
		switch (media ? media->type() : MediaTypeCount) {
		case MediaTypePhoto: mediaText = lang(lng_action_pinned_media_photo); break;
		case MediaTypeVideo: mediaText = lang(lng_action_pinned_media_video); break;
		case MediaTypeContact: mediaText = lang(lng_action_pinned_media_contact); break;
		case MediaTypeFile: mediaText = lang(lng_action_pinned_media_file); break;
		case MediaTypeGif: mediaText = lang(lng_action_pinned_media_gif); break;
		case MediaTypeSticker: mediaText = lang(lng_action_pinned_media_sticker); break;
		case MediaTypeLocation: mediaText = lang(lng_action_pinned_media_location); break;
		case MediaTypeMusicFile: mediaText = lang(lng_action_pinned_media_audio); break;
		case MediaTypeVoiceFile: mediaText = lang(lng_action_pinned_media_voice); break;
		}
		if (mediaText.isEmpty()) {
			QString original = pinned->msg->originalText().text;
			int32 cutat = 0, limit = PinnedMessageTextLimit, size = original.size();
			for (; limit > 0;) {
				--limit;
				if (cutat >= size) break;
				if (original.at(cutat).isLowSurrogate() && cutat + 1 < size && original.at(cutat + 1).isHighSurrogate()) {
					cutat += 2;
				} else {
					++cutat;
				}
			}
			if (!limit && cutat + 5 < size) {
				original = original.mid(0, cutat) + qstr("...");
			}
			text = lng_action_pinned_message(lt_from, from, lt_text, textcmdLink(2, original));
		} else {
			text = lng_action_pinned_media(lt_from, from, lt_media, textcmdLink(2, mediaText));
		}
		second = pinned->lnk;
		result = true;
	} else if (pinned && pinned->msgId) {
		text = lng_action_pinned_media(lt_from, from, lt_media, textcmdLink(2, lang(lng_contacts_loading)));
		second = pinned->lnk;
		result = true;
	} else {
		text = lng_action_pinned_media(lt_from, from, lt_media, lang(lng_deleted_message));
	}
	if (ptext) {
		*ptext = text;
	} else {
		setServiceText(text);
		_text.setLink(1, MakeShared<PeerOpenClickHandler>(_from));
		if (second) {
			_text.setLink(2, second);
		}
		if (history()->textCachedFor == this) {
			history()->textCachedFor = 0;
		}
		if (App::main()) {
			App::main()->dlgUpdated(history(), id);
		}
		App::historyUpdateDependent(this);
	}
	return result;
}

HistoryService::HistoryService(History *history, const MTPDmessageService &msg) :
	HistoryItem(history, msg.vid.v, mtpCastFlags(msg.vflags.v), ::date(msg.vdate), msg.has_from_id() ? msg.vfrom_id.v : 0) {
	if (msg.has_reply_to_msg_id()) {
		UpdateComponents(HistoryServicePinned::Bit());
		MsgId pinnedMsgId = Get<HistoryServicePinned>()->msgId = msg.vreply_to_msg_id.v;
		if (!updatePinned() && App::api()) {
			App::api()->requestMessageData(history->peer->asChannel(), pinnedMsgId, std_::make_unique<HistoryDependentItemCallback>(fullId()));
		}
	}
	setMessageByAction(msg.vaction);
}

HistoryService::HistoryService(History *history, MsgId msgId, QDateTime date, const QString &msg, MTPDmessage::Flags flags, int32 from) :
	HistoryItem(history, msgId, flags, date, from) {
	_text.setText(st::msgServiceFont, msg, _historySrvOptions);
}

void HistoryService::initDimensions() {
	_maxw = _text.maxWidth() + st::msgServicePadding.left() + st::msgServicePadding.right();
	_minh = _text.minHeight();
	if (_media) _media->initDimensions();
}

void HistoryService::countPositionAndSize(int32 &left, int32 &width) const {
	left = st::msgServiceMargin.left();
	int32 maxwidth = _history->width;
	if (Adaptive::Wide()) {
		maxwidth = qMin(maxwidth, int32(st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left()));
	}
	width = maxwidth - st::msgServiceMargin.left() - st::msgServiceMargin.left();
}

TextWithEntities HistoryService::selectedText(TextSelection selection) const {
	return _text.originalTextWithEntities((selection == FullSelection) ? AllTextSelection : selection);
}

QString HistoryService::inDialogsText() const {
	return _text.originalText(AllTextSelection, ExpandLinksNone);
}

QString HistoryService::inReplyText() const {
	QString result = HistoryService::inDialogsText();
	return result.trimmed().startsWith(author()->name) ? result.trimmed().mid(author()->name.size()).trimmed() : result;
}

void HistoryService::setServiceText(const QString &text) {
	textstyleSet(&st::serviceTextStyle);
	_text.setText(st::msgServiceFont, text, _historySrvOptions);
	textstyleRestore();
	setPendingInitDimensions();
	_textWidth = -1;
	_textHeight = 0;
}

void HistoryService::draw(Painter &p, const QRect &r, TextSelection selection, uint64 ms) const {
	int height = _height - st::msgServiceMargin.top() - st::msgServiceMargin.bottom();

	QRect clip(r);
	int dateh = 0, unreadbarh = 0;
	if (auto date = Get<HistoryMessageDate>()) {
		dateh = date->height();
		//if (clip.intersects(QRect(0, 0, _history->width, dateh))) {
		//	date->paint(p, 0, _history->width);
		//}
		p.translate(0, dateh);
		clip.translate(0, -dateh);
		height -= dateh;
	}
	if (auto unreadbar = Get<HistoryMessageUnreadBar>()) {
		unreadbarh = unreadbar->height();
		if (clip.intersects(QRect(0, 0, _history->width, unreadbarh))) {
			unreadbar->paint(p, 0, _history->width);
		}
		p.translate(0, unreadbarh);
		clip.translate(0, -unreadbarh);
		height -= unreadbarh;
	}

	HistoryLayout::PaintContext context(ms, clip, selection);
	HistoryLayout::ServiceMessagePainter::paint(p, this, context, height);

	if (int skiph = dateh + unreadbarh) {
		p.translate(0, -skiph);
	}
}

int32 HistoryService::resizeGetHeight_(int32 width) {
	_height = displayedDateHeight();
	if (auto unreadbar = Get<HistoryMessageUnreadBar>()) {
		_height += unreadbar->height();
	}

	if (_text.isEmpty()) {
		_textHeight = 0;
	} else {
		int32 maxwidth = _history->width;
		if (Adaptive::Wide()) {
			maxwidth = qMin(maxwidth, int32(st::msgMaxWidth + 2 * st::msgPhotoSkip + 2 * st::msgMargin.left()));
		}
		if (width > maxwidth) width = maxwidth;
		width -= st::msgServiceMargin.left() + st::msgServiceMargin.left(); // two small margins
		if (width < st::msgServicePadding.left() + st::msgServicePadding.right() + 1) width = st::msgServicePadding.left() + st::msgServicePadding.right() + 1;

		int32 nwidth = qMax(width - st::msgPadding.left() - st::msgPadding.right(), 0);
		if (nwidth != _textWidth) {
			_textWidth = nwidth;
			textstyleSet(&st::serviceTextStyle);
			_textHeight = _text.countHeight(nwidth);
			textstyleRestore();
		}
		if (width >= _maxw) {
			_height += _minh;
		} else {
			_height += _textHeight;
		}
		_height += st::msgServicePadding.top() + st::msgServicePadding.bottom() + st::msgServiceMargin.top() + st::msgServiceMargin.bottom();
		if (_media) {
			_height += st::msgServiceMargin.top() + _media->resizeGetHeight(_media->currentWidth());
		}
	}

	return _height;
}

bool HistoryService::hasPoint(int x, int y) const {
	int left = 0, width = 0, height = _height - st::msgServiceMargin.top() - st::msgServiceMargin.bottom(); // two small margins
	countPositionAndSize(left, width);
	if (width < 1) return false;

	if (int dateh = displayedDateHeight()) {
		y -= dateh;
		height -= dateh;
	}
	if (auto unreadbar = Get<HistoryMessageUnreadBar>()) {
		int unreadbarh = unreadbar->height();
		y -= unreadbarh;
		height -= unreadbarh;
	}

	if (_media) {
		height -= st::msgServiceMargin.top() + _media->height();
	}
	return QRect(left, st::msgServiceMargin.top(), width, height).contains(x, y);
}

HistoryTextState HistoryService::getState(int x, int y, HistoryStateRequest request) const {
	HistoryTextState result;

	int left = 0, width = 0, height = _height - st::msgServiceMargin.top() - st::msgServiceMargin.bottom(); // two small margins
	countPositionAndSize(left, width);
	if (width < 1) return result;

	if (int dateh = displayedDateHeight()) {
		y -= dateh;
		height -= dateh;
	}
	if (auto unreadbar = Get<HistoryMessageUnreadBar>()) {
		int unreadbarh = unreadbar->height();
		y -= unreadbarh;
		height -= unreadbarh;
	}

	if (_media) {
		height -= st::msgServiceMargin.top() + _media->height();
	}
	QRect trect(QRect(left, st::msgServiceMargin.top(), width, height).marginsAdded(-st::msgServicePadding));
	if (trect.contains(x, y)) {
		textstyleSet(&st::serviceTextStyle);
		auto textRequest = request.forText();
		textRequest.align = style::al_center;
		result = _text.getState(x - trect.x(), y - trect.y(), trect.width(), textRequest);
		textstyleRestore();
	} else if (_media) {
		result = _media->getState(x - st::msgServiceMargin.left() - (width - _media->maxWidth()) / 2, y - st::msgServiceMargin.top() - height - st::msgServiceMargin.top(), request);
	}
	return result;
}

void HistoryService::drawInDialog(Painter &p, const QRect &r, bool act, const HistoryItem *&cacheFor, Text &cache) const {
	if (cacheFor != this) {
		cacheFor = this;
		cache.setText(st::dialogsTextFont, inDialogsText(), _textDlgOptions);
	}
	QRect tr(r);
	p.setPen(act ? st::dialogsTextFgActive : st::dialogsTextFgService);
	cache.drawElided(p, tr.left(), tr.top(), tr.width(), tr.height() / st::dialogsTextFont->height);
}

QString HistoryService::notificationText() const {
    QString msg = _text.originalText();
    if (msg.size() > 0xFF) msg = msg.mid(0, 0xFF) + qsl("...");
    return msg;
}

void HistoryService::applyEditionToEmpty() {
	TextWithEntities textWithEntities = { QString(), EntitiesInText() };
	setServiceText(QString());
	removeMedia();

	finishEditionToEmpty();
}

void HistoryService::removeMedia() {
	if (!_media) return;

	bool mediaWasDisplayed = _media->isDisplayed();
	_media.clear();
	if (mediaWasDisplayed) {
		_textWidth = -1;
		_textHeight = 0;
	}
}

int32 HistoryService::addToOverview(AddToOverviewMethod method) {
	if (!indexInOverview()) return 0;

	int32 result = 0;
	if (auto media = getMedia()) {
		MediaOverviewType type = serviceMediaToOverviewType(media);
		if (type != OverviewCount) {
			if (history()->addToOverview(type, id, method)) {
				result |= (1 << type);
			}
		}
	}
	return result;
}

void HistoryService::eraseFromOverview() {
	if (auto media = getMedia()) {
		MediaOverviewType type = serviceMediaToOverviewType(media);
		if (type != OverviewCount) {
			history()->eraseFromOverview(type, id);
		}
	}
}

HistoryService::~HistoryService() {
	if (auto pinned = Get<HistoryServicePinned>()) {
		if (pinned->msg) {
			App::historyUnregDependency(this, pinned->msg);
		}
	}
	_media.clear();
}

HistoryJoined::HistoryJoined(History *history, const QDateTime &inviteDate, UserData *inviter, MTPDmessage::Flags flags)
	: HistoryService(history, clientMsgId(), inviteDate, QString(), flags) {
	textstyleSet(&st::serviceTextStyle);
	if (peerToUser(inviter->id) == MTP::authedId()) {
		_text.setText(st::msgServiceFont, lang(history->isMegagroup() ? lng_action_you_joined_group : lng_action_you_joined), _historySrvOptions);
	} else {
		_text.setText(st::msgServiceFont, history->isMegagroup() ? lng_action_add_you_group(lt_from, textcmdLink(1, inviter->name)) : lng_action_add_you(lt_from, textcmdLink(1, inviter->name)), _historySrvOptions);
		_text.setLink(1, MakeShared<PeerOpenClickHandler>(inviter));
	}
	textstyleRestore();
}

void GoToMessageClickHandler::onClickImpl() const {
	if (App::main()) {
		HistoryItem *current = App::mousedItem();
		if (current && current->history()->peer->id == peer()) {
			App::main()->pushReplyReturn(current);
		}
		Ui::showPeerHistory(peer(), msgid());
	}
}

void CommentsClickHandler::onClickImpl() const {
	if (App::main() && peerIsChannel(peer())) {
		Ui::showPeerHistory(peer(), msgid());
	}
}
