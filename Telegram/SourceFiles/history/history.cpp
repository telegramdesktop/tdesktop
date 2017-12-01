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
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "history/history.h"

#include "history/history_message.h"
#include "history/history_media_types.h"
#include "history/history_service.h"
#include "dialogs/dialogs_indexed_list.h"
#include "styles/style_dialogs.h"
#include "data/data_drafts.h"
#include "lang/lang_keys.h"
#include "apiwrap.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "storage/localstorage.h"
#include "observer_peer.h"
#include "auth_session.h"
#include "window/notifications_manager.h"
#include "calls/calls_instance.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"

namespace {

constexpr auto kStatusShowClientsideTyping = 6000;
constexpr auto kStatusShowClientsideRecordVideo = 6000;
constexpr auto kStatusShowClientsideUploadVideo = 6000;
constexpr auto kStatusShowClientsideRecordVoice = 6000;
constexpr auto kStatusShowClientsideUploadVoice = 6000;
constexpr auto kStatusShowClientsideRecordRound = 6000;
constexpr auto kStatusShowClientsideUploadRound = 6000;
constexpr auto kStatusShowClientsideUploadPhoto = 6000;
constexpr auto kStatusShowClientsideUploadFile = 6000;
constexpr auto kStatusShowClientsideChooseLocation = 6000;
constexpr auto kStatusShowClientsideChooseContact = 6000;
constexpr auto kStatusShowClientsidePlayGame = 10000;
constexpr auto kSetMyActionForMs = 10000;
constexpr auto kNewBlockEachMessage = 50;

auto GlobalPinnedIndex = 0;

HistoryItem *createUnsupportedMessage(History *history, MsgId msgId, MTPDmessage::Flags flags, MsgId replyTo, int32 viaBotId, QDateTime date, int32 from) {
	auto text = TextWithEntities { lng_message_unsupported(lt_link, qsl("https://desktop.telegram.org")) };
	TextUtilities::ParseEntities(text, _historyTextNoMonoOptions.flags);
	text.entities.push_front(EntityInText(EntityInTextItalic, 0, text.text.size()));
	flags &= ~MTPDmessage::Flag::f_post_author;
	return HistoryMessage::create(history, msgId, flags, replyTo, viaBotId, date, from, QString(), text);
}

Storage::SharedMediaType ConvertSharedMediaType(MediaOverviewType type) {
	return static_cast<Storage::SharedMediaType>(type);
}

MediaOverviewType ConvertSharedMediaType(Storage::SharedMediaType type) {
	return static_cast<MediaOverviewType>(type);
}

} // namespace

void HistoryInit() {
	HistoryInitMessages();
	HistoryInitMedia();
}

History::History(const PeerId &peerId)
: peer(App::peer(peerId))
, lastItemTextCache(st::dialogsTextWidthMin)
, cloudDraftTextCache(st::dialogsTextWidthMin)
, _mute(isNotifyMuted(peer->notify))
, _sendActionText(st::dialogsTextWidthMin) {
	if (peer->isUser() && peer->asUser()->botInfo) {
		outboxReadBefore = INT_MAX;
	}
	for (auto &countData : _overviewCountData) {
		countData = -1; // not loaded yet
	}
}

void History::clearLastKeyboard() {
	if (lastKeyboardId) {
		if (lastKeyboardId == lastKeyboardHiddenId) {
			lastKeyboardHiddenId = 0;
		}
		lastKeyboardId = 0;
		if (auto main = App::main()) {
			main->updateBotKeyboard(this);
		}
	}
	lastKeyboardInited = true;
	lastKeyboardFrom = 0;
}

bool History::canHaveFromPhotos() const {
	if (peer->isUser() && !Adaptive::ChatWide()) {
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
		Auth().api().saveDraftToCloudDelayed(from);
	}
}

