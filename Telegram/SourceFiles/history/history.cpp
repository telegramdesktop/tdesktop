/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history.h"

#include "history/view/history_view_element.h"
#include "history/admin_log/history_admin_log_section.h"
#include "history/history_message.h"
#include "history/history_media_types.h"
#include "history/history_service.h"
#include "history/history_item_components.h"
#include "history/history_inner_widget.h"
#include "dialogs/dialogs_indexed_list.h"
#include "styles/style_dialogs.h"
#include "data/data_drafts.h"
#include "data/data_session.h"
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
#include "storage/storage_feed_messages.h"
#include "data/data_channel_admins.h"
#include "data/data_feed.h"
#include "ui/text_options.h"
#include "core/crash_reports.h"

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
constexpr auto kSkipCloudDraftsFor = TimeId(3);

void checkForSwitchInlineButton(HistoryItem *item) {
	if (item->out() || !item->hasSwitchInlineButton()) {
		return;
	}
	if (const auto user = item->history()->peer->asUser()) {
		if (!user->botInfo || !user->botInfo->inlineReturnPeerId) {
			return;
		}
		if (const auto markup = item->Get<HistoryMessageReplyMarkup>()) {
			for_const (auto &row, markup->rows) {
				for_const (auto &button, row) {
					if (button.type == HistoryMessageMarkupButton::Type::SwitchInline) {
						Notify::switchInlineBotButtonReceived(QString::fromUtf8(button.data));
						return;
					}
				}
			}
		}
	}
}

} // namespace

Histories::Histories()
: _a_typings(animation(this, &Histories::step_typings))
, _selfDestructTimer([this] { checkSelfDestructItems(); }) {
}

History *Histories::find(PeerId peerId) const {
	if (const auto i = _map.find(peerId); i != _map.end()) {
		return i->second.get();
	}
	return nullptr;
}

not_null<History*> Histories::findOrInsert(PeerId peerId) {
	if (const auto result = find(peerId)) {
		return result;
	}
	const auto [i, ok] = _map.emplace(
		peerId,
		std::make_unique<History>(peerId));
	return i->second.get();
}

void Histories::clear() {
	for (const auto &[peerId, history] : _map) {
		history->unloadBlocks();
	}
	App::historyClearMsgs();
	_map.clear();

	_unreadFull = _unreadMuted = 0;
	Notify::unreadCounterUpdated();
	App::historyClearItems();
	typing.clear();
}

void Histories::registerSendAction(
		not_null<History*> history,
		not_null<UserData*> user,
		const MTPSendMessageAction &action,
		TimeId when) {
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
	const auto i = _map.find(peer);
	if (i != _map.cend()) {
		typing.remove(i->second.get());
		_map.erase(i);
	}
}