void History::createLocalDraftFromCloud() {
	auto draft = cloudDraft();
	if (Data::draftIsNull(draft) || !draft->date.isValid()) return;

	auto existing = localDraft();
	if (Data::draftIsNull(existing) || !existing->date.isValid() || draft->date >= existing->date) {
		if (!existing) {
			setLocalDraft(std::make_unique<Data::Draft>(draft->textWithTags, draft->msgId, draft->cursor, draft->previewCancelled));
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

Data::Draft *History::createCloudDraft(Data::Draft *fromDraft) {
	if (Data::draftIsNull(fromDraft)) {
		setCloudDraft(std::make_unique<Data::Draft>(TextWithTags(), 0, MessageCursor(), false));
		cloudDraft()->date = QDateTime();
	} else {
		auto existing = cloudDraft();
		if (!existing) {
			setCloudDraft(std::make_unique<Data::Draft>(fromDraft->textWithTags, fromDraft->msgId, fromDraft->cursor, fromDraft->previewCancelled));
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

	return cloudDraft();
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

void History::clearEditDraft() {
	_editDraft = nullptr;
}

void History::draftSavedToCloud() {
	updateChatListEntry();
	if (App::main()) App::main()->writeDrafts(this);
}

SelectedItemSet History::validateForwardDraft() {
	auto result = SelectedItemSet();
	auto count = 0;
	for_const (auto &fullMsgId, _forwardDraft) {
		if (auto item = App::histItemById(fullMsgId)) {
			result.insert(++count, item);
		}
	}
	if (result.size() != _forwardDraft.size()) {
		setForwardDraft(result);
	}
	return result;
}

void History::setForwardDraft(const SelectedItemSet &items) {
	_forwardDraft.clear();
	_forwardDraft.reserve(items.size());
	for_const (auto item, items) {
		_forwardDraft.push_back(item->fullId());
	}
}

bool History::updateSendActionNeedsAnimating(UserData *user, const MTPSendMessageAction &action) {
	using Type = SendAction::Type;
	if (action.type() == mtpc_sendMessageCancelAction) {
		unregSendAction(user);
		return false;
	}

	auto ms = getms();
	switch (action.type()) {
	case mtpc_sendMessageTypingAction: _typing.insert(user, ms + kStatusShowClientsideTyping); break;
	case mtpc_sendMessageRecordVideoAction: _sendActions.insert(user, { Type::RecordVideo, ms + kStatusShowClientsideRecordVideo }); break;
	case mtpc_sendMessageUploadVideoAction: _sendActions.insert(user, { Type::UploadVideo, ms + kStatusShowClientsideUploadVideo, action.c_sendMessageUploadVideoAction().vprogress.v }); break;
	case mtpc_sendMessageRecordAudioAction: _sendActions.insert(user, { Type::RecordVoice, ms + kStatusShowClientsideRecordVoice }); break;
	case mtpc_sendMessageUploadAudioAction: _sendActions.insert(user, { Type::UploadVoice, ms + kStatusShowClientsideUploadVoice, action.c_sendMessageUploadAudioAction().vprogress.v }); break;
	case mtpc_sendMessageRecordRoundAction: _sendActions.insert(user, { Type::RecordRound, ms + kStatusShowClientsideRecordRound }); break;
	case mtpc_sendMessageUploadRoundAction: _sendActions.insert(user, { Type::UploadRound, ms + kStatusShowClientsideUploadRound }); break;
	case mtpc_sendMessageUploadPhotoAction: _sendActions.insert(user, { Type::UploadPhoto, ms + kStatusShowClientsideUploadPhoto, action.c_sendMessageUploadPhotoAction().vprogress.v }); break;
	case mtpc_sendMessageUploadDocumentAction: _sendActions.insert(user, { Type::UploadFile, ms + kStatusShowClientsideUploadFile, action.c_sendMessageUploadDocumentAction().vprogress.v }); break;
	case mtpc_sendMessageGeoLocationAction: _sendActions.insert(user, { Type::ChooseLocation, ms + kStatusShowClientsideChooseLocation }); break;
	case mtpc_sendMessageChooseContactAction: _sendActions.insert(user, { Type::ChooseContact, ms + kStatusShowClientsideChooseContact }); break;
	case mtpc_sendMessageGamePlayAction: {
		auto it = _sendActions.find(user);
		if (it == _sendActions.end() || it->type == Type::PlayGame || it->until <= ms) {
			_sendActions.insert(user, { Type::PlayGame, ms + kStatusShowClientsidePlayGame });
		}
	} break;
	default: return false;
	}
	return updateSendActionNeedsAnimating(ms, true);
}

bool History::mySendActionUpdated(SendAction::Type type, bool doing) {
	auto ms = getms(true);
	auto i = _mySendActions.find(type);
	if (doing) {
		if (i == _mySendActions.cend()) {
			_mySendActions.insert(type, ms + kSetMyActionForMs);
		} else if (i.value() > ms + (kSetMyActionForMs / 2)) {
			return false;
		} else {
			i.value() = ms + kSetMyActionForMs;
		}
	} else {
		if (i == _mySendActions.cend()) {
			return false;
		} else if (i.value() <= ms) {
			return false;
		} else {
			_mySendActions.erase(i);
		}
	}
	return true;
}

bool History::paintSendAction(Painter &p, int x, int y, int availableWidth, int outerWidth, style::color color, TimeMs ms) {
	if (_sendActionAnimation) {
		_sendActionAnimation.paint(p, color, x, y + st::normalFont->ascent, outerWidth, ms);
		auto animationWidth = _sendActionAnimation.width();
		x += animationWidth;
		availableWidth -= animationWidth;
		p.setPen(color);
		_sendActionText.drawElided(p, x, y, availableWidth);
		return true;
	}
	return false;
}

bool History::updateSendActionNeedsAnimating(TimeMs ms, bool force) {
	auto changed = force;
	for (auto i = _typing.begin(), e = _typing.end(); i != e;) {
		if (ms >= i.value()) {
			i = _typing.erase(i);
			changed = true;
		} else {
			++i;
		}
	}
	for (auto i = _sendActions.begin(); i != _sendActions.cend();) {
		if (ms >= i.value().until) {
			i = _sendActions.erase(i);
			changed = true;
		} else {
			++i;
		}
	}
	if (changed) {
		QString newTypingString;
		auto typingCount = _typing.size();
		if (typingCount > 2) {
			newTypingString = lng_many_typing(lt_count, typingCount);
		} else if (typingCount > 1) {
			newTypingString = lng_users_typing(lt_user, _typing.begin().key()->firstName, lt_second_user, (_typing.end() - 1).key()->firstName);
		} else if (typingCount) {
			newTypingString = peer->isUser() ? lang(lng_typing) : lng_user_typing(lt_user, _typing.begin().key()->firstName);
		} else if (!_sendActions.isEmpty()) {
			// Handles all actions except game playing.
			using Type = SendAction::Type;
			auto sendActionString = [](Type type, const QString &name) -> QString {
				switch (type) {
				case Type::RecordVideo: return name.isEmpty() ? lang(lng_send_action_record_video) : lng_user_action_record_video(lt_user, name);
				case Type::UploadVideo: return name.isEmpty() ? lang(lng_send_action_upload_video) : lng_user_action_upload_video(lt_user, name);
				case Type::RecordVoice: return name.isEmpty() ? lang(lng_send_action_record_audio) : lng_user_action_record_audio(lt_user, name);
				case Type::UploadVoice: return name.isEmpty() ? lang(lng_send_action_upload_audio) : lng_user_action_upload_audio(lt_user, name);
				case Type::RecordRound: return name.isEmpty() ? lang(lng_send_action_record_round) : lng_user_action_record_round(lt_user, name);
				case Type::UploadRound: return name.isEmpty() ? lang(lng_send_action_upload_round) : lng_user_action_upload_round(lt_user, name);
				case Type::UploadPhoto: return name.isEmpty() ? lang(lng_send_action_upload_photo) : lng_user_action_upload_photo(lt_user, name);
				case Type::UploadFile: return name.isEmpty() ? lang(lng_send_action_upload_file) : lng_user_action_upload_file(lt_user, name);
				case Type::ChooseLocation:
				case Type::ChooseContact: return name.isEmpty() ? lang(lng_typing) : lng_user_typing(lt_user, name);
				default: break;
				};
				return QString();
			};
			for (auto i = _sendActions.cbegin(), e = _sendActions.cend(); i != e; ++i) {
				newTypingString = sendActionString(i->type, peer->isUser() ? QString() : i.key()->firstName);
				if (!newTypingString.isEmpty()) {
					_sendActionAnimation.start(i->type);
					break;
				}
			}

			// Everyone in sendActions are playing a game.
			if (newTypingString.isEmpty()) {
				int playingCount = _sendActions.size();
				if (playingCount > 2) {
					newTypingString = lng_many_playing_game(lt_count, playingCount);
				} else if (playingCount > 1) {
					newTypingString = lng_users_playing_game(lt_user, _sendActions.begin().key()->firstName, lt_second_user, (_sendActions.end() - 1).key()->firstName);
				} else {
					newTypingString = peer->isUser() ? lang(lng_playing_game) : lng_user_playing_game(lt_user, _sendActions.begin().key()->firstName);
				}
				_sendActionAnimation.start(Type::PlayGame);
			}
		}
		if (typingCount > 0) {
			_sendActionAnimation.start(SendAction::Type::Typing);
		} else if (newTypingString.isEmpty()) {
			_sendActionAnimation.stop();
		}
		if (_sendActionString != newTypingString) {
			_sendActionString = newTypingString;
			_sendActionText.setText(st::dialogsTextStyle, _sendActionString, _textNameOptions);
		}
	}
	auto result = (!_typing.isEmpty() || !_sendActions.isEmpty());
	if (changed || result) {
		App::histories().sendActionAnimationUpdated().notify({
			this,
			_sendActionAnimation.width(),
			st::normalFont->height,
			changed
		});
	}
	return result;
}

void ChannelHistory::getRangeDifference() {
	auto fromId = MsgId(0);
	auto toId = MsgId(0);
	for (auto blockIndex = 0, blocksCount = int(blocks.size()); blockIndex < blocksCount; ++blockIndex) {
		auto block = blocks[blockIndex];
		for (auto itemIndex = 0, itemsCount = int(block->items.size()); itemIndex < itemsCount; ++itemIndex) {
			auto item = block->items[itemIndex];
			if (item->id > 0) {
				fromId = item->id;
				break;
			}
		}
		if (fromId) break;
	}
	if (!fromId) return;

	for (auto blockIndex = blocks.size(); blockIndex > 0;) {
		auto block = blocks[--blockIndex];
		for (auto itemIndex = block->items.size(); itemIndex > 0;) {
			auto item = block->items[--itemIndex];
			if (item->id > 0) {
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

	int limit = _rangeDifferenceToId + 1 - _rangeDifferenceFromId;

	auto filter = MTP_channelMessagesFilter(MTP_flags(0), MTP_vector<MTPMessageRange>(1, MTP_messageRange(MTP_int(_rangeDifferenceFromId), MTP_int(_rangeDifferenceToId))));
	auto flags = MTPupdates_GetChannelDifference::Flag::f_force;
	_rangeDifferenceRequestId = MTP::send(MTPupdates_GetChannelDifference(MTP_flags(flags), peer->asChannel()->inputChannel, filter, MTP_int(pts), MTP_int(limit)), App::main()->rpcDone(&MainWidget::gotRangeDifference, peer->asChannel()));
}

HistoryJoined *ChannelHistory::insertJoinedMessage(bool unread) {
	if (_joinedMessage || !peer->asChannel()->amIn() || (peer->isMegagroup() && peer->asChannel()->mgInfo->joinedMessageFound)) {
		return _joinedMessage;
	}

	auto inviter = (peer->asChannel()->inviter > 0) ? App::userLoaded(peer->asChannel()->inviter) : nullptr;
	if (!inviter) return nullptr;

	MTPDmessage::Flags flags = 0;
	if (inviter->id == Auth().userPeerId()) {
		unread = false;
	//} else if (unread) {
	//	flags |= MTPDmessage::Flag::f_unread;
	}

	auto inviteDate = peer->asChannel()->inviteDate;
	if (unread) _maxReadMessageDate = inviteDate;
	if (isEmpty()) {
		_joinedMessage = HistoryJoined::create(this, inviteDate, inviter, flags);
		addNewItem(_joinedMessage, unread);
		return _joinedMessage;
	}

	for (auto blockIndex = blocks.size(); blockIndex > 0;) {
		auto block = blocks[--blockIndex];
		for (auto itemIndex = block->items.size(); itemIndex > 0;) {
			auto item = block->items[--itemIndex];

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
	if (!blocks.empty()) {
		firstDate = blocks.front()->items.front()->date;
		lastDate = blocks.back()->items.back()->date;
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

	for (auto blockIndex = blocks.size(); blockIndex > 0;) {
		auto block = blocks[--blockIndex];
		for (auto itemIndex = block->items.size(); itemIndex > 0;) {
			auto item = block->items[--itemIndex];
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

not_null<History*> Histories::findOrInsert(const PeerId &peerId) {
	auto i = map.constFind(peerId);
	if (i == map.cend()) {
		auto history = peerIsChannel(peerId) ? static_cast<History*>(new ChannelHistory(peerId)) : (new History(peerId));
		i = map.insert(peerId, history);
	}
	return i.value();
}

not_null<History*> Histories::findOrInsert(const PeerId &peerId, int32 unreadCount, int32 maxInboxRead, int32 maxOutboxRead) {
	auto i = map.constFind(peerId);
	if (i == map.cend()) {
		auto history = peerIsChannel(peerId) ? static_cast<History*>(new ChannelHistory(peerId)) : (new History(peerId));
		i = map.insert(peerId, history);
		history->setUnreadCount(unreadCount);
		history->inboxReadBefore = maxInboxRead + 1;
		history->outboxReadBefore = maxOutboxRead + 1;
	} else {
		auto history = i.value();
		if (unreadCount > history->unreadCount()) {
			history->setUnreadCount(unreadCount);
		}
		accumulate_max(history->inboxReadBefore, maxInboxRead + 1);
		accumulate_max(history->outboxReadBefore, maxOutboxRead + 1);
	}
	return i.value();
}

void Histories::clear() {
	App::historyClearMsgs();

	_pinnedDialogs.clear();
	auto temp = base::take(map);
	for_const (auto history, temp) {
		delete history;
	}

	_unreadFull = _unreadMuted = 0;
	Notify::unreadCounterUpdated();
	App::historyClearItems();
	typing.clear();
}

void Histories::regSendAction(History *history, UserData *user, const MTPSendMessageAction &action, TimeId when) {
	if (history->updateSendActionNeedsAnimating(user, action)) {
		user->madeAction(when);

		auto i = typing.find(history);
		if (i == typing.cend()) {
			typing.insert(history, getms());
			_a_typings.start();
		}
	}
}

void Histories::step_typings(TimeMs ms, bool timer) {
	for (auto i = typing.begin(), e = typing.end(); i != e;) {
		if (i.key()->updateSendActionNeedsAnimating(ms)) {
			++i;
		} else {
			i = typing.erase(i);
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
			for_const (auto &row, markup->rows) {
				for_const (auto &button, row) {
					if (button.type == HistoryMessageReplyMarkup::Button::Type::SwitchInline) {
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
	auto peer = peerFromMessage(msg);
	if (!peer) return nullptr;

	auto result = App::history(peer)->addNewMessage(msg, type);
	if (result && type == NewMessageUnread) {
		checkForSwitchInlineButton(result);
	}
	return result;
}

int Histories::unreadBadge() const {
	return _unreadFull - (Global::IncludeMuted() ? 0 : _unreadMuted);
}

bool Histories::unreadOnlyMuted() const {
	return Global::IncludeMuted() ? (_unreadMuted >= _unreadFull) : false;
}

void Histories::setIsPinned(History *history, bool isPinned) {
	if (isPinned) {
		_pinnedDialogs.insert(history);
		if (_pinnedDialogs.size() > Global::PinnedDialogsCountMax()) {
			auto minIndex = GlobalPinnedIndex + 1;
			auto minIndexHistory = (History*)nullptr;
			for_const (auto pinned, _pinnedDialogs) {
				if (pinned->getPinnedIndex() < minIndex) {
					minIndex = pinned->getPinnedIndex();
					minIndexHistory = pinned;
				}
			}
			Assert(minIndexHistory != nullptr);
			minIndexHistory->setPinnedDialog(false);
		}
	} else {
		_pinnedDialogs.remove(history);
	}
}

void Histories::clearPinned() {
	for (auto pinned : base::take(_pinnedDialogs)) {
		pinned->setPinnedDialog(false);
	}
}

int Histories::pinnedCount() const {
	return _pinnedDialogs.size();
}

QList<History*> Histories::getPinnedOrder() const {
	QMap<int, History*> sorter;
	for_const (auto pinned, _pinnedDialogs) {
		sorter.insert(pinned->getPinnedIndex(), pinned);
	}
	QList<History*> result;
	for (auto i = sorter.cend(), e = sorter.cbegin(); i != e;) {
		--i;
		result.push_back(i.value());
	}
	return result;
}

void Histories::savePinnedToServer() const {
	auto order = getPinnedOrder();
	auto peers = QVector<MTPInputPeer>();
	peers.reserve(order.size());
	for_const (auto history, order) {
		peers.push_back(history->peer->input);
	}
	auto flags = MTPmessages_ReorderPinnedDialogs::Flag::f_force;
	MTP::send(MTPmessages_ReorderPinnedDialogs(MTP_flags(flags), MTP_vector(peers)));
}

void Histories::selfDestructIn(not_null<HistoryItem*> item, TimeMs delay) {
	_selfDestructItems.push_back(item->fullId());
	if (!_selfDestructTimer.isActive() || _selfDestructTimer.remainingTime() > delay) {
		_selfDestructTimer.callOnce(delay);
	}
}

void Histories::checkSelfDestructItems() {
	auto now = getms(true);
	auto nextDestructIn = TimeMs(0);
	for (auto i = _selfDestructItems.begin(); i != _selfDestructItems.cend();) {
		if (auto item = App::histItemById(*i)) {
			if (auto destructIn = item->getSelfDestructIn(now)) {
				if (nextDestructIn > 0) {
					accumulate_min(nextDestructIn, destructIn);
				} else {
					nextDestructIn = destructIn;
				}
				++i;
			} else {
				i = _selfDestructItems.erase(i);
			}
		} else {
			i = _selfDestructItems.erase(i);
		}
	}
	if (nextDestructIn > 0) {
		_selfDestructTimer.callOnce(nextDestructIn);
	}
}

HistoryItem *History::createItem(const MTPMessage &msg, bool applyServiceAction, bool detachExistingItem) {
	auto msgId = MsgId(0);
	switch (msg.type()) {
	case mtpc_messageEmpty: msgId = msg.c_messageEmpty().vid.v; break;
	case mtpc_message: msgId = msg.c_message().vid.v; break;
	case mtpc_messageService: msgId = msg.c_messageService().vid.v; break;
	}
	if (!msgId) return nullptr;

	auto result = App::histItemById(channelId(), msgId);
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
	case mtpc_messageEmpty: {
		auto message = HistoryService::PreparedText { lang(lng_message_empty) };
		result = HistoryService::create(this, msg.c_messageEmpty().vid.v, date(), message);
	} break;

	case mtpc_message: {
		auto &m = msg.c_message();
		enum class MediaCheckResult {
			Good,
			Unsupported,
			Empty,
			HasTimeToLive,
		};
		auto badMedia = MediaCheckResult::Good;
		if (m.has_media()) switch (m.vmedia.type()) {
		case mtpc_messageMediaEmpty:
		case mtpc_messageMediaContact: break;
		case mtpc_messageMediaGeo:
			switch (m.vmedia.c_messageMediaGeo().vgeo.type()) {
			case mtpc_geoPoint: break;
			case mtpc_geoPointEmpty: badMedia = MediaCheckResult::Empty; break;
			default: badMedia = MediaCheckResult::Unsupported; break;
			}
			break;
		case mtpc_messageMediaVenue:
			switch (m.vmedia.c_messageMediaVenue().vgeo.type()) {
			case mtpc_geoPoint: break;
			case mtpc_geoPointEmpty: badMedia = MediaCheckResult::Empty; break;
			default: badMedia = MediaCheckResult::Unsupported; break;
			}
			break;
		case mtpc_messageMediaGeoLive:
			switch (m.vmedia.c_messageMediaGeoLive().vgeo.type()) {
			case mtpc_geoPoint: break;
			case mtpc_geoPointEmpty: badMedia = MediaCheckResult::Empty; break;
			default: badMedia = MediaCheckResult::Unsupported; break;
			}
			break;
		case mtpc_messageMediaPhoto: {
			auto &photo = m.vmedia.c_messageMediaPhoto();
			if (photo.has_ttl_seconds()) {
				badMedia = MediaCheckResult::HasTimeToLive;
			} else if (!photo.has_photo()) {
				badMedia = MediaCheckResult::Empty;
			} else {
				switch (photo.vphoto.type()) {
				case mtpc_photo: break;
				case mtpc_photoEmpty: badMedia = MediaCheckResult::Empty; break;
				default: badMedia = MediaCheckResult::Unsupported; break;
				}
			}
		} break;
		case mtpc_messageMediaDocument: {
			auto &document = m.vmedia.c_messageMediaDocument();
			if (document.has_ttl_seconds()) {
				badMedia = MediaCheckResult::HasTimeToLive;
			} else if (!document.has_document()) {
				badMedia = MediaCheckResult::Empty;
			} else {
				switch (document.vdocument.type()) {
				case mtpc_document: break;
				case mtpc_documentEmpty: badMedia = MediaCheckResult::Empty; break;
				default: badMedia = MediaCheckResult::Unsupported; break;
				}
			}
		} break;
		case mtpc_messageMediaWebPage:
			switch (m.vmedia.c_messageMediaWebPage().vwebpage.type()) {
			case mtpc_webPage:
			case mtpc_webPageEmpty:
			case mtpc_webPagePending: break;
			case mtpc_webPageNotModified:
			default: badMedia = MediaCheckResult::Unsupported; break;
			}
			break;
		case mtpc_messageMediaGame:
		switch (m.vmedia.c_messageMediaGame().vgame.type()) {
			case mtpc_game: break;
			default: badMedia = MediaCheckResult::Unsupported; break;
			}
			break;
		case mtpc_messageMediaInvoice:
			break;
		case mtpc_messageMediaUnsupported:
		default: badMedia = MediaCheckResult::Unsupported; break;
		}
		if (badMedia == MediaCheckResult::Unsupported) {
			result = createUnsupportedMessage(this, m.vid.v, m.vflags.v, m.vreply_to_msg_id.v, m.vvia_bot_id.v, date(m.vdate), m.vfrom_id.v);
		} else if (badMedia == MediaCheckResult::Empty) {
			auto message = HistoryService::PreparedText { lang(lng_message_empty) };
			result = HistoryService::create(this, m.vid.v, date(m.vdate), message, m.vflags.v, m.has_from_id() ? m.vfrom_id.v : 0);
		} else if (badMedia == MediaCheckResult::HasTimeToLive) {
			result = HistoryService::create(this, m);
		} else {
			result = HistoryMessage::create(this, m);
		}
	} break;

	case mtpc_messageService: {
		auto &m = msg.c_messageService();
		if (m.vaction.type() == mtpc_messageActionPhoneCall) {
			result = HistoryMessage::create(this, m);
		} else {
			result = HistoryService::create(this, m);
		}

		if (applyServiceAction) {
			auto &action = m.vaction;
			switch (action.type()) {
			case mtpc_messageActionChatAddUser: {
				auto &d = action.c_messageActionChatAddUser();
				if (auto megagroup = peer->asMegagroup()) {
					auto mgInfo = megagroup->mgInfo.get();
					Assert(mgInfo != nullptr);
					auto &v = d.vusers.v;
					for (auto i = 0, l = v.size(); i != l; ++i) {
						if (auto user = App::userLoaded(peerFromUser(v[i]))) {
							if (!base::contains(mgInfo->lastParticipants, user)) {
								mgInfo->lastParticipants.push_front(user);
								mgInfo->lastParticipantsStatus |= MegagroupInfo::LastParticipantsAdminsOutdated;
								Notify::peerUpdatedDelayed(peer, Notify::PeerUpdate::Flag::MembersChanged);
								Auth().data().addNewMegagroupParticipant(megagroup, user);
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
				auto &d = action.c_messageActionChatJoinedByLink();
				if (auto megagroup = peer->asMegagroup()) {
					auto mgInfo = megagroup->mgInfo.get();
					Assert(mgInfo != nullptr);
					if (auto user = result->from()->asUser()) {
						if (!base::contains(mgInfo->lastParticipants, user)) {
							mgInfo->lastParticipants.push_front(user);
							Notify::peerUpdatedDelayed(peer, Notify::PeerUpdate::Flag::MembersChanged);
							Auth().data().addNewMegagroupParticipant(megagroup, user);
						}
						if (user->botInfo) {
							mgInfo->bots.insert(user);
							if (mgInfo->botStatus != 0 && mgInfo->botStatus < 2) {
								mgInfo->botStatus = 2;
							}
						}
					}
				}
			} break;

			case mtpc_messageActionChatDeletePhoto: {
				auto chat = peer->asChat();
				if (chat) chat->setPhoto(MTP_chatPhotoEmpty());
			} break;

			case mtpc_messageActionChatDeleteUser: {
				auto &d = action.c_messageActionChatDeleteUser();
				auto uid = peerFromUser(d.vuser_id);
				if (lastKeyboardFrom == uid) {
					clearLastKeyboard();
				}
				if (auto megagroup = peer->asMegagroup()) {
					if (auto user = App::userLoaded(uid)) {
						auto mgInfo = megagroup->mgInfo.get();
						Assert(mgInfo != nullptr);
						auto i = ranges::find(
							mgInfo->lastParticipants,
							user,
							[](not_null<UserData*> user) { return user.get(); });
						if (i != mgInfo->lastParticipants.end()) {
							mgInfo->lastParticipants.erase(i);
							Notify::peerUpdatedDelayed(peer, Notify::PeerUpdate::Flag::MembersChanged);
						}
						Auth().data().removeMegagroupParticipant(megagroup, user);
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
							Notify::peerUpdatedDelayed(peer, Notify::PeerUpdate::Flag::AdminsChanged);
						}
						mgInfo->bots.remove(user);
						if (mgInfo->bots.empty() && mgInfo->botStatus > 0) {
							mgInfo->botStatus = -1;
						}
					}
				}
			} break;

			case mtpc_messageActionChatEditPhoto: {
				auto &d = action.c_messageActionChatEditPhoto();
				if (d.vphoto.type() == mtpc_photo) {
					auto &sizes = d.vphoto.c_photo().vsizes.v;
					if (!sizes.isEmpty()) {
						auto photo = App::feedPhoto(d.vphoto.c_photo());
						if (photo) photo->peer = peer;
						auto &smallSize = sizes.front();
						auto &bigSize = sizes.back();
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
				auto &d = action.c_messageActionChatEditTitle();
				if (auto chat = peer->asChat()) {
					chat->setName(qs(d.vtitle));
				}
			} break;

			case mtpc_messageActionChatMigrateTo: {
				if (auto chat = peer->asChat()) {
					chat->addFlags(MTPDchat::Flag::f_deactivated);
				}
				//auto &d = action.c_messageActionChatMigrateTo();
				//auto channel = App::channelLoaded(d.vchannel_id.v);
			} break;

			case mtpc_messageActionChannelMigrateFrom: {
				//auto &d = action.c_messageActionChannelMigrateFrom();
				//auto chat = App::chatLoaded(d.vchat_id.v);
			} break;

			case mtpc_messageActionPinMessage: {
				if (m.has_reply_to_msg_id() && result) {
					if (auto channel = result->history()->peer->asChannel()) {
						channel->setPinnedMessageId(m.vreply_to_msg_id.v);
					}
				}
			} break;

			case mtpc_messageActionPhoneCall: {
				Calls::Current().newServiceMessage().notify(result->fullId());
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

HistoryItem *History::createItemForwarded(MsgId id, MTPDmessage::Flags flags, QDateTime date, UserId from, const QString &postAuthor, HistoryMessage *msg) {
	return HistoryMessage::create(this, id, flags, date, from, postAuthor, msg);
}

HistoryItem *History::createItemDocument(MsgId id, MTPDmessage::Flags flags, UserId viaBotId, MsgId replyTo, QDateTime date, UserId from, const QString &postAuthor, DocumentData *doc, const QString &caption, const MTPReplyMarkup &markup) {
	return HistoryMessage::create(this, id, flags, replyTo, viaBotId, date, from, postAuthor, doc, caption, markup);
}

HistoryItem *History::createItemPhoto(MsgId id, MTPDmessage::Flags flags, UserId viaBotId, MsgId replyTo, QDateTime date, UserId from, const QString &postAuthor, PhotoData *photo, const QString &caption, const MTPReplyMarkup &markup) {
	return HistoryMessage::create(this, id, flags, replyTo, viaBotId, date, from, postAuthor, photo, caption, markup);
}

HistoryItem *History::createItemGame(MsgId id, MTPDmessage::Flags flags, UserId viaBotId, MsgId replyTo, QDateTime date, UserId from, const QString &postAuthor, GameData *game, const MTPReplyMarkup &markup) {
	return HistoryMessage::create(this, id, flags, replyTo, viaBotId, date, from, postAuthor, game, markup);
}

HistoryItem *History::addNewService(MsgId msgId, QDateTime date, const QString &text, MTPDmessage::Flags flags, bool newMsg) {
	auto message = HistoryService::PreparedText { text };
	return addNewItem(HistoryService::create(this, msgId, date, message, flags), newMsg);
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
	auto applyServiceAction = (type == NewMessageUnread);
	auto detachExistingItem = (type != NewMessageLast);
	auto item = createItem(msg, applyServiceAction, detachExistingItem);
	if (!item || !item->detached()) {
		return item;
	}
	return addNewItem(item, (type == NewMessageUnread));
}

HistoryItem *History::addToHistory(const MTPMessage &msg) {
	return createItem(msg, false, false);
}

HistoryItem *History::addNewForwarded(MsgId id, MTPDmessage::Flags flags, QDateTime date, UserId from, const QString &postAuthor, HistoryMessage *item) {
	return addNewItem(createItemForwarded(id, flags, date, from, postAuthor, item), true);
}

HistoryItem *History::addNewDocument(MsgId id, MTPDmessage::Flags flags, UserId viaBotId, MsgId replyTo, QDateTime date, UserId from, const QString &postAuthor, DocumentData *doc, const QString &caption, const MTPReplyMarkup &markup) {
	return addNewItem(createItemDocument(id, flags, viaBotId, replyTo, date, from, postAuthor, doc, caption, markup), true);
}

HistoryItem *History::addNewPhoto(MsgId id, MTPDmessage::Flags flags, UserId viaBotId, MsgId replyTo, QDateTime date, UserId from, const QString &postAuthor, PhotoData *photo, const QString &caption, const MTPReplyMarkup &markup) {
	return addNewItem(createItemPhoto(id, flags, viaBotId, replyTo, date, from, postAuthor, photo, caption, markup), true);
}

HistoryItem *History::addNewGame(MsgId id, MTPDmessage::Flags flags, UserId viaBotId, MsgId replyTo, QDateTime date, UserId from, const QString &postAuthor, GameData *game, const MTPReplyMarkup &markup) {
	return addNewItem(createItemGame(id, flags, viaBotId, replyTo, date, from, postAuthor, game, markup), true);
}

bool History::addToOverview(MediaOverviewType type, MsgId msgId, AddToOverviewMethod method) {
	_overview[type].insert(msgId);
	if (method == AddToOverviewNew) {
		if (_overviewCountData[type] > 0) {
			++_overviewCountData[type];
		}
		Notify::mediaOverviewUpdated(peer, type);
	}
	return true;
}

void History::eraseFromOverview(MediaOverviewType type, MsgId msgId) {
	auto i = _overview[type].find(msgId);
	if (i == _overview[type].cend()) return;

	_overview[type].erase(i);
	if (_overviewCountData[type] > 0) {
		--_overviewCountData[type];
	}
	Notify::mediaOverviewUpdated(peer, type);
}

void History::setUnreadMentionsCount(int count) {
	if (_unreadMentions.size() > count) {
		LOG(("API Warning: real mentions count is greater than received mentions count"));
		count = _unreadMentions.size();
	}
	_unreadMentionsCount = count;
}

bool History::addToUnreadMentions(MsgId msgId, AddToOverviewMethod method) {
	auto allLoaded = _unreadMentionsCount ? (_unreadMentions.size() >= *_unreadMentionsCount) : false;
	if (allLoaded) {
		if (method == AddToOverviewNew) {
			++*_unreadMentionsCount;
			_unreadMentions.insert(msgId);
			return true;
		}
	} else if (!_unreadMentions.empty() && method != AddToOverviewNew) {
		_unreadMentions.insert(msgId);
		return true;
	}
	return false;
}

void History::eraseFromUnreadMentions(MsgId msgId) {
	_unreadMentions.remove(msgId);
	if (_unreadMentionsCount) {
		if (*_unreadMentionsCount > 0) {
			--*_unreadMentionsCount;
		}
	}
	Notify::peerUpdatedDelayed(peer, Notify::PeerUpdate::Flag::UnreadMentionsChanged);
}

void History::addUnreadMentionsSlice(const MTPmessages_Messages &result) {
	auto count = 0;
	auto messages = (const QVector<MTPMessage>*)nullptr;
	auto getMessages = [](auto &list) {
		App::feedUsers(list.vusers);
		App::feedChats(list.vchats);
		return &list.vmessages.v;
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
		count = d.vcount.v;
	} break;

	case mtpc_messages_channelMessages: {
		LOG(("API Error: unexpected messages.channelMessages! (History::addUnreadMentionsSlice)"));
		auto &d = result.c_messages_channelMessages();
		messages = getMessages(d);
		count = d.vcount.v;
	} break;

	case mtpc_messages_messagesNotModified: {
		LOG(("API Error: received messages.messagesNotModified! (History::addUnreadMentionsSlice)"));
	} break;

	default: Unexpected("type in History::addUnreadMentionsSlice");
	}

	auto added = false;
	if (messages) {
		for (auto &message : *messages) {
			if (auto item = addToHistory(message)) {
				if (item->mentionsMe() && item->isMediaUnread()) {
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
	Notify::peerUpdatedDelayed(peer, Notify::PeerUpdate::Flag::UnreadMentionsChanged);
}

HistoryItem *History::addNewItem(HistoryItem *adding, bool newMsg) {
	Expects(!isBuildingFrontBlock());
	addItemToBlock(adding);

	setLastMessage(adding);
	if (newMsg) {
		newItemAdded(adding);
	}

	adding->addToOverview(AddToOverviewNew);
	if (IsServerMsgId(adding->id)) {
		if (auto sharedMediaTypes = adding->sharedMediaTypes()) {
			if (newMsg) {
				Auth().storage().add(Storage::SharedMediaAddNew(
					peer->id,
					sharedMediaTypes,
					adding->id));
			} else {
				auto from = loadedAtTop() ? 0 : minMsgId();
				auto till = loadedAtBottom() ? ServerMaxMsgId : maxMsgId();
				Auth().storage().add(Storage::SharedMediaAddExisting(
					peer->id,
					sharedMediaTypes,
					adding->id,
					{ from, till }));
			}
		}
	}
	if (adding->from()->id) {
		if (auto user = adding->from()->asUser()) {
			auto getLastAuthors = [this]() -> std::deque<not_null<UserData*>>* {
				if (auto chat = peer->asChat()) {
					return &chat->lastAuthors;
				} else if (auto channel = peer->asMegagroup()) {
					return &channel->mgInfo->lastParticipants;
				}
				return nullptr;
			};
			if (auto megagroup = peer->asMegagroup()) {
				if (user->botInfo) {
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
					peer->asChannel()->mgInfo->lastParticipantsStatus |= MegagroupInfo::LastParticipantsAdminsOutdated;
				}
				if (index) {
					lastAuthors->push_front(user);
				}
				if (auto megagroup = peer->asMegagroup()) {
					Notify::peerUpdatedDelayed(peer, Notify::PeerUpdate::Flag::MembersChanged);
					Auth().data().addNewMegagroupParticipant(megagroup, user);
				}
			}
		}
		if (adding->definesReplyKeyboard()) {
			auto markupFlags = adding->replyKeyboardFlags();
			if (!(markupFlags & MTPDreplyKeyboardMarkup::Flag::f_selective) || adding->mentionsMe()) {
				auto getMarkupSenders = [this]() -> base::flat_set<not_null<PeerData*>>* {
					if (auto chat = peer->asChat()) {
						return &chat->markupSenders;
					} else if (auto channel = peer->asMegagroup()) {
						return &channel->mgInfo->markupSenders;
					}
					return nullptr;
				};
				if (auto markupSenders = getMarkupSenders()) {
					markupSenders->insert(adding->from());
				}
				if (markupFlags & MTPDreplyKeyboardMarkup_ClientFlag::f_zero) { // zero markup means replyKeyboardHide
					if (lastKeyboardFrom == adding->from()->id || (!lastKeyboardInited && !peer->isChat() && !peer->isMegagroup() && !adding->out())) {
						clearLastKeyboard();
					}
				} else {
					bool botNotInChat = false;
					if (peer->isChat()) {
						botNotInChat = adding->from()->isUser() && (!peer->canWrite() || !peer->asChat()->participants.empty()) && !peer->asChat()->participants.contains(adding->from()->asUser());
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

void History::unregSendAction(UserData *from) {
	auto updateAtMs = TimeMs(0);
	auto i = _typing.find(from);
	if (i != _typing.cend()) {
		updateAtMs = getms();
		i.value() = updateAtMs;
	}
	auto j = _sendActions.find(from);
	if (j != _sendActions.cend()) {
		if (!updateAtMs) updateAtMs = getms();
		j.value().until = updateAtMs;
	}
	if (updateAtMs) {
		updateSendActionNeedsAnimating(updateAtMs, true);
	}
}

void History::newItemAdded(HistoryItem *item) {
	App::checkImageCacheSize();
	if (item->from() && item->from()->isUser()) {
		if (item->from() == item->author()) {
			unregSendAction(item->from()->asUser());
		}
		auto itemServerTime = toServerTime(item->date.toTime_t());
		item->from()->asUser()->madeAction(itemServerTime.v);
	}
	if (item->out()) {
		if (unreadBar) unreadBar->destroyUnreadBar();
		if (!item->unread()) {
			outboxRead(item);
		}
	} else if (item->unread()) {
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

		auto result = _buildingFrontBlock->block = new HistoryBlock(this);
		if (_buildingFrontBlock->expectedItemsCount > 0) {
			result->items.reserve(_buildingFrontBlock->expectedItemsCount + 1);
		}
		result->setIndexInHistory(0);
		blocks.push_front(result);
		for (int i = 1, l = blocks.size(); i < l; ++i) {
			blocks[i]->setIndexInHistory(i);
		}
		return result;
	}

	auto addNewBlock = blocks.empty() || (blocks.back()->items.size() >= kNewBlockEachMessage);
	if (!addNewBlock) {
		return blocks.back();
	}

	auto result = new HistoryBlock(this);
	result->setIndexInHistory(blocks.size());
	blocks.push_back(result);

	result->items.reserve(kNewBlockEachMessage);
	return result;
};

void History::addItemToBlock(HistoryItem *item) {
	Expects(item != nullptr);
	Expects(item->detached());

	auto block = prepareBlockForAddingItem();

	item->attachToBlock(block, block->items.size());
	block->items.push_back(item);
	item->previousItemChanged();

	if (isBuildingFrontBlock() && _buildingFrontBlock->expectedItemsCount > 0) {
		--_buildingFrontBlock->expectedItemsCount;
	}
}

template <int kSharedMediaTypeCount>
void History::addToSharedMedia(std::vector<MsgId> (&medias)[kSharedMediaTypeCount], bool force) {
	auto from = loadedAtTop() ? 0 : minMsgId();
	auto till = loadedAtBottom() ? ServerMaxMsgId : maxMsgId();
	if (from > till) {
		// History is desync, nothing good can be added.
		//// Logging
		auto value = QStringList();
		for (auto block : blocks) {
			auto indices = QStringList();
			auto &items = block->items;
			auto count = int(items.size());
			auto logItem = [&](auto &&item) {
				indices.push_back(QString::number(item->id));
			};
			if (count < 4) {
				for (auto item : items) {
					logItem(item);
				}
			} else {
				auto last = 0;
				auto logLast = [&] {
					logItem(items[last]);
				};
				auto logTill = [&](int till) {
					if (last < till - 1) {
						indices.push_back("...["
							+ QString::number(till - 1 - last)
							+ "]...");
					}
					last = till;
					logLast();
				};
				auto badPair = [&](int index) {
					auto prev = items[index - 1]->id;
					auto next = items[index]->id;
					return IsServerMsgId(prev)
						&& IsServerMsgId(next)
						&& (next < prev);
				};

				logLast();
				for (auto i = 1; i != count - 1; ++i) {
					if (badPair(i) || badPair(i + 1)) {
						logTill(i);
					}
				}
				logTill(count - 1);
			}
			value.push_back(indices.join(","));
		}
		SignalHandlers::setCrashAnnotation("full", value.join(";"));
		Assert(!"History desync caught!");
		//// Logging

		return;
	}
	for (auto i = 0; i != Storage::kSharedMediaTypeCount; ++i) {
		if (force || !medias[i].empty()) {
			auto type = static_cast<Storage::SharedMediaType>(i);
			Auth().storage().add(Storage::SharedMediaAddSlice(
				peer->id,
				type,
				std::move(medias[i]),
				{ from, till }));
		}
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

	auto logged = QStringList();
	logged.push_back(QString::number(minMsgId()));
	logged.push_back(QString::number(maxMsgId()));

	auto minAdded = -1;
	auto maxAdded = -1;

	startBuildingFrontBlock(slice.size());

	for (auto i = slice.cend(), e = slice.cbegin(); i != e;) {
		--i;
		auto adding = createItem(*i, false, true);
		if (!adding) continue;

		if (minAdded < 0 || minAdded > adding->id) {
			minAdded = adding->id;
		}
		if (maxAdded < 0 || maxAdded < adding->id) {
			maxAdded = adding->id;
		}

		addItemToBlock(adding);
	}

	auto block = finishBuildingFrontBlock();
	if (!block) {
		// If no items were added it means we've loaded everything old.
		oldLoaded = true;
	} else if (loadedAtBottom()) { // add photos to overview and authors to lastAuthors
		bool channel = isChannel();
		int32 mask = 0;
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
		for (auto i = block->items.size(); i > 0; --i) {
			auto item = block->items[i - 1];
			mask |= item->addToOverview(AddToOverviewFront);
			if (item->from()->id) {
				if (lastAuthors) { // chats
					if (auto user = item->from()->asUser()) {
						if (!base::contains(*lastAuthors, user)) {
							lastAuthors->push_back(user);
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
		if (mask) {
			Notify::PeerUpdate update(peer);
			update.flags |= Notify::PeerUpdate::Flag::SharedMediaChanged;
			update.mediaTypesMask |= mask;
			Notify::peerUpdatedDelayed(update);
		}
	}

	logged.push_back(QString::number(minAdded));
	logged.push_back(QString::number(maxAdded));
	SignalHandlers::setCrashAnnotation("old_minmaxwas_minmaxadd", logged.join(";"));

	addBlockToSharedMedia(block);

	SignalHandlers::setCrashAnnotation("old_minmaxwas_minmaxadd", "");

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
			setLastMessage(lastAvailableMessage());
		}
	}

	Assert(!isBuildingFrontBlock());
	if (!slice.isEmpty()) {
		auto logged = QStringList();
		logged.push_back(QString::number(minMsgId()));
		logged.push_back(QString::number(maxMsgId()));

		auto minAdded = -1;
		auto maxAdded = -1;

		std::vector<MsgId> medias[Storage::kSharedMediaTypeCount];
		auto atLeastOneAdded = false;
		for (auto i = slice.cend(), e = slice.cbegin(); i != e;) {
			--i;
			auto adding = createItem(*i, false, true);
			if (!adding) continue;

			if (minAdded < 0 || minAdded > adding->id) {
				minAdded = adding->id;
			}
			if (maxAdded < 0 || maxAdded < adding->id) {
				maxAdded = adding->id;
			}

			addItemToBlock(adding);
			atLeastOneAdded = true;
			if (auto types = adding->sharedMediaTypes()) {
				for (auto i = 0; i != Storage::kSharedMediaTypeCount; ++i) {
					auto type = static_cast<Storage::SharedMediaType>(i);
					if (types.test(type)) {
						if (medias[i].empty()) {
							medias[i].reserve(slice.size());
						}
						medias[i].push_back(adding->id);
					}
				}
			}
		}
		logged.push_back(QString::number(minAdded));
		logged.push_back(QString::number(maxAdded));
		SignalHandlers::setCrashAnnotation("new_minmaxwas_minmaxadd", logged.join(";"));

		if (!atLeastOneAdded) {
			newLoaded = true;
			setLastMessage(lastAvailableMessage());
		}
		addToSharedMedia(medias, wasLoadedAtBottom != loadedAtBottom());

		SignalHandlers::setCrashAnnotation("new_minmaxwas_minmaxadd", "");
	}

	if (!wasLoadedAtBottom) {
		checkAddAllToOverview();
	}

	if (isChannel()) asChannelHistory()->checkJoinedMessage();
	checkLastMsg();
}

void History::checkLastMsg() {
	if (lastMsg) {
		if (!newLoaded && !lastMsg->detached()) {
			newLoaded = true;
			checkAddAllToOverview();
		}
	} else if (newLoaded) {
		setLastMessage(lastAvailableMessage());
	}
}

void History::checkAddAllToOverview() {
	if (!loadedAtBottom()) {
		return;
	}

	int32 mask = 0;
	for_const (auto block, blocks) {
		for_const (auto item, block->items) {
			mask |= item->addToOverview(AddToOverviewBack);
		}
	}
	if (mask) {
		Notify::PeerUpdate update(peer);
		update.flags |= Notify::PeerUpdate::Flag::SharedMediaChanged;
		update.mediaTypesMask |= mask;
		Notify::peerUpdatedDelayed(update);
	}
}

void History::addBlockToSharedMedia(HistoryBlock *block) {
	std::vector<MsgId> medias[Storage::kSharedMediaTypeCount];
	if (block) {
		for (auto item : block->items) {
			if (auto types = item->sharedMediaTypes()) {
				for (auto i = 0; i != Storage::kSharedMediaTypeCount; ++i) {
					auto type = static_cast<Storage::SharedMediaType>(i);
					if (types.test(type)) {
						if (medias[i].empty()) {
							medias[i].reserve(block->items.size());
						}
						medias[i].push_back(item->id);
					}
				}
			}
		}
	}
	addToSharedMedia(medias, !block);
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
			if ((*j)->id > 0 && (!(*j)->out() || !showFrom)) {
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
		if (auto migrateTo = App::historyLoaded(peer->migrateTo()->id)) {
			migrateTo->updateChatListEntry();
		}
	}

	showFrom = nullptr;
	Auth().notifications().clearFromHistory(this);

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

HistoryItem *History::lastAvailableMessage() const {
	return isEmpty() ? nullptr : blocks.back()->items.back();
}

void History::setUnreadCount(int newUnreadCount) {
	if (_unreadCount != newUnreadCount) {
		if (newUnreadCount == 1) {
			if (loadedAtBottom()) showFrom = lastAvailableMessage();
			inboxReadBefore = qMax(inboxReadBefore, msgIdForRead());
		} else if (!newUnreadCount) {
			showFrom = nullptr;
			inboxReadBefore = qMax(inboxReadBefore, msgIdForRead() + 1);
		} else {
			if (!showFrom && !unreadBar && loadedAtBottom()) updateShowFrom();
		}
		if (inChatList(Dialogs::Mode::All)) {
			App::histories().unreadIncrement(newUnreadCount - _unreadCount, mute());
			if (!mute() || Global::IncludeMuted()) {
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
		Notify::peerUpdatedDelayed(
			peer,
			Notify::PeerUpdate::Flag::NotificationsEnabled);
	}
}

void History::getNextShowFrom(HistoryBlock *block, int i) {
	if (i >= 0) {
		auto l = block->items.size();
		for (++i; i < l; ++i) {
			if (block->items[i]->id > 0) {
				showFrom = block->items[i];
				return;
			}
		}
	}

	for (auto j = block->indexInHistory() + 1, s = int(blocks.size()); j < s; ++j) {
		block = blocks[j];
		for_const (auto item, block->items) {
			if (item->id > 0) {
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
		scrollTopOffset = (top - scrollTopItem->block()->y() - scrollTopItem->y());
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
		itemTop = blocks[blockIndex]->y() + scrollTopItem->y();
	}
	if (itemTop > top) {
		// go backward through history while we don't find an item that starts above
		do {
			auto block = blocks[blockIndex];
			for (--itemIndex; itemIndex >= 0; --itemIndex) {
				auto item = block->items[itemIndex];
				itemTop = block->y() + item->y();
				if (itemTop <= top) {
					scrollTopItem = item;
					return;
				}
			}
			if (--blockIndex >= 0) {
				itemIndex = blocks[blockIndex]->items.size();
			} else {
				break;
			}
		} while (true);

		scrollTopItem = blocks.front()->items.front();
	} else {
		// go forward through history while we don't find the last item that starts above
		for (int blocksCount = blocks.size(); blockIndex < blocksCount; ++blockIndex) {
			auto block = blocks[blockIndex];
			for (int itemsCount = block->items.size(); itemIndex < itemsCount; ++itemIndex) {
				auto item = block->items[itemIndex];
				itemTop = block->y() + item->y();
				if (itemTop > top) {
					Assert(itemIndex > 0 || blockIndex > 0);
					if (itemIndex > 0) {
						scrollTopItem = block->items[itemIndex - 1];
					} else {
						scrollTopItem = blocks[blockIndex - 1]->items.back();
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
		scrollTopItem = block->items[i];
		return;
	}
	int j = block->indexInHistory() + 1;
	if (j > 0 && j < blocks.size()) {
		scrollTopItem = blocks[j]->items.front();
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
	Expects(blockIndex >= 0);
	Expects(blockIndex < blocks.size());
	Expects(itemIndex >= 0);
	Expects(itemIndex <= blocks[blockIndex]->items.size());

	auto block = blocks[blockIndex];

	newItem->attachToBlock(block, itemIndex);
	block->items.insert(block->items.begin() + itemIndex, newItem);
	newItem->previousItemChanged();
	if (itemIndex + 1 < block->items.size()) {
		for (int i = itemIndex + 1, l = block->items.size(); i < l; ++i) {
			block->items[i]->setIndexInBlock(i);
		}
		block->items[itemIndex + 1]->previousItemChanged();
	} else if (blockIndex + 1 < blocks.size() && !blocks[blockIndex + 1]->items.empty()) {
		blocks[blockIndex + 1]->items.front()->previousItemChanged();
	} else {
		newItem->nextItemChanged();
	}

	return newItem;
}

void History::startBuildingFrontBlock(int expectedItemsCount) {
	Assert(!isBuildingFrontBlock());
	Assert(expectedItemsCount > 0);

	_buildingFrontBlock.reset(new BuildingBlock());
	_buildingFrontBlock->expectedItemsCount = expectedItemsCount;
}

HistoryBlock *History::finishBuildingFrontBlock() {
	Assert(isBuildingFrontBlock());

	// Some checks if there was some message history already
	auto block = _buildingFrontBlock->block;
	if (block) {
		if (blocks.size() > 1) {
			auto last = block->items.back(); // ... item, item, item, last ], [ first, item, item ...
			auto first = blocks[1]->items.front();

			// we've added a new front block, so previous item for
			// the old first item of a first block was changed
			first->previousItemChanged();
		} else {
			block->items.back()->nextItemChanged();
		}
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
		if (auto h = App::historyLoaded(peer->migrateFrom()->id)) {
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
	if (date.isNull()) return 0;
	return (uint64(date.toTime_t()) << 32) | (++_dialogsPosToTopShift);
}

inline uint64 pinnedDialogPos(int pinnedIndex) {
	return 0xFFFFFFFF00000000ULL + pinnedIndex;
}

void History::setLastMessage(HistoryItem *msg) {
	if (msg) {
		if (!lastMsg) Local::removeSavedPeer(peer);
		lastMsg = msg;
		setChatsListDate(msg->date);
	} else {
		lastMsg = 0;
		updateChatListEntry();
	}
}

bool History::needUpdateInChatList() const {
	if (inChatList(Dialogs::Mode::All)) {
		return true;
	} else if (peer->migrateTo()) {
		return false;
	} else if (isPinnedDialog()) {
		return true;
	}
	return !peer->isChannel() || peer->asChannel()->amIn();
}

void History::setChatsListDate(const QDateTime &date) {
	if (!lastMsgDate.isNull() && lastMsgDate >= date) {
		if (!needUpdateInChatList() || !inChatList(Dialogs::Mode::All)) {
			return;
		}
	}
	lastMsgDate = date;
	updateChatListSortPosition();
}

void History::updateChatListSortPosition() {
	auto chatListDate = [this]() {
		if (auto draft = cloudDraft()) {
			if (!Data::draftIsNull(draft) && draft->date > lastMsgDate) {
				return draft->date;
			}
		}
		return lastMsgDate;
	};

	_sortKeyInChatList = isPinnedDialog() ? pinnedDialogPos(_pinnedIndex) : dialogPosFromDate(chatListDate());
	if (auto m = App::main()) {
		if (needUpdateInChatList()) {
			if (_sortKeyInChatList) {
				m->createDialog(this);
				updateChatListEntry();
			} else {
				m->deleteConversation(peer, false);
			}
		}
	}
}

void History::fixLastMessage(bool wasAtBottom) {
	setLastMessage(wasAtBottom ? lastAvailableMessage() : 0);
}

MsgId History::minMsgId() const {
	for (auto block : std::as_const(blocks)) {
		for (auto item : std::as_const(block->items)) {
			if (IsServerMsgId(item->id)) {
				return item->id;
			}
		}
	}
	return 0;
}

MsgId History::maxMsgId() const {
	for (auto block : base::reversed(std::as_const(blocks))) {
		for (auto item : base::reversed(std::as_const(block->items))) {
			if (IsServerMsgId(item->id)) {
				return item->id;
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
	for_const (auto block, blocks) {
		block->setY(y);
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

not_null<History*> History::migrateToOrMe() const {
	if (auto to = peer->migrateTo()) {
		return App::history(to);
	}
	// We could get it by App::history(peer), but we optimize.
	return const_cast<History*>(this);
}

History *History::migrateFrom() const {
	if (auto from = peer->migrateFrom()) {
		return App::history(from);
	}
	return nullptr;
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
	if (leaveItems) {
		Auth().data().markHistoryUnloaded(this);
	} else {
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
		for (auto i = 0; i != OverviewCount; ++i) {
			if (!_overview[i].isEmpty()) {
				_overviewCountData[i] = -1; // not loaded yet
				_overview[i].clear();
				if (!App::quitting()) {
					Notify::mediaOverviewUpdated(peer, MediaOverviewType(i));
				}
			}
		}
		Auth().storage().remove(Storage::SharedMediaRemoveAll(peer->id));
		Auth().data().markHistoryCleared(this);
	}
	clearBlocks(leaveItems);
	if (leaveItems) {
		lastKeyboardInited = false;
	} else {
		setUnreadCount(0);
		if (auto channel = peer->asChannel()) {
			channel->clearPinnedMessage();
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
}

void History::clearUpTill(MsgId availableMinId) {
	auto minId = minMsgId();
	if (!minId || minId >= availableMinId) {
		return;
	}
	do {
		auto item = blocks.front()->items.front();
		auto itemId = item->id;
		if (IsServerMsgId(itemId) && itemId >= availableMinId) {
			if (itemId == availableMinId) {
				auto fromId = 0;
				auto replyToId = 0;
				item->applyEdition(MTP_messageService(
					MTP_flags(0),
					MTP_int(itemId),
					MTP_int(fromId),
					peerToMTP(peer->id),
					MTP_int(replyToId),
					toServerTime(item->date.toTime_t()),
					MTP_messageActionHistoryClear()
				).c_messageService());
			}
			break;
		}
		item->destroy();
	} while (!isEmpty());

	if (!lastMsg) {
		App::main()->checkPeerHistory(peer);
	}
}

void History::applyGroupAdminChanges(
		const base::flat_map<UserId, bool> &changes) {
	for (auto block : blocks) {
		for (auto item : block->items) {
			item->applyGroupAdminChanges(changes);
		}
	}
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
	Assert(indexed != nullptr);
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
	Assert(indexed != nullptr);
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
	Assert(indexed != nullptr);
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
	Assert(letter != 0);
	if (inChatList(list)) {
		chatListLinks(list).remove(letter);
	}
}

void History::addChatListEntryByLetter(Dialogs::Mode list, QChar letter, Dialogs::Row *row) {
	Assert(letter != 0);
	if (inChatList(list)) {
		chatListLinks(list).emplace(letter, row);
	}
}

void History::updateChatListEntry() const {
	if (auto main = App::main()) {
		if (inChatList(Dialogs::Mode::All)) {
			main->dlgUpdated(Dialogs::Mode::All, mainChatListLink(Dialogs::Mode::All));
			if (inChatList(Dialogs::Mode::Important)) {
				main->dlgUpdated(Dialogs::Mode::Important, mainChatListLink(Dialogs::Mode::Important));
			}
		}
	}
}

void History::setPinnedDialog(bool isPinned) {
	setPinnedIndex(isPinned ? (++GlobalPinnedIndex) : 0);
}

void History::setPinnedIndex(int pinnedIndex) {
	if (_pinnedIndex != pinnedIndex) {
		auto wasPinned = isPinnedDialog();
		_pinnedIndex = pinnedIndex;
		updateChatListSortPosition();
		updateChatListEntry();
		if (wasPinned != isPinnedDialog()) {
			Notify::peerUpdatedDelayed(peer, Notify::PeerUpdate::Flag::PinnedChanged);
		}
		App::histories().setIsPinned(this, isPinnedDialog());
	}
}

void History::overviewSliceDone(
		int32 overviewIndex,
		MsgId startMessageId,
		const MTPmessages_Messages &result,
		bool onlyCounts) {
	auto fullCount = 0;
	auto v = (const QVector<MTPMessage>*)nullptr;

	switch (result.type()) {
	case mtpc_messages_messages: {
		auto &d = result.c_messages_messages();
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		v = &d.vmessages.v;
		fullCount = v->size();
		_overviewCountData[overviewIndex] = 0;
	} break;

	case mtpc_messages_messagesSlice: {
		auto &d = result.c_messages_messagesSlice();
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		fullCount = _overviewCountData[overviewIndex] = d.vcount.v;
		v = &d.vmessages.v;
	} break;

	case mtpc_messages_channelMessages: {
		auto &d = result.c_messages_channelMessages();
		if (peer->isChannel()) {
			peer->asChannel()->ptsReceived(d.vpts.v);
		} else {
			LOG(("API Error: received messages.channelMessages when no channel was passed! (History::overviewSliceDone, onlyCounts %1)").arg(Logs::b(onlyCounts)));
		}
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		fullCount = _overviewCountData[overviewIndex] = d.vcount.v;
		v = &d.vmessages.v;
	} break;

	case mtpc_messages_messagesNotModified: {
		LOG(("API Error: received messages.messagesNotModified! (History::overviewSliceDone, onlyCounts %1)").arg(Logs::b(onlyCounts)));
	} break;

	default: return;
	}

	if (!onlyCounts && (!v || v->isEmpty())) {
		_overviewCountData[overviewIndex] = 0;
	}

	auto noSkipRange = MsgRange { startMessageId, startMessageId };
	auto sharedMediaType = ConvertSharedMediaType(
		static_cast<MediaOverviewType>(overviewIndex));
	auto slice = std::vector<MsgId>();
	if (v) {
		slice.reserve(v->size());
		for (auto &message : *v) {
			if (auto item = App::histories().addNewMessage(message, NewMessageExisting)) {
				auto itemId = item->id;
				_overview[overviewIndex].insert(itemId);
				if (item->sharedMediaTypes().test(sharedMediaType)) {
					slice.push_back(itemId);
					accumulate_min(noSkipRange.from, itemId);
					accumulate_max(noSkipRange.till, itemId);
				}
			}
		}
	}
	Auth().storage().add(Storage::SharedMediaAddSlice(
		peer->id,
		sharedMediaType,
		std::move(slice),
		noSkipRange,
		fullCount
	));
}

void History::changeMsgId(MsgId oldId, MsgId newId) {
	for (auto i = 0; i != OverviewCount; ++i) {
		auto j = _overview[i].find(oldId);
		if (j != _overview[i].cend()) {
			_overview[i].erase(j);
			_overview[i].insert(newId);
		}
	}
}

void History::removeBlock(HistoryBlock *block) {
	Expects(block->items.empty());

	if (_buildingFrontBlock && block == _buildingFrontBlock->block) {
		_buildingFrontBlock->block = nullptr;
	}

	int index = block->indexInHistory();
	blocks.erase(blocks.begin() + index);
	if (index < blocks.size()) {
		for (int i = index, l = blocks.size(); i < l; ++i) {
			blocks[i]->setIndexInHistory(i);
		}
		blocks[index]->items.front()->previousItemChanged();
	} else if (!blocks.empty() && !blocks.back()->items.empty()) {
		blocks.back()->items.back()->nextItemChanged();
	}
}

History::~History() {
	clearOnDestroy();
}

int HistoryBlock::resizeGetHeight(int newWidth, bool resizeAllItems) {
	auto y = 0;
	for_const (auto item, items) {
		item->setY(y);
		if (resizeAllItems || item->pendingResize()) {
			y += item->resizeGetHeight(newWidth);
		} else {
			y += item->height();
		}
	}
	_height = y;
	return _height;
}

void HistoryBlock::clear(bool leaveItems) {
	auto itemsList = base::take(items);

	if (leaveItems) {
		for_const (auto item, itemsList) {
			item->detachFast();
		}
	} else {
		for_const (auto item, itemsList) {
			delete item;
		}
	}
}

void HistoryBlock::removeItem(HistoryItem *item) {
	Expects(item->block() == this);

	auto blockIndex = indexInHistory();
	auto itemIndex = item->indexInBlock();
	if (_history->showFrom == item) {
		_history->getNextShowFrom(this, itemIndex);
	}
	if (_history->lastSentMsg == item) {
		_history->lastSentMsg = nullptr;
	}
	if (_history->unreadBar == item) {
		_history->unreadBar = nullptr;
	}
	if (_history->scrollTopItem == item) {
		_history->getNextScrollTopItem(this, itemIndex);
	}

	item->detachFast();
	items.erase(items.begin() + itemIndex);
	for (auto i = itemIndex, l = int(items.size()); i < l; ++i) {
		items[i]->setIndexInBlock(i);
	}
	if (items.empty()) {
		_history->removeBlock(this);
	} else if (itemIndex < items.size()) {
		items[itemIndex]->previousItemChanged();
	} else if (blockIndex + 1 < _history->blocks.size()) {
		_history->blocks[blockIndex + 1]->items.front()->previousItemChanged();
	} else if (!_history->blocks.empty() && !_history->blocks.back()->items.empty()) {
		_history->blocks.back()->items.back()->nextItemChanged();
	}

	if (items.empty()) {
		delete this;
	}
}