HistoryItem *Histories::addNewMessage(
		const MTPMessage &msg,
		NewMessageType type) {
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

int Histories::unreadMutedCount() const {
	return _unreadMuted;
}

void Histories::unreadIncrement(int count, bool muted) {
	_unreadFull += count;
	if (muted) {
		_unreadMuted += count;
	}
	if (!muted || Global::IncludeMuted()) {
		Notify::unreadCounterUpdated();
	}
}

void Histories::unreadMuteChanged(int count, bool muted) {
	if (muted) {
		_unreadMuted += count;
	} else {
		_unreadMuted -= count;
	}
	Notify::unreadCounterUpdated();
}

bool Histories::unreadOnlyMuted() const {
	return Global::IncludeMuted() ? (_unreadMuted >= _unreadFull) : false;
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

History::History(const PeerId &peerId)
: Entry(this)
, peer(App::peer(peerId))
, cloudDraftTextCache(st::dialogsTextWidthMin)
, _mute(Auth().data().notifyIsMuted(peer))
, _sendActionText(st::dialogsTextWidthMin) {
	if (const auto user = peer->asUser()) {
		if (user->botInfo) {
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
		if (auto main = App::main()) {
			main->updateBotKeyboard(this);
		}
	}
	lastKeyboardInited = true;
	lastKeyboardFrom = 0;
}

int History::height() const {
	return _height;
}

void History::removeNotification(not_null<HistoryItem*> item) {
	if (!notifies.isEmpty()) {
		for (auto i = notifies.begin(), e = notifies.end(); i != e; ++i) {
			if ((*i) == item) {
				notifies.erase(i);
				break;
			}
		}
	}
}

HistoryItem *History::currentNotification() {
	return notifies.isEmpty() ? 0 : notifies.front();
}

bool History::hasNotification() const {
	return !notifies.isEmpty();
}

void History::skipNotification() {
	if (!notifies.isEmpty()) {
		notifies.pop_front();
	}
}

void History::popNotification(HistoryItem *item) {
	if (!notifies.isEmpty() && notifies.back() == item) notifies.pop_back();
}

bool History::hasPendingResizedItems() const {
	return _flags & Flag::f_has_pending_resized_items;
}

void History::setHasPendingResizedItems() {
	_flags |= Flag::f_has_pending_resized_items;
}

void History::itemRemoved(not_null<HistoryItem*> item) {
	item->removeMainView();
	if (lastMessage() == item) {
		_lastMessage = std::nullopt;
		if (loadedAtBottom()) {
			if (const auto last = lastAvailableMessage()) {
				setLastMessage(last);
			}
		}
		if (const auto channel = peer->asChannel()) {
			if (const auto feed = channel->feed()) {
				// Must be after history->lastMessage() is updated.
				// Otherwise feed last message will be this value again.
				feed->messageRemoved(item);
			}
		}
	}
	itemVanished(item);
}

void History::itemVanished(not_null<HistoryItem*> item) {
	removeNotification(item);
	if (lastKeyboardId == item->id) {
		clearLastKeyboard();
	}
	if ((!item->out() || item->isPost())
		&& item->unread()
		&& unreadCount() > 0) {
		changeUnreadCount(-1);
	}
	if (const auto channel = peer->asChannel()) {
		if (channel->pinnedMessageId() == item->id) {
			channel->clearPinnedMessage();
		}
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
		Auth().api().saveDraftToCloudDelayed(from);
	}
}

void History::createLocalDraftFromCloud() {
	auto draft = cloudDraft();
	if (Data::draftIsNull(draft) || !draft->date) {
		return;
	}

	auto existing = localDraft();
	if (Data::draftIsNull(existing) || !existing->date || draft->date >= existing->date) {
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

Data::Draft *History::createCloudDraft(Data::Draft *fromDraft) {
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
		existing->date = unixtime();
	}

	cloudDraftTextCache.clear();
	updateChatListSortPosition();

	return cloudDraft();
}

bool History::skipCloudDraft(const QString &text, TimeId date) const {
	if (date > 0 && date <= _lastSentDraftTime + kSkipCloudDraftsFor) {
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
	accumulate_max(_lastSentDraftTime, unixtime());
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

HistoryItemsList History::validateForwardDraft() {
	auto result = Auth().data().idsToItems(_forwardDraft);
	if (result.size() != _forwardDraft.size()) {
		setForwardDraft(Auth().data().itemsToIds(result));
	}
	return result;
}

void History::setForwardDraft(MessageIdsList &&items) {
	_forwardDraft = std::move(items);
}

bool History::updateSendActionNeedsAnimating(
		not_null<UserData*> user,
		const MTPSendMessageAction &action) {
	if (peer->isSelf()) {
		return false;
	}

	using Type = SendAction::Type;
	if (action.type() == mtpc_sendMessageCancelAction) {
		clearSendAction(user);
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

bool History::paintSendAction(
		Painter &p,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		style::color color,
		TimeMs ms) {
	if (_sendActionAnimation) {
		_sendActionAnimation.paint(
			p,
			color,
			x,
			y + st::normalFont->ascent,
			outerWidth,
			ms);
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
			_sendActionText.setText(
				st::dialogsTextStyle,
				_sendActionString,
				Ui::NameTextOptions());
		}
	}
	auto result = (!_typing.isEmpty() || !_sendActions.isEmpty());
	if (changed || (result && !anim::Disabled())) {
		App::histories().sendActionAnimationUpdated().notify({
			this,
			_sendActionAnimation.width(),
			st::normalFont->height,
			changed
		});
	}
	return result;
}

HistoryItem *History::createItem(
		const MTPMessage &message,
		bool detachExistingItem) {
	const auto messageId = idFromMessage(message);
	if (!messageId) {
		return nullptr;
	}

	if (const auto result = App::histItemById(channelId(), messageId)) {
		if (detachExistingItem) {
			result->removeMainView();
		}
		if (message.type() == mtpc_message) {
			const auto media = message.c_message().has_media()
				? &message.c_message().vmedia
				: nullptr;
			result->updateSentMedia(media);
		}
		return result;
	}
	return HistoryItem::Create(this, message);
}

std::vector<not_null<HistoryItem*>> History::createItems(
		const QVector<MTPMessage> &data) {
	auto result = std::vector<not_null<HistoryItem*>>();
	result.reserve(data.size());
	for (auto i = data.cend(), e = data.cbegin(); i != e;) {
		const auto detachExistingItem = true;
		if (const auto item = createItem(*--i, detachExistingItem)) {
			result.push_back(item);
		}
	}
	return result;
}

not_null<HistoryItem*> History::addNewService(
		MsgId msgId,
		TimeId date,
		const QString &text,
		MTPDmessage::Flags flags,
		bool unread) {
	auto message = HistoryService::PreparedText { text };
	return addNewItem(
		new HistoryService(this, msgId, date, message, flags),
		unread);
}

HistoryItem *History::addNewMessage(
		const MTPMessage &msg,
		NewMessageType type) {
	if (type == NewMessageExisting) {
		return addToHistory(msg);
	}
	if (!loadedAtBottom() || peer->migrateTo()) {
		if (const auto item = addToHistory(msg)) {
			setLastMessage(item);
			if (type == NewMessageUnread) {
				newItemAdded(item);
			}
			return item;
		}
		return nullptr;
	}

	return addNewToLastBlock(msg, type);
}

HistoryItem *History::addNewToLastBlock(
		const MTPMessage &msg,
		NewMessageType type) {
	Expects(type != NewMessageExisting);

	const auto detachExistingItem = (type != NewMessageLast);
	const auto item = createItem(msg, detachExistingItem);
	if (!item || item->mainView()) {
		return item;
	}
	const auto newUnreadMessage = (type == NewMessageUnread);
	if (newUnreadMessage) {
		applyMessageChanges(item, msg);
	}
	const auto result = addNewItem(item, newUnreadMessage);
	if (type == NewMessageLast) {
		// When we add just one last item, like we do while loading dialogs,
		// we want to remove a single added grouped media, otherwise it will
		// jump once we open the message history (first we show only that
		// media, then we load the rest of the group and show the group).
		//
		// That way when we open the message history we show nothing until a
		// whole history part is loaded, it certainly will contain the group.
		removeOrphanMediaGroupPart();
	}
	return result;
}

HistoryItem *History::addToHistory(const MTPMessage &msg) {
	const auto detachExistingItem = false;
	return createItem(msg, detachExistingItem);
}

not_null<HistoryItem*> History::addNewForwarded(
		MsgId id,
		MTPDmessage::Flags flags,
		TimeId date,
		UserId from,
		const QString &postAuthor,
		not_null<HistoryMessage*> original) {
	return addNewItem(
		new HistoryMessage(
			this,
			id,
			flags,
			date,
			from,
			postAuthor,
			original),
		true);
}

not_null<HistoryItem*> History::addNewDocument(
		MsgId id,
		MTPDmessage::Flags flags,
		UserId viaBotId,
		MsgId replyTo,
		TimeId date,
		UserId from,
		const QString &postAuthor,
		not_null<DocumentData*> document,
		const TextWithEntities &caption,
		const MTPReplyMarkup &markup) {
	return addNewItem(
		new HistoryMessage(
			this,
			id,
			flags,
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

not_null<HistoryItem*> History::addNewPhoto(
		MsgId id,
		MTPDmessage::Flags flags,
		UserId viaBotId,
		MsgId replyTo,
		TimeId date,
		UserId from,
		const QString &postAuthor,
		not_null<PhotoData*> photo,
		const TextWithEntities &caption,
		const MTPReplyMarkup &markup) {
	return addNewItem(
		new HistoryMessage(
			this,
			id,
			flags,
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

not_null<HistoryItem*> History::addNewGame(
		MsgId id,
		MTPDmessage::Flags flags,
		UserId viaBotId,
		MsgId replyTo,
		TimeId date,
		UserId from,
		const QString &postAuthor,
		not_null<GameData*> game,
		const MTPReplyMarkup &markup) {
	return addNewItem(
		new HistoryMessage(
			this,
			id,
			flags,
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
	if (_unreadMentions.size() > count) {
		LOG(("API Warning: real mentions count is greater than received mentions count"));
		count = _unreadMentions.size();
	}
	_unreadMentionsCount = count;
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
			++*_unreadMentionsCount;
			_unreadMentions.insert(msgId);
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

not_null<HistoryItem*> History::addNewItem(
		not_null<HistoryItem*> item,
		bool unread) {
	Expects(!isBuildingFrontBlock());

	addItemToBlock(item);

	if (!unread && IsServerMsgId(item->id)) {
		if (const auto sharedMediaTypes = item->sharedMediaTypes()) {
			auto from = loadedAtTop() ? 0 : minMsgId();
			auto till = loadedAtBottom() ? ServerMaxMsgId : maxMsgId();
			Auth().storage().add(Storage::SharedMediaAddExisting(
				peer->id,
				sharedMediaTypes,
				item->id,
				{ from, till }));
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
					// admins information outdated
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

	Auth().data().notifyHistoryChangeDelayed(this);
	return item;
}

void History::applyMessageChanges(
		not_null<HistoryItem*> item,
		const MTPMessage &data) {
	if (data.type() == mtpc_messageService) {
		applyServiceChanges(item, data.c_messageService());
	}
	App::checkSavedGif(item);
}

void History::applyServiceChanges(
		not_null<HistoryItem*> item,
		const MTPDmessageService &data) {
	auto &action = data.vaction;
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
			if (auto user = item->from()->asUser()) {
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
		if (const auto chat = peer->asChat()) {
			chat->setPhoto(MTP_chatPhotoEmpty());
		}
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
			Data::ChannelAdminChanges(megagroup).feed(uid, false);
		}
	} break;

	case mtpc_messageActionChatEditPhoto: {
		auto &d = action.c_messageActionChatEditPhoto();
		if (d.vphoto.type() == mtpc_photo) {
			auto &sizes = d.vphoto.c_photo().vsizes.v;
			if (!sizes.isEmpty()) {
				auto photo = Auth().data().photo(d.vphoto.c_photo());
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
					const auto newPhotoId = photo ? photo->id : 0;
					if (const auto chat = peer->asChat()) {
						chat->setPhoto(newPhotoId, MTP_chatPhoto(*smallLoc, *bigLoc));
					} else if (const auto channel = peer->asChannel()) {
						channel->setPhoto(newPhotoId, MTP_chatPhoto(*smallLoc, *bigLoc));
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
		if (data.has_reply_to_msg_id() && item) {
			if (auto channel = item->history()->peer->asChannel()) {
				channel->setPinnedMessageId(data.vreply_to_msg_id.v);
			}
		}
	} break;

	case mtpc_messageActionPhoneCall: {
		Calls::Current().newServiceMessage().notify(item->fullId());
	} break;
	}
}

void History::clearSendAction(not_null<UserData*> from) {
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

void History::mainViewRemoved(
		not_null<HistoryBlock*> block,
		not_null<HistoryView::Element*> view) {
	if (_joinedMessage == view->data()) {
		_joinedMessage = nullptr;
	}
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
	App::checkImageCacheSize();
	item->indexAsNewItem();
	if (const auto from = item->from() ? item->from()->asUser() : nullptr) {
		if (from == item->author()) {
			clearSendAction(from);
		}
		from->madeAction(item->date());
	}
	if (item->out()) {
		destroyUnreadBar();
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
	if (scrollTopItem == was) scrollTopItem= now;
	if (_firstUnreadView == was) _firstUnreadView= now;
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
		Auth().storage().add(Storage::SharedMediaAddSlice(
			peer->id,
			type,
			{},
			{ from, till }));
	}
}

void History::addOlderSlice(const QVector<MTPMessage> &slice) {
	if (slice.isEmpty()) {
		_loadedAtTop = true;
		checkJoinedMessage();
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

	checkJoinedMessage();
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

	checkJoinedMessage();
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
	for (const auto item : base::reversed(items)) {
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
			Auth().storage().add(Storage::SharedMediaAddSlice(
				peer->id,
				type,
				std::move(medias[i]),
				{ from, till }));
		}
	}
}

int History::countUnread(MsgId upTo) {
	int result = 0;
	for (auto i = blocks.cend(), e = blocks.cbegin(); i != e;) {
		--i;
		const auto &messages = (*i)->messages;
		for (auto j = messages.cend(), en = messages.cbegin(); j != en;) {
			--j;
			const auto item = (*j)->data();
			if (item->id > 0 && item->id <= upTo) {
				break;
			} else if (!item->out() && item->unread() && item->id > upTo) {
				++result;
			}
		}
	}
	return result;
}

void History::calculateFirstUnreadMessage() {
	if (_firstUnreadView || !_inboxReadBefore) {
		return;
	}

	for (auto i = blocks.cend(); i != blocks.cbegin();) {
		--i;
		const auto &messages = (*i)->messages;
		for (auto j = messages.cend(); j != messages.cbegin();) {
			--j;
			const auto view = j->get();
			const auto item = view->data();
			if (!IsServerMsgId(item->id)) {
				continue;
			} else if (!item->out() || !_firstUnreadView) {
				if (item->id >= *_inboxReadBefore) {
					_firstUnreadView = view;
				} else {
					return;
				}
			}
		}
	}
}

MsgId History::readInbox() {
	const auto upTo = msgIdForRead();
	changeUnreadCount(-unreadCount());
	if (upTo) {
		inboxRead(upTo);
	}
	return upTo;
}

void History::inboxRead(MsgId upTo) {
	if (const auto nowUnreadCount = unreadCount()) {
		if (loadedAtBottom()) {
			App::main()->historyToDown(this);
		}
		changeUnreadCount(countUnread(upTo) - nowUnreadCount);
	}
	setInboxReadTill(upTo);
	updateChatListEntry();
	if (peer->migrateTo()) {
		if (auto migrateTo = App::historyLoaded(peer->migrateTo()->id)) {
			migrateTo->updateChatListEntry();
		}
	}

	_firstUnreadView = nullptr;
	Auth().notifications().clearFromHistory(this);
}

void History::inboxRead(not_null<const HistoryItem*> wasRead) {
	if (IsServerMsgId(wasRead->id)) {
		inboxRead(wasRead->id);
	}
}

void History::outboxRead(MsgId upTo) {
	setOutboxReadTill(upTo);
	if (const auto last = lastMessage()) {
		if (last->out() && IsServerMsgId(last->id) && last->id <= upTo) {
			if (const auto main = App::main()) {
				main->repaintDialogRow(this, last->id);
			}
		}
	}
	updateChatListEntry();
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

HistoryItem *History::lastAvailableMessage() const {
	return isEmpty() ? nullptr : blocks.back()->messages.back()->data().get();
}

int History::unreadCount() const {
	return _unreadCount ? *_unreadCount : 0;
}

int History::historiesUnreadCount() const {
	const auto result = unreadCount();
	return (!result && unreadMark()) ? 1 : result;
}

bool History::unreadCountKnown() const {
	return !!_unreadCount;
}

void History::setUnreadCount(int newUnreadCount) {
	if (!_unreadCount || *_unreadCount != newUnreadCount) {
		const auto unreadCountDelta = _unreadCount | [&](int count) {
			return newUnreadCount - count;
		};
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
		} else {
			if (!_firstUnreadView && !_unreadBarView && loadedAtBottom()) {
				calculateFirstUnreadMessage();
			}
		}
		const auto unreadMarkDelta = [&] {
			if (_unreadMark) {
				const auto was = _unreadCount && (*_unreadCount > 0);
				const auto now = (newUnreadCount > 0);
				if (was != now) {
					return was ? 1 : -1;
				}
			}
			return 0;
		}();
		_unreadCount = newUnreadCount;

		if (_unreadBarView) {
			const auto count = chatListUnreadCount();
			if (count > 0) {
				_unreadBarView->setUnreadBarCount(count);
			} else {
				_unreadBarView->setUnreadBarFreezed();
			}
		}

		if (inChatList(Dialogs::Mode::All)) {
			const auto delta = unreadCountDelta
				? *unreadCountDelta
				: newUnreadCount;
			App::histories().unreadIncrement(
				delta + unreadMarkDelta,
				mute());
		}
		Notify::peerUpdatedDelayed(
			peer,
			Notify::PeerUpdate::Flag::UnreadViewChanged);
	}
}

void History::setUnreadMark(bool unread) {
	if (_unreadMark != unread) {
		_unreadMark = unread;
		if (!_unreadCount || !*_unreadCount) {
			if (inChatList(Dialogs::Mode::All)) {
				const auto delta = _unreadMark ? 1 : -1;
				App::histories().unreadIncrement(delta, mute());

				updateChatListEntry();
			}
		}
		Notify::peerUpdatedDelayed(
			peer,
			Notify::PeerUpdate::Flag::UnreadViewChanged);
	}
}

bool History::unreadMark() const {
	return _unreadMark;
}

void History::changeUnreadCount(int delta) {
	if (_unreadCount) {
		setUnreadCount(std::max(*_unreadCount + delta, 0));
	}
	if (const auto channel = peer->asChannel()) {
		if (const auto feed = channel->feed()) {
			const auto mutedCountDelta = mute() ? delta : 0;
			feed->unreadCountChanged(delta, mutedCountDelta);
		}
	}
}

bool History::mute() const {
	return _mute;
}

bool History::changeMute(bool newMute) {
	if (_mute == newMute) {
		return false;
	}
	_mute = newMute;

	const auto feed = peer->isChannel()
		? peer->asChannel()->feed()
		: nullptr;
	if (feed) {
		if (_unreadCount) {
			if (*_unreadCount) {
				const auto unreadCountDelta = 0;
				const auto mutedCountDelta = _mute ? *_unreadCount : -*_unreadCount;
				feed->unreadCountChanged(unreadCountDelta, mutedCountDelta);
			}
		} else {
			Auth().api().requestDialogEntry(this);
			Auth().api().requestDialogEntry(feed);
		}
	}
	if (inChatList(Dialogs::Mode::All)) {
		if (const auto count = historiesUnreadCount()) {
			App::histories().unreadMuteChanged(count, _mute);
			Notify::unreadCounterUpdated();
		}
		Notify::historyMuteUpdated(this);
	}
	updateChatListEntry();
	Notify::peerUpdatedDelayed(
		peer,
		Notify::PeerUpdate::Flag::NotificationsEnabled);
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

std::shared_ptr<AdminLog::LocalIdManager> History::adminLogIdManager() {
	if (const auto strong = _adminLogIdManager.lock()) {
		return strong;
	}
	auto result = std::make_shared<AdminLog::LocalIdManager>();
	_adminLogIdManager = result;
	return result;
}

TimeId History::adjustChatListTimeId() const {
	const auto result = chatsListTimeId();
	if (const auto draft = cloudDraft()) {
		if (!Data::draftIsNull(draft)) {
			return std::max(result, draft->date);
		}
	}
	return result;
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
					scrollTopItem = view;
					return;
				}
			}
			if (--blockIndex >= 0) {
				itemIndex = blocks[blockIndex]->messages.size();
			} else {
				break;
			}
		} while (true);

		scrollTopItem = blocks.front()->messages.front().get();
	} else {
		// go forward through history while we don't find the last item that starts above
		for (auto blocksCount = int(blocks.size()); blockIndex < blocksCount; ++blockIndex) {
			const auto &block = blocks[blockIndex];
			for (auto itemsCount = int(block->messages.size()); itemIndex < itemsCount; ++itemIndex) {
				itemTop = block->y() + block->messages[itemIndex]->y();
				if (itemTop > top) {
					Assert(itemIndex > 0 || blockIndex > 0);
					if (itemIndex > 0) {
						scrollTopItem = block->messages[itemIndex - 1].get();
					} else {
						scrollTopItem = blocks[blockIndex - 1]->messages.back().get();
					}
					return;
				}
			}
			itemIndex = 0;
		}
		scrollTopItem = blocks.back()->messages.back().get();
	}
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
		_unreadBarView->setUnreadBarCount(count);
	}
}

void History::destroyUnreadBar() {
	if (const auto view = base::take(_unreadBarView)) {
		view->destroyUnreadBar();
	}
}

bool History::hasNotFreezedUnreadBar() const {
	if (_firstUnreadView) {
		if (const auto view = _unreadBarView) {
			if (const auto bar = view->Get<HistoryView::UnreadBar>()) {
				return !bar->freezed;
			}
		}
	}
	return false;
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
	return App::historyLoaded(addFromId);
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

HistoryItem *History::chatsListItem() const {
	return lastMessage();
}

const QString &History::chatsListName() const {
	return peer->name;
}

const base::flat_set<QString> &History::chatsListNameWords() const {
	return peer->nameWords();
}

const base::flat_set<QChar> &History::chatsListFirstLetters() const {
	return peer->nameFirstLetters();
}

void History::loadUserpic() {
	peer->loadUserpic();
}

void History::paintUserpic(
		Painter &p,
		int x,
		int y,
		int size) const {
	peer->paintUserpic(p, x, y, size);
}

void History::startBuildingFrontBlock(int expectedItemsCount) {
	Assert(!isBuildingFrontBlock());
	Assert(expectedItemsCount > 0);

	_buildingFrontBlock.reset(new BuildingBlock());
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
	notifies.clear();
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
		return App::history(peer->migrateFrom()->id)->isReadyFor(-msgId);
	}

	if (msgId == ShowAtTheEndMsgId) {
		return loadedAtBottom();
	}
	if (msgId == ShowAtUnreadMsgId) {
		if (const auto migratePeer = peer->migrateFrom()) {
			if (const auto migrated = App::historyLoaded(migratePeer)) {
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
	HistoryItem *item = App::histItemById(channelId(), msgId);
	return item && (item->history() == this) && item->mainView();
}

void History::getReadyFor(MsgId msgId) {
	if (msgId < 0 && -msgId < ServerMaxMsgId && peer->migrateFrom()) {
		const auto migrated = App::history(peer->migrateFrom()->id);
		migrated->getReadyFor(-msgId);
		if (migrated->isEmpty()) {
			unloadBlocks();
		}
		return;
	}
	if (msgId == ShowAtUnreadMsgId) {
		if (const auto migratePeer = peer->migrateFrom()) {
			if (const auto migrated = App::historyLoaded(migratePeer)) {
				if (migrated->unreadCount()) {
					unloadBlocks();
					migrated->getReadyFor(msgId);
					return;
				}
			}
		}
	}
	if (!isReadyFor(msgId)) {
		unloadBlocks();
		if (const auto migratePeer = peer->migrateFrom()) {
			if (const auto migrated = App::historyLoaded(migratePeer)) {
				migrated->unloadBlocks();
			}
		}
		if (msgId == ShowAtTheEndMsgId) {
			_loadedAtBottom = true;
		}
	}
}

void History::setNotLoadedAtBottom() {
	_loadedAtBottom = false;

	Auth().storage().invalidate(
		Storage::SharedMediaInvalidateBottom(peer->id));
	if (const auto channel = peer->asChannel()) {
		if (const auto feed = channel->feed()) {
			Auth().storage().invalidate(
				Storage::FeedMessagesInvalidateBottom(
					feed->id()));
		}
	}
}

void History::markFullyLoaded() {
	_loadedAtTop = _loadedAtBottom = true;
	if (isEmpty()) {
		Auth().storage().remove(Storage::SharedMediaRemoveAll(peer->id));
		if (const auto channel = peer->asChannel()) {
			if (const auto feed = channel->feed()) {
				Auth().storage().remove(Storage::FeedMessagesRemoveAll(
					feed->id(),
					channel->bareId()));
			}
		}
	}
}

void History::setLastMessage(HistoryItem *item) {
	if (item) {
		if (_lastMessage) {
			if (!*_lastMessage) {
				Local::removeSavedPeer(peer);
			} else if (!IsServerMsgId((*_lastMessage)->id)
				&& (*_lastMessage)->date() > item->date()) {
				return;
			}
		}
		_lastMessage = item;
		if (const auto feed = peer->feed()) {
			feed->updateLastMessage(item);
		}
		setChatsListTimeId(item->date());
	} else if (!_lastMessage || *_lastMessage) {
		_lastMessage = nullptr;
		updateChatListEntry();
	}
}

HistoryItem *History::lastMessage() const {
	return _lastMessage ? (*_lastMessage) : nullptr;
}

bool History::lastMessageKnown() const {
	return !!_lastMessage;
}

void History::updateChatListExistence() {
	Entry::updateChatListExistence();
	if (!lastMessageKnown() || !unreadCountKnown()) {
		if (const auto channel = peer->asChannel()) {
			if (!channel->feed()) {
				// After ungrouping from a feed we need to load dialog.
				Auth().api().requestDialogEntry(this);
			}
		}
	}
}

bool History::useProxyPromotion() const {
	if (!isProxyPromoted()) {
		return false;
	} else if (const auto channel = peer->asChannel()) {
		return !isPinnedDialog() && !channel->amIn();
	}
	return false;
}

bool History::shouldBeInChatList() const {
	if (peer->migrateTo()) {
		return false;
	} else if (isPinnedDialog()) {
		return true;
	} else if (const auto channel = peer->asChannel()) {
		if (!channel->amIn()) {
			return isProxyPromoted();
		} else if (const auto feed = channel->feed()) {
			return !feed->needUpdateInChatList();
		}
	}
	return true;
}

void History::unknownMessageDeleted(MsgId messageId) {
	if (_inboxReadBefore && messageId >= *_inboxReadBefore) {
		changeUnreadCount(-1);
	}
}

bool History::isServerSideUnread(not_null<const HistoryItem*> item) const {
	Expects(IsServerMsgId(item->id));

	return item->out()
		? (!_outboxReadBefore || (item->id >= *_outboxReadBefore))
		: (!_inboxReadBefore || (item->id >= *_inboxReadBefore));
}

void History::applyDialog(const MTPDdialog &data) {
	applyDialogFields(
		data.vunread_count.v,
		data.vread_inbox_max_id.v,
		data.vread_outbox_max_id.v);
	applyDialogTopMessage(data.vtop_message.v);
	setUnreadMark(data.is_unread_mark());
	setUnreadMentionsCount(data.vunread_mentions_count.v);
	if (const auto channel = peer->asChannel()) {
		if (data.has_pts()) {
			channel->ptsReceived(data.vpts.v);
		}
		if (!channel->amCreator()) {
			const auto topMessageId = FullMsgId(
				peerToChannel(channel->id),
				data.vtop_message.v);
			if (const auto item = App::histItemById(topMessageId)) {
				if (item->date() <= channel->date) {
					Auth().api().requestSelfParticipant(channel);
				}
			}
		}
	}
	Auth().data().applyNotifySetting(
		MTP_notifyPeer(data.vpeer),
		data.vnotify_settings);

	if (data.has_draft() && data.vdraft.type() == mtpc_draftMessage) {
		Data::applyPeerCloudDraft(peer->id, data.vdraft.c_draftMessage());
	}
}

bool History::skipUnreadUpdateForClientSideUnread() const {
	if (peer->id != peerFromUser(ServiceUserId)) {
		return false;
	} else if (!_unreadCount || !*_unreadCount) {
		return false;
	} else if (!_lastMessage || IsServerMsgId((*_lastMessage)->id)) {
		return false;
	}
	return true;
}

void History::applyDialogFields(
		int unreadCount,
		MsgId maxInboxRead,
		MsgId maxOutboxRead) {
	if (!skipUnreadUpdateForClientSideUnread()) {
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
		if (const auto item = App::histItemById(itemId)) {
			setLastMessage(item);
		} else {
			setLastMessage(nullptr);
		}
	} else {
		setLastMessage(nullptr);
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
	for (const auto &block : base::reversed(blocks)) {
		for (const auto &message : base::reversed(block->messages)) {
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
	for (const auto &block : base::reversed(blocks)) {
		for (const auto &message : base::reversed(block->messages)) {
			const auto item = message->data();
			if (IsServerMsgId(item->id)
				&& !item->serviceMsg()
				&& (item->out() || peer->isSelf())) {
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

MsgRange History::rangeForDifferenceRequest() const {
	auto fromId = MsgId(0);
	auto toId = MsgId(0);
	for (auto blockIndex = 0, blocksCount = int(blocks.size()); blockIndex < blocksCount; ++blockIndex) {
		const auto &block = blocks[blockIndex];
		for (auto itemIndex = 0, itemsCount = int(block->messages.size()); itemIndex < itemsCount; ++itemIndex) {
			const auto id = block->messages[itemIndex]->data()->id;
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

HistoryService *History::insertJoinedMessage(bool unread) {
	if (!isChannel()
		|| _joinedMessage
		|| !peer->asChannel()->amIn()
		|| (peer->isMegagroup()
			&& peer->asChannel()->mgInfo->joinedMessageFound)) {
		return _joinedMessage;
	}

	const auto inviter = (peer->asChannel()->inviter > 0)
		? App::userLoaded(peer->asChannel()->inviter)
		: nullptr;
	if (!inviter) {
		return nullptr;
	}

	MTPDmessage::Flags flags = 0;
	if (inviter->id == Auth().userPeerId()) {
		unread = false;
	//} else if (unread) {
	//	flags |= MTPDmessage::Flag::f_unread;
	}

	const auto inviteDate = peer->asChannel()->inviteDate;
	if (isEmpty()) {
		_joinedMessage = GenerateJoinedMessage(
			this,
			inviteDate,
			inviter,
			flags);
		addNewItem(_joinedMessage, unread);
		return _joinedMessage;
	}

	for (auto blockIndex = blocks.size(); blockIndex > 0;) {
		const auto &block = blocks[--blockIndex];
		for (auto itemIndex = block->messages.size(); itemIndex > 0;) {
			const auto item = block->messages[--itemIndex]->data();

			// Due to a server bug sometimes inviteDate is less (before) than the
			// first message in the megagroup (message about migration), let us
			// ignore that and think, that the inviteDate is always greater-or-equal.
			if (item->isGroupMigrate()
				&& peer->isMegagroup()
				&& peer->migrateFrom()) {
				peer->asChannel()->mgInfo->joinedMessageFound = true;
				return nullptr;
			}
			if (item->date() <= inviteDate) {
				++itemIndex;
				_joinedMessage = GenerateJoinedMessage(
					this,
					inviteDate,
					inviter,
					flags);
				addNewInTheMiddle(_joinedMessage, blockIndex, itemIndex);
				const auto lastDate = chatsListTimeId();
				if (!lastDate || inviteDate >= lastDate) {
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
	_joinedMessage = GenerateJoinedMessage(
		this,
		inviteDate,
		inviter,
		flags);
	addItemToBlock(_joinedMessage);
	finishBuildingFrontBlock();

	return _joinedMessage;
}

void History::checkJoinedMessage(bool createUnread) {
	if (!isChannel() || _joinedMessage || peer->asChannel()->inviter <= 0) {
		return;
	}
	if (isEmpty()) {
		if (loadedAtTop() && loadedAtBottom()) {
			if (insertJoinedMessage(createUnread)) {
				if (_joinedMessage->mainView()) {
					setLastMessage(_joinedMessage);
				}
			}
			return;
		}
	}

	const auto inviteDate = peer->asChannel()->inviteDate;
	auto firstDate = TimeId(0);
	auto lastDate = TimeId(0);
	if (!blocks.empty()) {
		firstDate = blocks.front()->messages.front()->data()->date();
		lastDate = blocks.back()->messages.back()->data()->date();
	}
	if (firstDate
		&& lastDate
		&& (firstDate <= inviteDate || loadedAtTop())
		&& (lastDate > inviteDate || loadedAtBottom())) {
		const auto willBeLastMsg = (inviteDate >= lastDate);
		if (insertJoinedMessage(createUnread && willBeLastMsg)
			&& willBeLastMsg) {
			if (_joinedMessage->mainView()) {
				setLastMessage(_joinedMessage);
			}
		}
	}
}

void History::removeJoinedMessage() {
	if (_joinedMessage) {
		base::take(_joinedMessage)->destroy();
	}
}

bool History::isEmpty() const {
	return blocks.empty();
}

bool History::isDisplayedEmpty() const {
	return isEmpty() || ((blocks.size() == 1)
		&& blocks.front()->messages.size() == 1
		&& blocks.front()->messages.front()->data()->isEmpty());
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
		unloadBlocks();
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

void History::clear() {
	clearBlocks(false);
}

void History::unloadBlocks() {
	clearBlocks(true);
}

void History::clearBlocks(bool leaveItems) {
	_unreadBarView = nullptr;
	_firstUnreadView = nullptr;
	_joinedMessage = nullptr;

	if (scrollTopItem) {
		forgetScrollState();
	}
	if (leaveItems) {
		Auth().data().notifyHistoryUnloaded(this);
	} else {
		setLastMessage(nullptr);
		notifies.clear();
		Auth().data().notifyHistoryCleared(this);
	}
	blocks.clear();
	if (leaveItems) {
		lastKeyboardInited = false;
	} else {
		changeUnreadCount(-unreadCount());
		if (auto channel = peer->asChannel()) {
			channel->clearPinnedMessage();
			if (const auto feed = channel->feed()) {
				// Should be after setLastMessage(nullptr);
				feed->historyCleared(this);
			}
		}
		clearLastKeyboard();
	}
	Auth().data().notifyHistoryChangeDelayed(this);

	_loadedAtTop = false;
	_loadedAtBottom = !leaveItems;
	forgetScrollState();
	if (const auto chat = peer->asChat()) {
		chat->lastAuthors.clear();
		chat->markupSenders.clear();
	} else if (const auto channel = peer->asMegagroup()) {
		channel->mgInfo->markupSenders.clear();
	}
}

void History::clearUpTill(MsgId availableMinId) {
	auto minId = minMsgId();
	if (!minId || minId > availableMinId) {
		return;
	}
	do {
		const auto item = blocks.front()->messages.front()->data();
		const auto itemId = item->id;
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
					MTP_int(item->date()),
					MTP_messageActionHistoryClear()
				).c_messageService());
			}
			break;
		}
		item->destroy();
	} while (!isEmpty());

	if (!lastMessageKnown()) {
		Auth().api().requestDialogEntry(this);
	}
	Auth().data().sendHistoryChangeNotifications();
}

void History::applyGroupAdminChanges(
		const base::flat_map<UserId, bool> &changes) {
	for (const auto &block : blocks) {
		for (const auto &message : block->messages) {
			message->data()->applyGroupAdminChanges(changes);
		}
	}
}

void History::changedInChatListHook(Dialogs::Mode list, bool added) {
	if (list == Dialogs::Mode::All) {
		if (const auto delta = historiesUnreadCount() * (added ? 1 : -1)) {
			App::histories().unreadIncrement(delta, mute());
		}
	}
}

void History::changedChatListPinHook() {
	Notify::peerUpdatedDelayed(
		peer,
		Notify::PeerUpdate::Flag::PinnedChanged);
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
	auto refreshed = item->createView(HistoryInner::ElementDelegate());

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
